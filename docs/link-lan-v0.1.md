# LINK Protocol · LAN HTTP v0.1

This is the canonical contract for LINK Browser's experimental LAN transport. It lets a browser fetch a world from another computer by its IPv4 address. [LINK World Standards v0.1](link-world-v0.1.md) still defines the documents, Element, Script, and Style. [Local addresses](link-local-v0.1.md) keep their filesystem meaning.

This profile targets trusted world authors and a trusted local network. It uses plain HTTP and does not provide DNS discovery, HTTPS, authentication, accounts, shared multiplayer state, or OS `link://` handler registration.

## Addresses and HTTP mapping

```text
link://192.168.0.10/hello-world
link://192.168.0.10:8000/hello-world
link://127.0.0.1:8001/hello-world
```

The form is `link://<IPv4>[:port]/<world>`. The scheme must be lowercase `link://`. IPv4 consists of exactly four decimal octets from 0 through 255, with no leading zeroes in multi-digit octets. The optional decimal port is 1–65535; its default is **8000**. The whole remote address is at most 2,048 bytes. The browser preserves the entered LINK address.

The world name is one nonempty segment containing ASCII letters, digits, and hyphens after one percent-decoding pass. Letter case is preserved. DNS names, IPv6, userinfo, queries, fragments, trailing slashes, extra segments, malformed escapes, encoded separators, and nested escapes are rejected. `link://local/<world>` is reserved for the browser's registered local root; `local:8000` is invalid.

The first request is:

```http
GET http://192.168.0.10:8000/hello-world/world.link
```

The browser then fetches the manifest's Element, Style, and listed Script files, followed by images referenced in the scene, all relative to `/hello-world/`. Each request must return **HTTP 200**; redirects fail. A supplied `Content-Length` must be valid, within the applicable budget, and match the received body. Cookies, HTTP authentication, and proxy use are disabled. Fresh navigation and reload request fresh bytes.

An ordinary static HTTP server can implement this resource mapping. LINK does not require a custom TCP protocol or the bundled Python server. The reference server also offers `HEAD`, a small `/` information page, and optional `/.well-known/link` metadata; the current browser does not require those endpoints.

## World resources and loading

Manifest references and scene images remain relative to their own world directory. Paths are percent-decoded once; absolute paths, `..` components, drive/UNC forms, backslashes, control characters, `:`, `?`, `#`, `@`, residual percent escapes, and encoded separators are rejected. `./` is allowed. The browser re-encodes validated UTF-8 path bytes for HTTP, including spaces and Unicode asset names.

Downloaded files go into a unique temporary world snapshot. The browser also rejects filenames that could alias reserved or unsafe Windows paths. Document references cannot select another world or an arbitrary external URL. Existing Script binding strings must still exactly match entries in the manifest.

| Resource | Limit |
| --- | ---: |
| `world.link` | 64 KiB |
| Each Element / Style document | 1 MiB |
| Each Script file / Script list | 128 KiB / 16 files |
| Each image / unique image references | 16 MiB / 128 |
| Total downloaded bytes per world load | 64 MiB |
| Each HTTP file transfer | 20-second deadline |

The browser checks actual received bytes, including when no length header is supplied. Repeated references to the same staged file reuse that file within one snapshot. All document and runtime budgets in the [language contract](../native/docs/language.md) also apply. Source documents use UTF-8 without a BOM; supported image extensions are jpg, jpeg, png, and bmp.

Loading observes cancellation during bounded HTTP waits, file writes, parsing, and validation. Only the latest validated navigation candidate may activate. A failed or cancelled preparation keeps the current world. After preparation succeeds, activation follows the existing world lifecycle; an activation failure can leave an error state after the previous runtime has been released.

The immutable `World` owns the snapshot lifetime, so downloaded image files remain available while the renderer uses that world. Releasing the final owner removes the snapshot. Reload creates another snapshot and resets Script state. This is a temporary per-load snapshot, not a persistent offline cache. Separate HTTP requests do not guarantee an atomic view if an author changes server files during loading.

## Portals

A portal target may be an absolute LINK address or a root-relative world address:

| Target | Meaning |
| --- | --- |
| `/second-world` from `link://192.168.0.10:8000/hello-world` | Same server and port: `link://192.168.0.10:8000/second-world` |
| `/hello-world` from `link://local/heukryong` | `link://local/hello-world` |
| `link://192.168.0.20:8001/other-world` | Explicit remote server |
| `link://local/room-a` | The visiting browser's local `room-a`, even from a remote world |

