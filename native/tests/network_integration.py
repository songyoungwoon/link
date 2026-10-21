"""Real WinHTTP regression tests; stdlib fixture, no sibling server checkout required."""
from __future__ import annotations
import argparse
import http.server
import pathlib
import socket
import shutil
import struct
import subprocess
import tempfile
import threading
import time
import urllib.parse
import zlib


def png() -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return struct.pack("!I", len(data)) + kind + data + struct.pack("!I", zlib.crc32(kind + data))
    pixels = b"\x00\x12\x89\x77\xff\xf4\xf8\xff\xff" * 2
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack("!2I5B", 2, 2, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(pixels)) + chunk(b"IEND", b"")


class Fixture(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    root: pathlib.Path
    counts: dict[str, int] = {}
    gate = threading.Lock()

    def handle(self):
        try:
            super().handle()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def log_message(self, *_args):
        pass

    def do_GET(self):
        try:
            self.reply()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass

    def reply(self):
        path = urllib.parse.unquote(self.path, errors="strict")
        with self.gate:
            self.counts[path] = self.counts.get(path, 0) + 1
        if path.endswith("/redirect"):
            self.send_response(302)
            self.send_header("Location", "/fixture/binary")
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if path.endswith("/oversize"):
            self.send_response(200)
            self.send_header("Content-Length", "1048576")
            self.send_header("Connection", "close")
            self.end_headers()
            self.close_connection = True
            return
        if path.endswith("/chunked"):
            self.send_response(200)
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            self.wfile.write(b"20\r\n" + b"x" * 32 + b"\r\n0\r\n\r\n")
            return
        if path.endswith("/slow"):
            self.send_response(200)
            self.send_header("Content-Length", "64")
            self.end_headers()
            self.wfile.write(b"x")
            self.wfile.flush()
            time.sleep(2)
            self.wfile.write(b"x" * 63)
            return
        if path.endswith("/truncated"):
            self.send_response(200)
            self.send_header("Content-Length", "200")
            self.send_header("Connection", "close")
            self.end_headers()
            self.wfile.write(b"short")
            self.close_connection = True
            return
        if path.endswith("/binary"):
            data = b"\x00\xff\x80LINK\x00"
        else:
            resource = (self.root / path.lstrip("/")).resolve()
            if not resource.is_relative_to(self.root) or not resource.is_file():
                self.send_error(404)
                return
            data = resource.read_bytes()
        self.send_response(200)
        self.send_header("Content-Length", str(len(data)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(data)


def write_fixture(root: pathlib.Path):
    world = root / "fixture"
    (world / "assets").mkdir(parents=True)
    (world / "scripts").mkdir()
    (world / "world.link").write_text('''world {
version: "0.1"; id: "fixture"; name: "Remote integration";
elements: "scene.element"; style: "appearance.style"; scripts: ["scripts/test.script"];
entry: {position:[0,2,8],target:[0,1,0]};
portals:[{id:"next",address:"/missing-target",position:[100,1,0],size:[1,2,1]}];
}''', encoding="utf-8")
    (world / "scene.element").write_text('''scene { object remote {
state:{ready:false}; script:"scripts/test.script";
component display { element picture { geometry:{type:"plane",size:[2,2]}; image:"assets/원격 사진.png"; }
 element cube { position:[2,1,0]; geometry:{type:"box",size:[1,1,1]}; } }
} }''', encoding="utf-8")
    (world / "appearance.style").write_text('defaults {color:"#128977";}', encoding="utf-8")
    (world / "scripts" / "test.script").write_text('on onStart(ctx) { ctx.setState({ready:true}); }', encoding="utf-8")
    (world / "assets" / "원격 사진.png").write_bytes(png())


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--probe", type=pathlib.Path, required=True)
    parser.add_argument("--loader", type=pathlib.Path, required=True)
    parser.add_argument("--worlds", type=pathlib.Path, required=True)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="link-http-fixture-") as temporary:
        Fixture.root = pathlib.Path(temporary).resolve()
        Fixture.counts = {}
        write_fixture(Fixture.root)
        for broken in ("missing-image", "invalid-scene"):
            shutil.copytree(Fixture.root / "fixture", Fixture.root / broken)
        (Fixture.root / "missing-image" / "assets" / "원격 사진.png").unlink()
        (Fixture.root / "invalid-scene" / "scene.element").write_text("scene {object broken {unsupported:1;}}", encoding="utf-8")
        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Fixture)
        server.daemon_threads = True
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        address = f"link://127.0.0.1:{server.server_port}/fixture"
        checks = 0
        try:
            def probe(resource: str, budget: int = 4096, *, ok: bool = True, cancel: int | None = None):
                nonlocal checks
                command = [str(args.probe.resolve()), address, resource, str(budget)]
                if cancel is not None:
                    command.append(str(cancel))
                started = time.monotonic()
                result = subprocess.run(command, capture_output=True, encoding="utf-8", errors="replace", timeout=8)
                elapsed = time.monotonic() - started
                assert (result.returncode == 0) == ok, (resource, result.returncode, result.stdout, result.stderr)
                checks += 1
                return result, elapsed
            result, _ = probe("binary")
            assert "hex=00ff804c494e4b00" in result.stdout, result.stdout
            result, _ = probe("assets/원격 사진.png")
            assert f"bytes={len(png())}" in result.stdout
            for path in ("missing", "redirect", "oversize", "chunked", "truncated"):
                probe(path, 16, ok=False)
            for path in ("../world.link", "/world.link", "assets/%2e%2e/secret", "assets\\secret"):
                probe(path, ok=False)
            for _ in range(10):
                result, elapsed = probe("slow", cancel=100, ok=False)
                assert "cancel" in result.stderr.lower(), result.stderr
                assert elapsed < 1.5, f"Cancellation took {elapsed:.3f}s"
            result = subprocess.run([str(args.loader.resolve()), "--integration", address, str(args.worlds.resolve())], capture_output=True, encoding="utf-8", errors="replace", timeout=15)
            assert result.returncode == 0, (result.stdout, result.stderr)
            assert Fixture.counts.get("/missing-target/world.link", 0) == 0, "Portal target fetched before entry"
            print(result.stdout.strip())
            for broken, expected in (("missing-image", "404"), ("invalid-scene", "unsupported field")):
                broken_address = f"link://127.0.0.1:{server.server_port}/{broken}"
                result = subprocess.run([str(args.loader.resolve()), "--reject", broken_address, str(args.worlds.resolve()), expected], capture_output=True, encoding="utf-8", errors="replace", timeout=15)
                assert result.returncode == 0, (broken, result.stdout, result.stderr)
                print(result.stdout.strip())
            print(f"PASS WinHTTP integration: {checks} real requests/probes, binary+Unicode, HTTP failures, byte limits, repeated cancellation, remote snapshots")
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=3)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())