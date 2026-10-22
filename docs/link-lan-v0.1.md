# LINK Protocol · LAN setup v0.1

LINK Browser connects directly to the reference server using **LINK TCP v0.1**. This guide covers addresses, Windows startup, portals, and connection checks. The [binary wire contract](link-protocol-v0.1.md) defines framing, handshake, resource requests, and error handling. [LINK World Standards v0.1](link-world-v0.1.md) still defines Element, Script, and Style.

This experimental profile targets trusted documents and a trusted network. It is plaintext TCP: there is no TLS, server authentication, shared multiplayer state, DNS/IPv6 support, or OS `link://` handler registration. HTTP is not used by the current browser.

## Addresses

~~~text
link://192.168.0.10/hello-world
link://192.168.0.10:8000/hello-world
link://127.0.0.1:8001/afterlight
~~~

The form is `link://<IPv4>[:port]/<world>`. The scheme must be lowercase `link://`. IPv4 consists of exactly four decimal octets from 0 through 255, with no leading zeroes in multi-digit octets. The optional decimal port is 1–65535; its default is **8000**. The whole remote address is at most 2,048 bytes. The browser preserves the entered LINK address.

The world name is one nonempty segment containing ASCII letters, digits, and hyphens after one percent-decoding pass. Letter case is preserved. DNS names, IPv6, userinfo, queries, fragments, trailing slashes, extra segments, malformed escapes, encoded separators, and nested escapes are rejected. `link://local/<world>` is reserved for the browser's [local worlds root](link-local-v0.1.md); `local:8000` is invalid.

The browser opens a TCP connection to the address and selects the world through `HELLO` / `WELCOME`. It requests `world.link`, its Element, Style, and listed Script files, then referenced images through `GET` / `RESOURCE`. One connection serves the entire preparation. Reload and portal navigation open new sessions. A conventional HTTP server cannot serve these native LINK messages, and there is no HTTP fallback.

## World resources and loading

Document and image references stay inside their own world directory. Authoring references are percent-decoded once; absolute paths, `..` components, drive/UNC forms, backslashes, control characters, `:`, `?`, `#`, `@`, residual percent escapes, and encoded separators are rejected. `./` is allowed in documents and normalized away on the wire.

The browser sends canonical decoded UTF-8 relative paths, including spaces and Unicode filenames, without HTTP URL encoding. The server resolves each request under the selected world and allows only supported resource types. Downloaded files go into a unique temporary snapshot. Reserved or unsafe Windows filename aliases are rejected. A resource cannot select another world or external URL. Script binding strings must still exactly match entries in the manifest.

| Resource or operation | Limit |
| --- | ---: |
| `world.link` | 64 KiB |
| Each Element / Style document | 1 MiB |
| Each Script file / Script list | 128 KiB / 16 files |
| Each image / unique image references | 16 MiB / 128 |
| Resource bytes per world load / session | 64 MiB |
| `GET` requests per session | 256 |
| Browser network operation | 20-second deadline |
| Server complete-message read | 10-second deadline |
| Reference server concurrent connections | 32 |

Frame headers and actual received bytes are checked against limits. Repeated references to the same staged file reuse that file within one snapshot. All document and runtime budgets in the [language contract](../native/docs/language.md) also apply. Source documents use UTF-8 without a BOM; supported image extensions are jpg, jpeg, png, and bmp.

Loading observes cancellation during bounded network waits, file writes, parsing, and validation. Cancellation or failure closes the socket and removes an unused candidate snapshot. Only the latest validated navigation candidate may activate. Failed or cancelled preparation keeps the current world. A later activation failure can leave an error state after the previous runtime has been released.

The immutable `World` owns its local snapshot lifetime, so downloaded image files remain available while rendering. Releasing the final owner removes the snapshot. The network session ends after preparation and is not held by the active runtime. Reload creates a fresh snapshot and resets Script state. There is no persistent offline cache, and separate `GET` requests do not guarantee an atomic view when an author changes server files during loading.

## Portals

| Target | Meaning |
| --- | --- |
| `/second-world` from `link://192.168.0.10:8000/hello-world` | Same server and port: `link://192.168.0.10:8000/second-world` |
| `/hello-world` from `link://local/heukryong` | `link://local/hello-world` |
| `link://192.168.0.20:8001/other-world` | Explicit remote server |
| `link://local/room-a` | The visiting browser's local `room-a`, including from a remote world |

Relative targets inherit the current LINK authority, including its effective port. Remote targets are checked syntactically when declared and fetched only on entry. Local targets require an existing directory within the registered local worlds root. Declaring a portal does not read or execute its destination documents. Entering it starts a new preparation and, for remote destinations, a new LINK session.

The bundled photo corridor uses `/hello-world`, so hosting both worlds on one server preserves that server's authority.

## Run the server and browser on Windows

