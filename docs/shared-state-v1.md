# Experimental shared-state-v1 · first cooperative game profile

This optional profile connects the LINK Browser runtime to the first small cooperative RPG server. It is an experimental, bounded game schema, not a general multiplayer Internet standard. Existing world documents remain version 0.1. Existing LINK resource sessions are unchanged.

A world opts in with `extensions: ["shared-state-v1"]`. The browser opens a separate persistent connection to the same remote authority and world. Local previews render but cannot join a shared server. Network work happens on an owned worker; the rendering thread reads the newest validated snapshot and never waits for socket I/O.

## Script bridge

| API | Contract |
| --- | --- |
| `ctx.getSharedState()` | Latest validated snapshot, or null before joining. No token or socket access. |
| `ctx.sendAction(kind,{target:ID})` | Queue a declared intent; returns false if disconnected or the 32-action queue is full. Damage, HP and rewards remain server-owned. |
| `ctx.getViewer()` | Local position, XYZ Euler rotation in degrees, forward vector, weaponPosition and weaponRotation. VR values follow tracked head/controller poses. |
| `ctx.setText(id,text)` | Replace an existing text plane, maximum 1,024 UTF-8 bytes. Unchanged text does no GPU work; changed text is coalesced and at most one plaque is rasterized/uploaded per frame. |
| `str(value)` | Format a value for labels; strings remain unquoted. |

`raycast:false` on a node excludes its subtree from selection rays. Held weapons and viewer-attached UI use this so they cannot block selecting the world. shared-state worlds may declare 32 text planes within the existing 150 MiB scene texture budget; ordinary worlds retain their 16-plane limit. The current renderer uses hierarchical transforms for character joints; skeletal-mesh import is not part of this profile.

## Session and wire

The 16-byte LINK header remains `>4sBBBBII`: magic LINK, wire 0.1, zero flags, big-endian request ID and payload length. JSON bodies are UTF8, finite and duplicate-key-free, at most 65,536 bytes. JOIN 16/id 0 contains version 1, world, optional resume token and display name. JOINED 17/id 0 returns a private token, playerId and initial snapshot. STEP 18 uses increasing IDs and the latest head position/yaw plus at most one action; STATE 19 replies with the matching ID. LEAVE 20 is empty; FAIL 21 returns a bounded generic error and closes. The worker sends at most 20 STEPs per second and coalesces obsolete poses.

The first game snapshot contains self, 8 player slots, 8 monsters, tick/time and a personal notice. Actions use attack/equip/rest/claim intents. See the game project's public operating guide for balance, limits, ownership and persistence policy. This is plaintext TCP with bearer credentials; it is intended for trusted LAN testing. Encryption, account recovery and a public-service authentication system are separate work.

Each client profile stores its token by authority/world in `%LOCALAPPDATA%/LINK/profiles/<profile>.sessions`. Tokens never enter Script or logs. `--profile alice --player-name Alice` and a separate `--profile bob --player-name Bob` allow two players on one PC. Advanced testing can override storage with `--profile-directory`; do not share profile files.

The server supplies the initial spawn and explicit respawn changes. The browser moves the desktop camera or VR rig to that location without changing the user's tracked head orientation or physical height. Arbitrary remote Script cannot change the camera through this API.

## Validation

`link_shared_runtime` checks Script state/actions/viewer/live-text behavior and budgets. `link_shared_transport` checks real socket framing, Unicode, malformed inputs, queue bounds, cancellation, two simultaneous clients and identity resumption using a disposable fixture. `native/tests/shared_integration.py --probe <probe> --server <sao/server/asterfall.py>` additionally starts a disposable real RPG server. Neither this test nor synthetic OpenXR validates headset performance or physical sword collision.
