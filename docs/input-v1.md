# Experimental input-v1 · semantic world input

`input-v1` lets a LINK world receive a browser input action without reading raw keyboard state or device handles. Add it to `world.link`:

```text
extensions: ["input-v1"];
```

A game using shared server state can declare both `"shared-state-v1"` and `"input-v1"`. Existing v0.1 documents and LINK resource transfers keep their current format. This profile is experimental and currently defines one action, `menu`.

## Script event

Attach a Script to an Object or Component and add `on onInput(ctx,event)`. For a status panel, the following Script starts hidden and toggles its subtree:

```text
on onStart(ctx) {
  store statusOpen = false;
  ctx.setTransform({visible:false});
}
on onInput(ctx,event) {
  if (event.action == "menu" && event.phase == "pressed") {
    statusOpen = !statusOpen;
    ctx.setTransform({visible:statusOpen});
  }
}
```

| Field | Current values |
| --- | --- |
| `event.action` | `"menu"` |
| `event.phase` | `"pressed"` |
| `event.source` | `"keyboard"` or `"controller"` |

The Windows browser maps **Tab** in its world viewport and the Quest Touch controller's **left Y button** to `menu`. These inputs are press events: holding the key or button must not repeatedly toggle a window. Game menu input requires application focus, head tracking and an active button action. Controller position tracking is independent: losing the aim pose alone does not disable a valid menu button. Releasing and pressing again is required after lost focus or an inactive button action. Device bindings depend on the active interaction profile. A controller without a menu binding can use the desktop key.

Each active Script binding with an `onInput` handler receives the same event, including bindings whose visual subtree is hidden. This permits a hidden panel to reopen itself. World authors decide which component handles `menu`; do not attach independent menu toggles to every object. Ordinary `onUpdate` calls do not repeat the event. A world without `input-v1`, an unstarted runtime, and bindings without a handler receive no event. A failed handler is disabled under the same rules as other lifecycle hooks.

## Browser bridge and limits

The native browser calls `Runtime::input("menu","keyboard")` or `Runtime::input("menu","controller")` once per accepted press. It returns whether an active `onInput` handler received the event. Unsupported action or source strings return false. An event delivered to a handler that faults still counts as delivered; that handler is then disabled until the world is reloaded.

Each input callback has a 65,536-step allowance, and all callbacks for one input event share a 262,144-step allowance. Existing Script storage, state, call and API budgets still apply. Errors retain their node, Script binding and `onInput` lifecycle diagnostics; other active bindings continue within the remaining event allowance.

The event contains semantic intent only. It includes no raw key stream, session token or network handle. Dispatching it does not send a server action automatically. Opening a local panel does not require a server round trip; its displayed HP, equipment and progression can still come from `ctx.getSharedState()`. This profile does not expose hand joints or implement gesture recognition.
