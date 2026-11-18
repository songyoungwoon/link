# Experimental npc-travel-v1

This optional world extension observes server-owned NPCs across LINK addresses. It is an experimental reference profile, not a finalized federation standard. Existing resource framing, RPG sessions, local worlds and PC OpenXR controls are unchanged.

An NPC has a durable global ID. A server advances its route even when no browsers are connected. Entering an operator-configured portal freezes the source simulation durably and transfers bounded state to an allowlisted destination. Each world supplies its own approved model and presentation Script. No model path, executable Script or player credential is transferred with an NPC.

## World and presentation

Declare `extensions: ["models-v1", "npc-travel-v1"]`. `models-v1` is needed only when the authored NPC uses a GLB; a primitive presentation is also possible. `shared-state-v1` and `npc-travel-v1` are mutually exclusive session profiles in this implementation. The browser opens a separate persistent connection to the remote world's authority. `link://local/...` can preview the environment but has no live NPC service.

`ctx.getSharedState()` supplies the latest validated observation. Script maps `kind` to a predeclared local model slot, changes its transform and plays a locally available animation. Unknown kinds and animation names need an authored fallback. The demo presents one `liora` NPC with `Idle` / `Walk` clips; the transport permits up to 16 NPCs. It does not create arbitrary scene nodes. A missing NPC hides the slot. If the connection fails, the browser publishes an empty presence list and shows the connection error, avoiding a stale visible NPC.

The observation profile does not register a player, send head/controller positions, acquire or store bearer tokens, change the viewer camera, navigate automatically, or permit Script to transfer authority. Viewers can walk through the ordinary authored portal themselves. Multiple viewers receive the same server state independently.

## Observer wire

Use the existing 16-byte LINK 0.1 header with zero flags. Bodies are duplicate-key-free, finite UTF-8 JSON, at most 65,536 bytes. The connection uses these message types:

| Message | ID | Body |
| --- | --- | --- |
| JOIN (16) | 0 | `{ "version":1, "world":"traveler-a", "profile":"npc-travel-v1" }` |
| JOINED (17) | 0 | `{ "snapshot": SNAPSHOT }` |
| STEP (18) | Increasing from 1 | `{}` only; at most 20 Hz |
| STATE (19) | Matching STEP | `SNAPSHOT` |
| LEAVE (20) | Next request ID | Empty body |
| FAIL (21) | Matching request | Bounded generic error; closes the connection |

The snapshot has exactly these fields:

```json
{
  "tick": 120,
  "time": 6.0,
  "world": "traveler-a",
  "npcs": [{
    "id": "wayfarer-01",
    "kind": "liora",
    "name": "Liora",
    "position": [0, 0, 3],
    "yaw": 180,
    "animation": "Walk",
    "epoch": 1,
    "journey": 0
  }]
}
```

`world` must match JOIN. `tick` is a nonnegative safe integer and must not decrease during a connection; `time` is a nonnegative finite number. IDs are unique per snapshot. `id` (128 bytes) and `kind` (64 bytes) use ASCII letters, digits, underscore, dot, colon or hyphen. `name` and `animation` are nonempty printable UTF-8 strings, bounded to 128 and 64 bytes. Coordinates use meters and Y-up; position components and yaw in degrees are finite and bounded to ±1,000,000. `epoch` is an integer from 1 to 2^53−1; `journey` is an integer from 0 to 2^53−1. Both increase by one on a successful portal handoff. Epoch is an ownership generation, not a global timestamp.

## Authority handoff and failure semantics

The game project's `server/npc_server.py` is a separate reference service with a durable SQLite database per authority. Its configuration defines the authority, trusted peer addresses, world bounds, path, portal volume, arrival location and initial seed. A preexisting ID is never reseeded just because it has departed or returned. NPC progress is server-owned; observer requests cannot mutate it.

The source commits its outgoing state before sending a handoff. The destination validates peer authentication, destination, bounds, archetype and generation, then commits the incoming entity and a deduplication receipt together. Only then does it acknowledge. The source retries the same durable transfer ID and payload after a lost acknowledgement or restart. An identical receipt returns the prior acknowledgement without spawning again. A late acknowledgement cannot delete a newer generation that has already returned.

This provides durable idempotent application and a single active simulation under the configured trust model. It does not promise instantaneous simultaneous screen updates or continuous availability through a partition: a transfer can pause or temporarily disappear. A timed-out transfer never reactivates the old source copy. The reference service limits unresolved outboxes to 128 and receipts to 65,536; old receipt retries still work after the receipt limit, while new transfers fail closed. Durable outboxes and receipts must be retained for the supported replay/recovery window. Only one configured seed authority may create an ID; cloning an active database or seeding the same ID on two independent authorities violates this trust model. Restoring or deleting only one authority's database is outside this guarantee; back up and restore cooperating authorities consistently.

The reference transfer channel uses LINK frames and HMAC authentication with an operator-provisioned peer-group key and an explicit destination allowlist. It carries data only. It remains plaintext TCP: HMAC does not encrypt NPC state, authenticate public server certificates or protect observer traffic. This version targets trusted local/LAN peers. TLS, per-peer credentials and revocation, public federation, arbitrary routing, global discovery and portable autonomous AI state are future work.

## Try and verify

In the sibling `link-game` repository, start `LINK_GAME_NPC_START.cmd`, then `LINK_GAME_NPC_PLAY.cmd`. The default addresses are `link://127.0.0.1:8010/traveler-a` and `link://127.0.0.1:8011/traveler-b`. Liora follows a short authored route and repeatedly crosses the portal. This is deterministic route-following, not a pathfinding or AI behavior system.

See `link-game/docs/npc-travel.md` in a sibling checkout for deployment and recovery. The observer transport fixture checks malformed frames and state, replay/order, bounded cancellation, and absence of player credentials. The native probe can load the real authored world and execute its Script. `LINK --open link://127.0.0.1:8010/traveler-a --npc-smoke report.json` verifies live model presentation, departure, arrival at a different address, a return with the same ID and higher epoch, viewer camera independence and Home cleanup, and saves framebuffer captures. This check requires the two reference servers; it does not certify physical headset performance.
