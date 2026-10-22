# LINK Protocol · TCP v0.1

This document defines the experimental binary protocol used directly between LINK Browser and `link-server`. A remote `link://<IPv4>[:port]/<world>` address opens one TCP connection, selects a world, and requests its resources through LINK messages. **There is no HTTP request, HTTP negotiation, or HTTP fallback.**

The wire version and the world-document version are separate fields. Both are currently **0.1**. Element, Script, and Style retain the [LINK World Standards v0.1](link-world-v0.1.md) grammar. This is a prototype contract, not an established Internet standard.

For Windows startup, addresses, portals, and firewall checks, see the [LAN setup guide](link-lan-v0.1.md). `link://local/...` retains its [local filesystem meaning](link-local-v0.1.md).

## Session

1. Connect to the numeric IPv4 and port in the address; the default port is **8000**.
2. Send `HELLO` with request ID `0`, world-document version `0.1`, and the selected world name.
3. Receive `WELCOME` with ID `0` and exactly the same payload, or an `ERROR` followed by connection closure.
4. Send `GET` with ID `1` for `world.link`. Request the referenced Element, Style, Script, and image files with consecutive IDs.
5. For each `GET`, receive one `RESOURCE` with the same ID, or one `ERROR` followed by closure. Finish each exchange before sending the next request.
6. Send an empty `GOODBYE` with the next request ID and close. The server does not reply to `GOODBYE`.

One connection serves one world load. The selected world cannot change within that connection. Reload and portal navigation create new connections and repeat the handshake, including when the destination uses the same server. The connection is closed after preparation; it does not carry frame updates, player positions, or shared Script state. A client may also close early to cancel.

## Frame header

All integer fields use **big-endian network byte order**. Every frame starts with this **16-byte** header, followed by exactly `payloadLength` bytes. TCP is a byte stream: a header or payload may arrive in several reads, and several frames may arrive in one read.

| Byte offset | Size | Field | v0.1 value |
| --- | ---: | --- | --- |
| 0 | 4 | Magic | ASCII `LINK`; hexadecimal `4c 49 4e 4b` |
| 4 | 1 | Wire major version | `0` |
| 5 | 1 | Wire minor version | `1` |
| 6 | 1 | Message type | See below |
| 7 | 1 | Flags | `0`; other values are invalid |
| 8 | 4 | Request ID | Unsigned 32-bit integer |
| 12 | 4 | Payload length | Unsigned 32-bit byte count |

Python's equivalent layout is `struct.Struct("!4sBBBBII")`. There is no terminating NUL, line ending, padding, compression, or implicit string length inside a frame.

Type-specific payload bounds are HELLO/WELCOME **3–2,050 bytes**, GET **5–2,052 bytes**, RESOURCE **0 through the requested resource cap**, ERROR **2–1,026 bytes**, and GOODBYE **0 bytes**. These bounds apply before payload allocation. A resource's request cap never exceeds 16 MiB.

The receiver validates magic, version, flags, direction, request ID, and the type-specific length **before allocating or reading the payload**. Truncated frames, unexpected response types, mismatched IDs, and mismatched handshakes fail the load. There is no resynchronization after malformed framing.

## Messages

| Type | Value | Direction | Payload |
| --- | ---: | --- | --- |
| `HELLO` | 1 | Browser → server | Two world-version bytes followed by an ASCII world name |
| `WELCOME` | 2 | Server → browser | Exact `HELLO` payload echoed |
| `GET` | 3 | Browser → server | Unsigned 32-bit maximum response size, then a UTF-8 relative path |
| `RESOURCE` | 4 | Server → browser | Raw resource bytes |
| `ERROR` | 5 | Server → browser | Unsigned 16-bit code, then a UTF-8 explanation |
| `GOODBYE` | 6 | Browser → server | Empty |

### HELLO and WELCOME

The payload is **3–2,050 bytes**. The first two payload bytes are the requested **world-document** major and minor versions: `00 01`. All remaining bytes are the world name, such as `hello-world`. It contains one or more ASCII letters, digits, or hyphens, preserving case; no slash, percent encoding, NUL, or surrounding whitespace is allowed.

The server validates the requested version and that the world exists inside its configured world root before replying. The browser requires `WELCOME` to echo both the version and the world name exactly. A successful handshake selects the resource namespace; it does not authenticate the server.

### GET and RESOURCE

`GET` starts with a 4-byte unsigned response limit from **1 byte through 16 MiB**. The rest is a **1–2,048-byte** UTF-8 path relative to the selected world's directory, for example `world.link` or `assets/흑룡이1.jpg`.

The browser decodes and validates document references before sending them. Wire paths are canonical decoded paths: `/` separates nonempty components; `.` and `..` components, percent escapes, leading/trailing slashes, backslashes, drive/UNC forms, control characters, `:`, `?`, `#`, and `@` are forbidden. An authored `./assets/photo.png` reference therefore becomes `assets/photo.png` on the wire. Unicode names and spaces inside a filename are UTF-8, without HTTP URL encoding. Reserved or ambiguous Windows filename aliases are rejected as well.