Relative targets inherit the current LINK authority, including the effective port. Remote targets are checked syntactically when their portal is declared; no target fetch occurs until entry. Local targets still require an existing directory within the registered local worlds root. Declaring a portal does not read or execute its destination documents. Entering it uses the same preparation, cancellation, validation, and activation rules as address-bar navigation.

The bundled photo corridor uses `/hello-world`, so hosting both worlds on one server preserves that server's authority.

## Run the server and browser on Windows

Keep the independent repositories as siblings, for example:

```text
LINK/
  link/          # LINK Browser source
  link-server/   # Python LAN server and sample worlds
```

Update both repositories and build the current browser. Earlier local-only browser builds cannot open remote LINK addresses. From the browser repository:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\build-native.ps1 -Test
```

The helper finds Visual Studio's CMake and regenerates stale paths after a checkout moves. Use `-Fresh -Test` when a fresh configuration is wanted. Browser tool requirements are in the [README](../README.md#run-link-browser).

On the server PC, install Python 3.10+ and start from `link-server`:

```powershell
.\LINK_SERVER_START.cmd
# Equivalent defaults:
python .\link_server.py --host 0.0.0.0 --port 8000
# Publish an explicit parent directory of worlds:
python .\link_server.py --host 0.0.0.0 --port 8000 --worlds "C:\LINK\worlds"
```

The default worlds root is the `worlds` directory next to `link_server.py`, independent of the working directory. An explicit relative `--worlds` path resolves from the command's working directory. Only that configured root is published. The server restricts resource types and canonical paths, rejects world escapes through links or junctions, and does not expose directory listings, uploads, repository source, or arbitrary filesystem files. It prints request logs and discovered IPv4 addresses. Stop it with **Ctrl+C**.

Choose the actual Ethernet or Wi-Fi IPv4 address from the startup output or `ipconfig`; VPN and virtual adapters may also appear. `0.0.0.0` means listen on all IPv4 interfaces, and `127.0.0.1` reaches only the same computer. The example address below is a placeholder, not a detected address.

On another PC connected to the same trusted network, enter the address in LINK Browser or run from the browser repository:

```powershell
.\build\native\Release\LINK.exe --open link://192.168.0.10:8000/hello-world
```

The server's `hello-world` sample includes an image and an interactive Script; its `/second-world` portal stays on that server. **F5** fetches fresh files. Script runs in each visiting browser; visitors do not share clicks, object state, or player positions.

To host the browser's included worlds from a sibling `link-server` checkout:

```powershell
python .\link_server.py --worlds "..\link\native\worlds" --port 8000
```

Then open `link://192.168.0.10:8000/heukryong`, replacing the example IP. Keep referenced assets inside each world and follow the [photo asset terms](../ASSET_LICENSE.md) when distributing the photo demo.

## Connection checks

If another PC cannot connect, confirm the chosen IPv4 interface, matching port, running server, network reachability, and Wi-Fi client isolation policy. Check Windows Firewall on the server PC. The server does not change firewall rules automatically.

If needed, an administrator can manually allow TCP 8000 only from the local subnet in an elevated PowerShell:

```powershell
New-NetFirewallRule -Name "LINK-LAN-TCP-8000" -DisplayName "LINK LAN TCP 8000 (LocalSubnet)" -Direction Inbound -Action Allow -Protocol TCP -LocalPort 8000 -RemoteAddress LocalSubnet -Profile Any
```

Match the port if the server uses another value. To remove that rule:

```powershell
Remove-NetFirewallRule -Name "LINK-LAN-TCP-8000"
```

## Trust boundary and future profiles

Plain HTTP provides no confidentiality, server identity, or integrity against a network attacker. The reference server serves trusted static world files; its path checks are not a handle-based filesystem sandbox against concurrent filesystem changes. The native browser's parsers, image decoder, and renderer have no process isolation.

Script has no file, network, OS, or shell API, but constrained APIs and execution budgets do not constitute a production process sandbox. Use trusted authors and servers for this profile. DNS and discovery, TLS, authentication and permissions, stronger execution isolation, and shared state remain future work. This implementation is a starting point for an open ecosystem and does not claim Internet production readiness.
