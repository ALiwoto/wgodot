# Breakpoints and hard debugging

## Breakpoints

Use one-based source lines in project GDScript files:

```powershell
godot --wg breakpoint add res://src/player.gd:42
godot --wg breakpoint list
godot --wg breakpoint disable 1
godot --wg breakpoint enable 1
godot --wg breakpoint remove 1
godot --wg breakpoint clear
```

Use `bp` as a shorter alias for `breakpoint`; for example, `godot --wg bp list` and `godot --wg bp add res://src/player.gd:42`.

`breakpoint add` prints the breakpoint ID. Adding the same location again returns its existing ID and enables it. IDs remain stable while that editor is open. Disabled breakpoints remain in WGodot's list even though Godot removes them from the active debugger internally.

Breakpoints can be configured before running the game and are sent to later debugger sessions. They are mirrored into Godot's script-editor and debugger UI. A breakpoint on a comment, blank line, or other non-executable line may never trigger.

Add `--json` to any breakpoint command for structured output.

## Debugger state and control

Inspect the active session:

```powershell
godot --wg debug state
```

Hard-pause a running game through its debugger:

```powershell
godot --wg debug pause
```

This differs from `godot --wg pause`. The ordinary `pause` command suspends scheduled process and physics phases while WGodot keeps controlling execution. `debug pause` stops at the current script-debugger location and exposes a call stack.

Resume or step from a hard debugger break:

```powershell
godot --wg debug continue
godot --wg debug step_into
godot --wg debug step_over
godot --wg debug step_out
```

Continue waits for confirmed resumption. Each step waits until the next debugger break and reports its new top frame. These commands fail if the game is not hard-paused or the current break cannot continue.

Wait for a running game to reach a breakpoint without polling:

```powershell
godot --wg debug wait
godot --wg debug wait --timeout 30
```

The timeout is in seconds, defaults to `15`, and accepts `1` through `60`. If the game is already stopped at a breakpoint, `debug wait` returns the current state immediately.

Use `--session <id>` only for intentional multi-instance debugging. All debug commands accept `--json`.

When the main game thread reaches a hard breakpoint, gameplay, physics, and rendering effectively stop. The debugger message loop remains responsive, which allows these commands to operate. Other game threads are not guaranteed to be suspended.
