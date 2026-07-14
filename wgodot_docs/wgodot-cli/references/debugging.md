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
godot --wg debug continue # or debug resume
godot --wg debug step_into
godot --wg debug step_over
godot --wg debug step_out
```

`debug resume` is an alias for `debug continue`. Both wait for confirmed resumption. Each step waits until the next debugger break and reports its new top frame. These commands fail if the game is not hard-paused or the current break cannot continue.

## Calls that reach breakpoints

`call` and `call_static` normally wait for completion but return early if the invoked execution reaches a hard debugger break. The response reports `completed: false`, the break reason, and the top frame. The method remains suspended and continues after `debug resume`; its eventual result is discarded because the original CLI request has already returned.

Use `--wait-through-breakpoint` only when another terminal or user will control the debugger. It preserves the original request and eventual return value, with a 60-second completion window. Use `--detach` to return immediately after dispatch when method validation, call errors, and the return value are not needed. These two options cannot be combined.

Wait for a running game to reach a breakpoint without polling:

```powershell
godot --wg debug wait
godot --wg debug wait --timeout 30
```

The timeout is in seconds, defaults to `15`, and accepts `1` through `60`. If the game is already stopped at a breakpoint, `debug wait` returns the current state immediately.

Use `--session <id>` only for intentional multi-instance debugging. All debug commands accept `--json`.

## Stack frames and scoped variables

While the game is stopped at a hard debugger break, inspect the cached call stack:

```powershell
godot --wg debug stack
```

Frame indices are zero-based. Frame `0` is selected automatically at every new break. Select another frame, or display the current selection without changing it:

```powershell
godot --wg debug frame 2
godot --wg debug frame
```

Inspect variables from the selected frame:

```powershell
godot --wg debug locals
godot --wg debug members
godot --wg debug globals
godot --wg debug vars
```

`debug vars` prints all three scopes. Use `--frame` with a scoped-variable command to select and inspect another frame in one request:

```powershell
godot --wg debug locals --frame 2
godot --wg debug vars --frame 2
```

`debug members` shows members declared by the selected frame's script and uses their declared types even when their current value is `null`. Untyped members are displayed as `Variant`.

Inspect the live object stored in a member, or continue through a dot-separated property path:

```powershell
godot --wg debug members player_credits
godot --wg debug members player_credits.wallet.owner
godot --wg debug members self.player_credits
```

The path is resolved from the selected frame's live `self`; the explicit `self.` prefix is optional. Every object hop and the final member list use fresh remote values. Resolving properties may invoke their getters. `--frame`, `--all`, and `--exclude-builtin` apply to the final object:

```powershell
godot --wg debug members player_credits --frame 2
godot --wg debug members player_credits --all --exclude-builtin
```

Filter final-object fields by declared type, including named-class and native inheritance, or by a case-insensitive field-name substring. Both filters may be combined with each other and with `--all` or `--exclude-builtin`:

```powershell
godot --wg debug members current_screen --all --filter-type Node2D
godot --wg debug members current_screen --all --exclude-builtin --filter-name _sandbox
godot --wg debug members current_screen --filter-type Node2D --filter-name element
```

Normal scoped-variable output does not expand arrays. It displays `Array(size=N)` or the corresponding packed-array type and length. Strings longer than 128 characters are displayed as a quoted 64-character preview followed by `... (+N chars more)`.

An explicitly targeted array or string is treated as a terminal value and printed in full instead of requiring an Object:

```powershell
godot --wg debug members current_screen.loading_light_points_particles
godot --wg debug members current_screen.long_description
```

Other final targets must be live Objects. If a segment is `null`, WGodot reports its declared type and suggests using `list <Type>` to inspect metadata without a live instance.

Include inherited GDScript members and native properties from the paused `self` object with `--all`:

```powershell
godot --wg debug members --all
```

Filter those results by where each field was declared, rather than by its value type:

```powershell
godot --wg debug members --all --exclude-builtin
```

`--exclude-builtin` removes properties declared by Godot's native classes. A user-declared field remains included even when its declared type, such as `String`, is built in. Therefore, `--all --exclude-builtin` shows members from the selected script and its GDScript base classes without native properties. Without `--all`, `--exclude-builtin` is allowed but has no additional effect because ordinary `debug members` is already limited to the selected script.

The stack is cached only for the current break, and a new break selects frame `0` again. Scoped-variable commands request fresh values every time they run, including the remote values used by `members --all`; their temporary response snapshot is discarded after printing. This allows changes made while hard-paused to appear in the next inspection without command-specific cache invalidation.

When the main game thread reaches a hard breakpoint, gameplay, physics, and rendering effectively stop. The debugger message loop remains responsive, which allows these commands to operate. Other game threads are not guaranteed to be suspended.