Keep the independent repositories as siblings, for example:

~~~text
LINK/
  link/          # LINK Browser source
  link-server/   # Python server and sample worlds
~~~

**Update both repositories.** Previous HTTP-based browser builds and server defaults use a different transport. From the browser repository, build the current browser:

~~~powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-native.ps1 -Test
~~~

The helper finds Visual Studio's CMake and regenerates stale paths after a checkout moves. Use `-Fresh -Test` for a fresh configuration. Browser tool requirements are in the [README](../README.md#run-link-browser).

On the server PC, install Python 3.10+ and start from `link-server`:

~~~powershell
.\LINK_SERVER_START.cmd
# Equivalent defaults: native LINK TCP, port 8000
python .\link_server.py --host 0.0.0.0 --port 8000
# Publish an explicit parent directory of worlds:
python .\link_server.py --host 0.0.0.0 --port 8000 --worlds "C:\LINK\worlds"
~~~

The default worlds root is `worlds` next to `link_server.py`, independent of the working directory. An explicit relative `--worlds` path resolves from the command's working directory. Only that configured root is published. The server checks canonical paths, resource types, and world escapes through links or junctions. It does not provide uploads or directory listings. Stop it with **Ctrl+C**.

Choose the actual Ethernet or Wi-Fi IPv4 address from the startup output or `ipconfig`. VPN and virtual adapters may also appear. `0.0.0.0` means listen on all IPv4 interfaces; `127.0.0.1` reaches only the same computer. Replace the example IP below with the server's actual address.

On another PC on the same trusted network, enter the address in LINK Browser or run from the browser repository:

~~~powershell
.\build\native\Release\LINK.exe --open link://192.168.0.10:8000/hello-world
~~~

The server's `hello-world` sample includes an image and an interactive Script; its `/second-world` portal stays on that server. **F5** fetches fresh files. Script runs separately in each visiting browser; visitors do not share clicks, object state, or player positions.

To host the browser's included worlds from a sibling `link-server` checkout:

~~~powershell
python .\link_server.py --worlds "..\link\native\worlds" --port 8000
~~~

Then open `link://192.168.0.10:8000/afterlight` or `link://192.168.0.10:8000/heukryong`. Keep all referenced assets inside each world and follow the [photo asset terms](../ASSET_LICENSE.md) when distributing the photo demo.

## Connection checks

Confirm the actual IPv4 interface, matching port, running server, network reachability, and any Wi-Fi client isolation. First test the port on the visiting PC:

~~~powershell
Test-NetConnection 192.168.0.10 -Port 8000
~~~

`TcpTestSucceeded: True` confirms a TCP connection can be established. It does not confirm that the service speaks LINK or that a world exists. Verify that startup identifies the native LINK transport; an older HTTP browser or `--transport http` server cannot complete the native handshake. A web browser or `curl http://...` is not a native LINK connection test. The server logs and LINK Browser's error message give the next failure point.

The server does not change Windows Firewall. If needed, an administrator can manually allow TCP 8000 only from the local subnet in an elevated PowerShell:

~~~powershell
New-NetFirewallRule -Name "LINK-LAN-TCP-8000" -DisplayName "LINK LAN TCP 8000 (LocalSubnet)" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8000 -RemoteAddress LocalSubnet -Profile Any
~~~

Match the port if the server uses another value. To remove that rule:

~~~powershell
Remove-NetFirewallRule -Name "LINK-LAN-TCP-8000"
~~~

## Legacy HTTP compatibility

The reference server retains an **explicit compatibility mode for older HTTP-based LINK Browser builds**:

~~~powershell
python .\link_server.py --transport http --host 0.0.0.0 --port 8001
~~~

In that mode an older browser maps `link://192.168.0.10:8001/hello-world` to HTTP `GET /hello-world/world.link` and relative resource requests. It is a separate startup mode; the same listener does not detect or negotiate both protocols. The current native browser never uses HTTP and cannot connect to that endpoint. To use the current browser, stop legacy mode and start the server with its default LINK transport.

## Trust boundary and future profiles

Plain TCP provides no confidentiality, authenticated server identity, or integrity against network attackers. LINK framing and budgets limit accepted inputs; a custom protocol does not make an unencrypted connection secure.

The reference server serves trusted static world files. Its path checks are not a handle-based filesystem sandbox against concurrent filesystem changes. The native browser's parsers, image decoder, and renderer have no process isolation. Script has no file, network, OS, or shell API, but constrained APIs and execution budgets do not constitute a production process sandbox.

Use trusted authors and servers. TLS, authentication and permissions, DNS/IPv6 and discovery, verified resource integrity, stronger execution isolation, and shared state remain future work. The [TCP v0.1 contract](link-protocol-v0.1.md) is a starting point for an open ecosystem, not a claim of Internet production readiness.