The server confines the resolved regular file to the selected world, applies its allowed resource types and size limits, and returns its raw bytes. `RESOURCE` has no MIME header or filename prefix: request ID identifies the request. Empty resources can have a zero-byte payload; normal document validation still applies.

The response must fit both the client-supplied limit and the applicable file budget. A client must reject an excessive declared length before allocating its body, and verify all received bytes. Only one request may be outstanding; v0.1 has no pipelining, chunks within a resource, compression, cache validators, push, or priority messages.

### ERROR

The payload begins with a 2-byte code followed by up to **1,024 bytes of UTF-8 explanation**. The explanation must be valid UTF-8 without ASCII control characters (bytes 0–31 or 127). It is diagnostic text, not executable content or a machine-readable subprotocol.

| Code | Name | Meaning |
| --- | --- | --- |
| 1 | BadRequest | Invalid message, order, field, or path syntax |
| 2 | NotFound | World or resource does not exist |
| 3 | Forbidden | Resource is outside the world or is not permitted |
| 4 | TooLarge | Requested resource exceeds a size limit |
| 5 | Version | Unsupported wire or world-document version |
| 6 | Busy | Server connection capacity reached |
| 7 | Internal | Server could not complete the operation |
| 8 | Limit | Session request or byte budget exhausted |

`ERROR` uses the offending request's ID, including ID `0` for handshake failures. A server may reject a connection as `Busy` with ID `0` before the handshake. After an error, it closes the connection; the browser does not retry through HTTP. An unparseable or abruptly truncated connection can close without an error frame.

### GOODBYE

`GOODBYE` has the next consecutive request ID and a zero-byte payload; after 256 successful GET requests, that ID is `257`. It is optional, terminal, and has no response. An early TCP close also ends the session; clients must never rely on a response to `GOODBYE`.

## Byte example

Opening `link://127.0.0.1:8000/hello-world` starts with the following bytes. Hexadecimal spaces and line breaks below are for reading only.

~~~text
HELLO header:
4c 49 4e 4b  00 01 01 00  00 00 00 00  00 00 00 0d
HELLO payload:
00 01 68 65 6c 6c 6f 2d 77 6f 72 6c 64
~~~

The response uses type `02` and echoes the same 13 payload bytes. The first resource request asks for `world.link` with a 64 KiB response cap:

~~~text
GET header:
4c 49 4e 4b  00 01 03 00  00 00 00 01  00 00 00 0e
GET payload:
00 01 00 00 77 6f 72 6c 64 2e 6c 69 6e 6b
~~~

A successful `RESOURCE` has type `04`, request ID `1`, a length equal to the file size, and the file bytes as its payload. A conventional HTTP server cannot answer these messages. Run the native LINK server, and update both repositories together.

## Budgets and deadlines

| Resource or operation | Limit |
| --- | ---: |
| Manifest `world.link` | 64 KiB |
| Each Element / Style document | 1 MiB |
| Each Script file / Script files per world | 128 KiB / 16 |
| Each image / unique image references | 16 MiB / 128 |
| Resource bytes per session / browser world load | 64 MiB |
| `GET` requests per session | 256 |
| Reference server concurrent connections | 32 |
| Wire path | 2,048 UTF-8 bytes |
| Server read deadline per complete message | 10 seconds |
| Browser deadline per network operation | 20 seconds |
| Browser cancellation polling interval | Up to 50 milliseconds during network waits |

The server's message-read deadline covers the whole header and payload, rather than restarting for each byte. Header limits are checked before payload allocation. Connection, read, and write failures terminate preparation and release the socket and staged files. Interpreter, node, text, and image-decoding budgets remain defined in the [language contract](../native/docs/language.md).

The browser prepares documents, scripts, and images in a unique temporary world snapshot, then parses and validates them before activation. Failed or cancelled preparation keeps the current world. Only the newest validated navigation candidate may activate. A successful preparation can still encounter a renderer activation error after the previous runtime is released.

A loaded world owns its staged files until its final owner is released. Reload creates a fresh snapshot and resets Script state. This is neither a persistent offline cache nor an atomic versioned snapshot of server files: an author editing files between `GET` requests can still produce an inconsistent candidate.

## Trust and compatibility

**TCP v0.1 is plaintext. It provides no encryption, authenticated server identity, or integrity against a network attacker.** A custom frame format does not improve those properties by itself. Use trusted documents and servers on a trusted network. TLS, authentication, resource manifests with verified integrity, DNS/IPv6, and stronger execution isolation require future protocol work.

The server's path validation limits published files to the configured root; it is not a handle-based filesystem sandbox against concurrent filesystem changes. Browser parsers, image decoders, and rendering run without process isolation. Script has no file, network, OS, or shell API, but limited APIs and budgets are not a production process sandbox.

The reference server defaults to this native LINK transport. Its explicit `--transport http` mode remains only for **older HTTP-based browsers**, as described in the [setup guide](link-lan-v0.1.md#legacy-http-compatibility). A native browser cannot open that legacy endpoint; an older HTTP browser cannot open a native LINK endpoint. There is no automatic transport detection or downgrade.
