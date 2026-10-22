"""Native LINK/0.1 TCP regression tests; no sibling server checkout required."""
from __future__ import annotations

import argparse
import pathlib
import shutil
import socket
import socketserver
import struct
import subprocess
import tempfile
import threading
import time
import zlib

HEADER = struct.Struct("!4sBBBBII")
HELLO, WELCOME, GET, RESOURCE, ERROR, GOODBYE = range(1, 7)
MAX_RESOURCE = 16 * 1024 * 1024


def frame(kind: int, request_id: int, payload: bytes = b"", *,
          magic: bytes = b"LINK", major: int = 0, minor: int = 1,
          flags: int = 0, length: int | None = None) -> bytes:
    return HEADER.pack(magic, major, minor, kind, flags, request_id,
                       len(payload) if length is None else length) + payload


def png() -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))
    pixels = b"\x00\x12\x89\x77\xff\xf4\xf8\xff\xff" * 2
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", 2, 2, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b""))


class LinkFixture(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True
    block_on_close = False

    def __init__(self, root: pathlib.Path):
        self.root = root
        self.gate = threading.Lock()
        self.sessions: list[dict] = []
        self.errors: list[str] = []
        self.stopping = threading.Event()
        super().__init__(("127.0.0.1", 0), Handler)

    def snapshot(self) -> list[dict]:
        with self.gate:
            return [dict(session, gets=list(session["gets"])) for session in self.sessions]


class Handler(socketserver.BaseRequestHandler):
    server: LinkFixture

    def exact(self, length: int) -> bytes:
        result = bytearray()
        while len(result) < length:
            block = self.request.recv(length - len(result))
            if not block:
                raise EOFError
            result.extend(block)
        return bytes(result)

    def receive(self):
        magic, major, minor, kind, flags, request_id, size = HEADER.unpack(self.exact(HEADER.size))
        assert (magic, major, minor, flags) == (b"LINK", 0, 1, 0), "invalid client frame header"
        assert size <= 4096, "unbounded client frame"
        return kind, request_id, self.exact(size)

    def stall(self):
        # This also makes every cancellation test prove the peer actually closed.
        self.request.settimeout(0.1)
        while not self.server.stopping.is_set():
            try:
                if not self.request.recv(1):
                    return
            except socket.timeout:
                continue

    def fragmented(self, data: bytes):
        offset = 0
        for size in (1, 2, 1, 3, 2, 4, 1, 2):
            self.request.sendall(data[offset:offset + size])
            offset += size
            time.sleep(0.002)
        for offset in range(offset, len(data), 3):
            self.request.sendall(data[offset:offset + 3])
            time.sleep(0.001)

    def handle(self):
        self.request.settimeout(4)
        session = None
        try:
            kind, request_id, hello = self.receive()
            assert (kind, request_id) == (HELLO, 0), "connection must begin with HELLO id 0"
            assert hello.startswith(b"\x00\x01"), "HELLO must select world format 0.1"
            world = hello[2:].decode("ascii")
            assert world and all(c.isalnum() or c == "-" for c in world), "HELLO world must be a slug"
            session = {"world": world, "gets": [], "goodbye": False}
            with self.server.gate:
                self.server.sessions.append(session)
            if world == "hello-slow":
                self.stall()
                return
            if world.startswith("welcome-"):
                mode = world.removeprefix("welcome-")
                variants = {
                    "bad-magic": frame(WELCOME, 0, hello, magic=b"FAIL"),
                    "bad-major": frame(WELCOME, 0, hello, major=1),
                    "bad-minor": frame(WELCOME, 0, hello, minor=2),
                    "bad-flags": frame(WELCOME, 0, hello, flags=1),
                    "bad-type": frame(RESOURCE, 0, hello),
                    "bad-id": frame(WELCOME, 1, hello),
                    "bad-echo": frame(WELCOME, 0, b"\x00\x01other-world"),
                    "bad-format": frame(WELCOME, 0, b"\x00\x02" + world.encode("ascii")),
                    "short-body": frame(WELCOME, 0, b"\x00"),
                    "error": frame(ERROR, 0, b"\x00\x02world not found"),
                    "error-bad-id": frame(ERROR, 1, b"\x00\x02world not found"),
                    "too-large": frame(WELCOME, 0, length=4096),
                    "truncated": frame(WELCOME, 0, hello)[:-1],
                    "http": b"HTTP/1.1 200 OK\r\nContent-Length: 0\r\n\r\n",
                }
                if mode in variants:
                    self.request.sendall(variants[mode])
                    return
            welcome = frame(WELCOME, 0, hello)
            if world == "welcome-fragmented":
                self.fragmented(welcome)
            else:
                self.request.sendall(welcome)
            expected_id = 1
            while not self.server.stopping.is_set():
                kind, request_id, payload = self.receive()
                assert request_id == expected_id, f"nonconsecutive client id {request_id}, expected {expected_id}"
                expected_id += 1
                if kind == GOODBYE:
                    assert payload == b"", "GOODBYE body must be empty"
                    with self.server.gate:
                        session["goodbye"] = True
                    return
                assert kind == GET and len(payload) > 4, "expected GET with cap and resource path"
                cap = struct.unpack("!I", payload[:4])[0]
                path = payload[4:].decode("utf-8")
                assert 1 <= cap <= MAX_RESOURCE, "GET cap exceeds wire limit"
                assert len(payload[4:]) <= 2048, "GET resource path exceeds limit"
                assert not path.startswith("/") and all(p not in ("", ".", "..") for p in path.split("/")), "GET path must be canonical"
                assert not any(c in path for c in "%\\\x00"), "GET path must be decoded UTF-8"
                with self.server.gate:
                    session["gets"].append((request_id, path, cap))
                if world == "cancel-load" and path == "appearance.style":
                    self.request.sendall(frame(RESOURCE, request_id, b"x", length=128))
                    self.stall()
                    return
                if self.response(world, path, request_id, cap):
                    return
        except (EOFError, ConnectionError, socket.timeout, OSError):
            pass
        except BaseException as error:
            with self.server.gate:
                self.server.errors.append(f"{session}: {error!r}")

    def response(self, world: str, path: str, request_id: int, cap: int) -> bool:
        bad = {
            "bad-magic": frame(RESOURCE, request_id, b"x", magic=b"FAIL"),
            "bad-major": frame(RESOURCE, request_id, b"x", major=1),
            "bad-minor": frame(RESOURCE, request_id, b"x", minor=2),
            "bad-flags": frame(RESOURCE, request_id, b"x", flags=0x80),
            "bad-type": frame(99, request_id, b"x"),
            "bad-id-zero": frame(RESOURCE, 0, b"x"),
            "bad-id-future": frame(RESOURCE, request_id + 1, b"x"),
            "bad-id-previous": frame(RESOURCE, request_id - 1, b"x"),
            "hello-response": frame(HELLO, request_id, b"x"),
            "welcome-response": frame(WELCOME, request_id, b"x"),
            "goodbye-response": frame(GOODBYE, request_id),
            "oversize": frame(RESOURCE, request_id, length=cap + 1),
            "huge-size": frame(RESOURCE, request_id, length=0xFFFFFFFF),
            "truncated": frame(RESOURCE, request_id, b"short", length=200),
            "truncated-header": frame(RESOURCE, request_id)[:9],
            "error-empty": frame(ERROR, request_id),
            "error-zero-code": frame(ERROR, request_id, b"\x00\x00invalid"),
            "error-unknown-code": frame(ERROR, request_id, b"\x00\x09invalid"),
            "error-invalid-utf8": frame(ERROR, request_id, b"\x00\x02\xC0\xAF"),
            "error-too-large": frame(ERROR, request_id, length=1027),
        }
        if path in bad:
            self.request.sendall(bad[path])
            return True
        if path.startswith("error-code-"):
            code = int(path.rsplit("-", 1)[1])
            self.request.sendall(frame(ERROR, request_id, struct.pack("!H", code) + b"fixture failure"))
            return True
        if path in ("slow", "slow-header"):
            prefix = frame(RESOURCE, request_id, b"x", length=64)
            self.request.sendall(prefix if path == "slow" else prefix[:7])
            self.stall()
            return True
        if path == "deadline":
            self.request.sendall(frame(RESOURCE, request_id, length=1024))
            for _ in range(1024):
                if self.server.stopping.wait(0.1):
                    break
                self.request.sendall(b"x")
            return True
        if path in ("binary", "fragmented", "duplicate"):
            data = b"\x00\xff\x80LINK\x00"
        elif path == "large":
            data = b"x" * MAX_RESOURCE
        elif path == "empty":
            data = b""
        else:
            selected_world = "fixture" if world == "welcome-fragmented" else world
            candidate = (self.server.root / selected_world / path).resolve()
            if not candidate.is_relative_to(self.server.root) or not candidate.is_file():
                self.request.sendall(frame(ERROR, request_id, b"\x00\x02resource not found"))
                return True
            data = candidate.read_bytes()
        if len(data) > cap:
            self.request.sendall(frame(ERROR, request_id, b"\x00\x04resource exceeds requested cap"))
            return True
        packet = frame(RESOURCE, request_id, data)
        if path == "duplicate":
            self.request.sendall(packet + packet)
        elif path == "fragmented":
            self.fragmented(packet)
        else:
            # A whole header and binary payload may arrive in a single TCP read.
            self.request.sendall(packet)
        return False


def write_fixture(root: pathlib.Path):
    world = root / "fixture"
    (world / "assets").mkdir(parents=True)
    (world / "scripts").mkdir()
    (world / "world.link").write_text('''world {
version: "0.1"; id: "fixture"; name: "Remote integration";
elements: "scene.element"; style: "appearance.style"; scripts: ["scripts/test.script"];
entry: {position:[0,2,8],target:[0,1,0]};
portals:[{id:"next",address:"/portal-target",position:[100,1,0],size:[1,2,1]}];
}''', encoding="utf-8")
    (world / "scene.element").write_text('''scene { object remote {
state:{ready:false}; script:"scripts/test.script";
component display { element picture { geometry:{type:"plane",size:[2,2]}; image:"assets/원격 사진.png"; }
 element cube { position:[2,1,0]; geometry:{type:"box",size:[1,1,1]}; } }
} }''', encoding="utf-8")
    (world / "appearance.style").write_text('defaults {color:"#128977";}', encoding="utf-8")
    (world / "scripts" / "test.script").write_text('on onStart(ctx) { ctx.setState({ready:true}); }', encoding="utf-8")
    (world / "assets" / "원격 사진.png").write_bytes(png())
    for target in ("portal-target", "missing-image", "invalid-scene", "cancel-load"):
        shutil.copytree(world, root / target)
    destination = root / "portal-target" / "world.link"
    destination.write_text(destination.read_text(encoding="utf-8").replace('address:"/portal-target"', 'address:"/fixture"'), encoding="utf-8")
    (root / "missing-image" / "assets" / "원격 사진.png").unlink()
    (root / "invalid-scene" / "scene.element").write_text("scene {object broken {unsupported:1;}}", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--loader", type=pathlib.Path, required=True)
    parser.add_argument("--worlds", type=pathlib.Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="link-tcp-fixture-") as temporary:
        root = pathlib.Path(temporary).resolve()
        write_fixture(root)
        server = LinkFixture(root)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        authority = f"link://127.0.0.1:{server.server_address[1]}"
        address = f"{authority}/fixture"
        checks = 0
        try:
            def probe(resource: str, budget: int = 4096, *, ok: bool = True,
                      cancel: int | None = None, world: str = "fixture", timeout: float = 8,
                      repeat: int | None = None):
                nonlocal checks
                command = [str(args.probe.resolve()), f"{authority}/{world}", resource, str(budget)]
                if cancel is not None:
                    command.append(str(cancel))
                if repeat is not None:
                    assert cancel is None
                    command.extend(("--repeat", str(repeat)))
                started = time.monotonic()
                result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=timeout)
                elapsed = time.monotonic() - started
                assert (result.returncode == 0) == ok, (world, resource, result.returncode, result.stdout, result.stderr)
                checks += 1
                return result, elapsed

            def loader(mode: str, world: str = "fixture", extra: str | None = None):
                command = [str(args.loader.resolve()), mode, f"{authority}/{world}", str(args.worlds.resolve())]
                if extra is not None:
                    command.append(extra)
                result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=15)
                assert result.returncode == 0, (mode, world, result.stdout, result.stderr)
                print(result.stdout.strip())

            result, _ = probe("binary")
            assert "hex=00ff804c494e4b00" in result.stdout, result.stdout
            result, _ = probe("fragmented")
            assert "hex=00ff804c494e4b00" in result.stdout
            result, _ = probe("binary", world="welcome-fragmented")
            assert "hex=00ff804c494e4b00" in result.stdout
            result, _ = probe("empty", budget=1)
            assert "bytes=0" in result.stdout
            result, _ = probe("assets/원격 사진.png")
            assert f"bytes={len(png())}" in result.stdout
            for resource in ("missing", "bad-magic", "bad-major", "bad-minor", "bad-flags", "bad-type",
                             "bad-id-zero", "bad-id-future", "bad-id-previous", "hello-response",
                             "welcome-response", "goodbye-response", "oversize", "huge-size",
                             "truncated", "truncated-header", "error-empty", "error-zero-code",
                             "error-unknown-code", "error-invalid-utf8", "error-too-large"):
                probe(resource, 16, ok=False)
            for code in range(1, 9):
                result, _ = probe(f"error-code-{code}", 16, ok=False)
                assert "fixture failure" in result.stderr, result.stderr
            for mode in ("bad-magic", "bad-major", "bad-minor", "bad-flags", "bad-type",
                         "bad-id", "bad-echo", "bad-format", "short-body", "error", "error-bad-id", "too-large", "truncated", "http"):
                probe("binary", world=f"welcome-{mode}", ok=False)
            for resource in ("../world.link", "/world.link", "assets/%2e%2e/secret", "assets\\secret"):
                probe(resource, ok=False)
            probe("binary", budget=0, ok=False)
            probe("binary", budget=MAX_RESOURCE + 1)  # caller cap is clamped to the wire maximum
            probe("duplicate", ok=False, repeat=2)
            before = len(server.snapshot())
            probe("binary", ok=False, repeat=257, timeout=20)
            limited = server.snapshot()[before:]
            assert len(limited) == 1 and len(limited[0]["gets"]) == 256, limited
            before = len(server.snapshot())
            probe("large", budget=MAX_RESOURCE, ok=False, repeat=5, timeout=20)
            limited = server.snapshot()[before:]
            assert len(limited) == 1 and len(limited[0]["gets"]) == 4, limited
            for world, resource, repetitions in (("fixture", "slow", 10), ("fixture", "slow-header", 3),
                                                 ("hello-slow", "binary", 3)):
                for _ in range(repetitions):
                    result, elapsed = probe(resource, world=world, cancel=100, ok=False)
                    assert "cancel" in result.stderr.lower(), result.stderr
                    assert elapsed < 1.5, f"Cancellation took {elapsed:.3f}s"
            assert any(s["world"] == "fixture" and any(p == "slow" for _, p, _ in s["gets"])
                       for s in server.snapshot()), "cancellation test never reached an active resource transfer"

            before = len(server.snapshot())
            loader("--integration")
            loaded = server.snapshot()[before:]
            assert len(loaded) == 2, f"one persistent TCP connection per load/reload required: {loaded}"
            expected = {"world.link", "scene.element", "appearance.style", "scripts/test.script", "assets/원격 사진.png"}
            for session in loaded:
                assert session["world"] == "fixture", session
                assert {p for _, p, _ in session["gets"]} == expected, session
                assert session["goodbye"], "successful snapshot must close with GOODBYE"
            assert not any(s["world"] == "portal-target" for s in server.snapshot()), "portal destination fetched before entry"

            before = len(server.snapshot())
            loader("--visit-portals")
            visited = server.snapshot()[before:]
            assert [s["world"] for s in visited] == ["fixture", "portal-target", "fixture"], visited
            assert all(s["goodbye"] for s in visited), visited
            for broken, expected_error in (("missing-image", "NOT_FOUND"), ("invalid-scene", "unsupported field")):
                loader("--reject", broken, expected_error)
            loader("--cancel", "cancel-load", "100")

            result, elapsed = probe("deadline", ok=False, timeout=25)
            assert "deadline" in result.stderr.lower(), result.stderr
            assert 19 <= elapsed < 23, f"Total operation deadline was reset by incoming bytes: {elapsed:.3f}s"
            assert not server.errors, "\n".join(server.errors)
            print(f"PASS native LINK TCP: {checks} probes, fragmented/coalesced framing, binary+Unicode, strict wire validation, "
                  "caps, bounded cancellation/deadline, persistent snapshots and portal round trip")
        finally:
            server.stopping.set()
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
