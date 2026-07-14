---
name: wgodot-cli
description: Use WGodot's agent-oriented command-line interface to run, stop, pause, step, frame-sync, set breakpoints, or control a hard debugger pause; inspect logs, debugger errors, scene trees, class members, source declarations, and runtime or named-class static properties; modify properties; call runtime or static methods; semantically rename GDScript symbols; capture screenshots; inject input; query editor sessions; and check project GDScript. Use when an agent needs `godot --wg` commands while developing, inspecting, debugging, refactoring, or testing a WGodot project.
---

# WGodot CLI

Use WGodot CLI commands directly from a project directory or any directory below it. Do not run `status` before every command; it is a diagnostic command, not an initialization step.

Run this to see the commands supported by the installed WGodot build:

```powershell
godot --wg help
```

Treat that output as authoritative when it contains commands newer than this skill.

## Command form

Place normal Godot options before `--wg`, then place the WGodot command and its arguments after it:

```powershell
godot [Godot options] --wg <command> [arguments]
```

Examples:

```powershell
godot --wg status
godot --path E:\projects\my_game --wg status
```

Usually omit `--path`. WGodot searches upward from the current directory for `project.godot`, so commands work from the project root and its subdirectories.

## Editor status

Check whether the matching WGodot editor is running and see its active game sessions:

```powershell
godot --wg status
```

Use structured output when the result needs to be parsed:

```powershell
godot --wg status --json
```

WGodot normally chooses the editor-selected active game session automatically. Do not pass a session for ordinary single-game development. Select one explicitly only for intentional multi-instance testing:

```powershell
godot --wg status --session 1
```

Commands that operate on a running game also accept `--session <id>`.

`status` does not start the editor or game. If it reports that no editor was found, ensure a WGodot editor is open on the same project. If it reports that no game is running, start the project in that editor before using commands that operate on the running game.

## Run and stop

Run the project's main scene and wait for its debugger session to become ready:

```powershell
godot --wg run
```

Run the scene currently open in the editor:

```powershell
godot --wg run --current
```

Run a specific scene:

```powershell
godot --wg run res://levels/test_level.tscn
```

Stop the running game:

```powershell
godot --wg stop
```

These commands also support `--json` when their results need to be parsed.

## Logs and errors

Read recent Output-dock messages and structured Debugger errors together:

```powershell
godot --wg logs
godot --wg logs --level "error, warning"
godot --wg logs --source debugger --tail 20
```

Output and Debugger results stay in separate sections. Output levels are `standard` (alias: `info`), `warning`, `error`, and `editor`. Debugger entries contain runtime error or warning details, source locations, sessions, and stack frames. Repeat `--source` or `--level`, or use comma-separated values. Sources are `output`, `debugger`, and `all`; both sources are included by default. `--tail` defaults to the newest 100 matching entries per source.

Use `--session <id>` to restrict Debugger errors during intentional multi-instance testing. Use `--json` for structured entries. Logs remain available after the game stops.

Clear stale buffers before a focused test run:

```powershell
godot --wg clear_logs
godot --wg clear_logs --source output
godot --wg clear_logs --source debugger --session 1
```

`clear_logs` defaults to both sources and clears the corresponding editor UI buffers as well. It does not delete rotated log files such as `user://logs/godot.log`.

## Breakpoints and hard debugging

Use breakpoint CRUD, hard debugger pausing, stepping, stack frames, scoped variables, and wait commands as described in [references/debugging.md](references/debugging.md). Prefer `debug wait` over polling when waiting for a breakpoint.

## Pause and step

Pause scheduled process and physics work while keeping rendering and WGodot inspection commands available:

```powershell
godot --wg pause
godot --wg resume
```

Advance normal process frames while the full game pause remains active:

```powershell
godot --wg step
godot --wg step --count 3
```

Pause only fixed physics ticks. Normal process frames and rendering continue:

```powershell
godot --wg pause_physics
godot --wg step_physics
godot --wg step_physics --count 3
godot --wg resume_physics
```

`step` requires `pause`. It advances `_process`, idle timers, and idle tweens without advancing physics. `step_physics` requires either `pause` or `pause_physics`; it advances complete fixed ticks, including `_physics_process`, physics timers and tweens, navigation, and the 2D/3D physics servers.

Step commands wait until the requested count has completed. Use a positive integer for `--count`; its default is `1`.

`resume` clears the full game pause but preserves an explicit `pause_physics`. `resume_physics` clears only the explicit physics pause, so a full game pause still keeps physics stopped. These commands also accept `--json` and `--session <id>`.

Wait for naturally running process frames after an action:

```powershell
godot --wg wait
godot --wg wait --count 3
```

Wait for fixed physics ticks instead:

```powershell
godot --wg wait --physics
godot --wg wait --physics --count 3
```

`wait` observes normal execution and defaults to one process frame. It does not advance paused work: use `step` during a full pause and `step_physics` while physics is paused. The command returns after the requested frames or ticks and the corresponding rendered frame have completed.

## Scene tree

Print the running game's scene tree:

```powershell
godot --wg tree
```

Use filters for large projects instead of repeatedly dumping the complete tree:

```powershell
godot --wg tree --include Control
godot --wg tree --include Control --include Node3D
godot --wg tree --include "Control, Node3D" --exclude Button
godot --wg tree --exclude "Timer, AnimationPlayer"
godot --wg tree --include Button --property text
godot --wg tree --include ButtonElement --property "element_text, position"
godot --wg tree --include ButtonElement --property "position.x"
godot --wg tree --depth 3
godot --wg tree --root /root/Main/UI
godot --wg tree --include Control --root /root/Main/UI --json
```

- `--include <type>` includes that type and all types that inherit it. Its default value is `*`, which includes every type.
- `--exclude <type>` excludes that type and all types that inherit it. Exclusions are applied after inclusions and take precedence.
- Repeat `--include` or `--exclude`, or pass a comma-separated list, to match several types. `*` means every type for either option.
- Use native Godot class names or registered global script class names. The command reports unknown type names instead of silently returning an empty tree.
- `--property <name>` appends a runtime property value to each matching node. Use dots for nested access, such as `position.x` or `state.player.stats.health`. Nesting may continue through objects, dictionaries, and built-in value fields. The argument is repeatable, and comma-separated property names are also accepted.
- `--depth <number>` limits traversal depth relative to the selected root.
- `--root <node-path>` inspects only that runtime subtree.
- `--json` returns node paths, names, types, IDs, visibility, child counts, and scene paths as structured data.

Requested properties appear in argument order. Missing properties are displayed as `<missing>` and marked with `valid: false` in JSON output. Strings are quoted, while vectors and other values use compact Godot-style text.

The displayed node type prefers the nearest named script class, including a GDScript `class_name` or named inner class. It falls back to the native Godot class, such as `Node2D`, when the node's script inheritance chain has no named class.

Prefer `--include Control`, `--root`, or a shallow `--depth` when looking for a UI element. Use an exact node path returned by this command in later commands that accept node targets.

## Member listing

List the members of an exact runtime node, a registered GDScript `class_name`, or a native Godot class:

```powershell
godot --wg list /root/Main/UI/PlayButton
godot --wg list GameStatics
godot --wg list Node2D
```

Inspect a script directly even when it has no registered `class_name`:

```powershell
godot --wg list res://src/client/game_client.gd
```

Inspect one function by appending its name to a class or resolved property target:

```powershell
godot --wg list GameClient.get_tree
godot --wg list GameClient.restart_game
godot --wg list GameStatics.current_game_client.restart_game
```

Function output shows the complete signature. Native Godot and Variant methods are prefixed with `(built-in)`. GDScript methods show their absolute source-file path and definition line range when parser metadata is available. Bodyless interface functions are supported; their range covers the declaration.

Follow a registered class's member and any further nested properties:

```powershell
godot --wg list GameStatics.current_game_client
godot --wg list GameStatics.current_game_client.player.inventory
```

The first dotted segment must be a registered GDScript `class_name`. Each following segment is resolved as a static or ordinary property, matching the nesting behavior of `get_static` when a live value exists.

`list` prefers the live running game's value. If no game is running, or a nested value cannot be resolved live, it follows declared property types from script and native-class metadata instead. This allows type inspection without starting the game and allows instance-property targets such as:

```powershell
godot --wg list GameClient.auto_translate_mode
```

Metadata fallback can resolve script classes, native classes, built-in Variant types, and named enums. Runtime node paths such as `/root/Main` still require a running game because they identify instances rather than declared types.

Hide native Godot members when only project-defined GDScript members matter:

```powershell
godot --wg list GameClient --exclude-builtin
godot --wg list res://src/client/game_client.gd --exclude-builtin
```

Without `--exclude-builtin`, class and Object listings include inherited native members. With it, WGodot keeps members declared by the target script and its GDScript base classes, including interface functions, but omits native engine properties, methods, signals, constants, and enums. An explicitly targeted built-in function can still be inspected.

Restrict the result by declared type. Object types include members declared as an inheriting type, so `Node2D` also matches a variable declared as a named class that extends `Node2D`:

```powershell
godot --wg list GameClient --filter-type Node2D
```

Restrict the member categories with `--member-type`:

```powershell
godot --wg list GameClient --member-type func
godot --wg list GameClient --member-type "var, static_var"
godot --wg list GameClient --member-type signal --member-type static_func
```

`--member-type` is case-insensitive and repeatable, and accepts comma-separated values. Its primary values are `func`, `static_func`, `signal`, `var`, `static_var`, `const`, `enum`, and `class`. `function` aliases `func`; long forms such as `static_function`, `variable`, `static_variable`, `constant`, `enumeration`, and `nested_class` are also accepted.

For variables, `--filter-type` checks the declared value type. For functions it checks the return type, for constants it checks the stored value type, and for nested classes it checks the base type. Signals have the type `Signal`. Use `--member-type` instead when the goal is to select a category rather than a value type.

The first output line describes the resolved type, including its `class_name` or local-class status, script file, and direct base type when available. The remaining output combines inherited members rather than separating them by inheritance level or export group. It shows function and signal signatures, static and instance members in separate sections, constant values, enum member counts, and nested class base types. Empty sections are omitted. Nested classes and enums are summarized at one level; their inner contents are not recursively listed. Add `--json` for the declaration, `live` or `metadata` resolution source, structured sections, and member records.

After resolving its target, `list` reads reflection metadata and does not read every property value or invoke methods. Resolving a dotted target reads that property chain and may therefore invoke getters on the chain. It works while the game is paused.

## Runtime properties and methods

Read one or several properties from an exact runtime node path:

```powershell
godot --wg get /root/Main player.health
godot --wg get /root/Main player.health position.x visible
```

Assign flat or nested properties. The command prints the actual value read back after assignment:

```powershell
godot --wg set /root/Main player.health 100
godot --wg set /root/Main position.x 250
godot --wg set /root/Main position "Vector2(250, 400)"
godot --wg set /root/Main title "Hello world"
```

Call a node method with optional arguments, or call a method on a nested Object property:

```powershell
godot --wg call /root/Main restart_game
godot --wg call /root/Main spawn_enemy 3 "Vector2(250, 400)"
godot --wg call /root/Main inventory.add_item potion 3
```

Calls normally wait for either method completion or a hard debugger break. If the invoked code reaches a breakpoint, the command returns the paused frame immediately so the debugger can be inspected, while the method remains suspended until `godot --wg debug resume`.

Keep the original CLI request blocked through debugger breaks when another terminal or user will resume execution:

```powershell
godot --wg call /root/Main restart_game --wait-through-breakpoint
```

Wait-through mode allows up to 60 seconds for completion. Dispatch without waiting for validation, errors, or a return value only when that tradeoff is intentional:

```powershell
godot --wg call /root/Main restart_game --detach
```

Values use Godot Variant syntax when valid, including `true`, `null`, numbers, arrays, dictionaries, `Vector2(...)`, `Color(...)`, and `NodePath(...)`. Otherwise, the argument is treated as a String. Quote an argument at the shell level when it contains spaces. To pass a String that itself looks like Variant syntax, preserve quotes inside the argument; in PowerShell, for example:

```powershell
godot --wg set /root/Main text '"true"'
```

Use `--` before an operand beginning with `--` so it is not parsed as a CLI option. These commands work while the game is paused and accept `--json` for typed, structured results.

### Named-class static members

Use a complete class-qualified path whose first segment is a registered GDScript `class_name`:

```powershell
godot --wg get_static GameStatics.current_moving_element.element_text
godot --wg get_static GameStatics.current_score OtherStatics.enabled
godot --wg set_static GameStatics.current_score 100
godot --wg call_static GameStatics.reset
godot --wg call_static GameStatics.spawn_enemy 3 "Vector2(250, 400)"
```

Continue nesting after the static member as needed. For example, `GameStatics.current_moving_element.element_text` reads the ordinary `element_text` field from the object stored in the static `current_moving_element` variable. A nested call such as `GameStatics.current_moving_element.activate` calls the normal method on that resolved object.

Use the same Variant value syntax, `--`, `--json`, and `--session` rules as `get`, `set`, and `call`. These commands also work while the game is paused.

`call_static` supports the same breakpoint-aware default, `--wait-through-breakpoint`, and `--detach` modes as `call`.

## Screenshots

Capture the running game's root viewport:

```powershell
godot --wg ss
```

The command prints the saved PNG path. Choose an output path when a predictable location is useful:

```powershell
godot --wg ss -o screenshots/current.png
godot --wg ss --output screenshots/current.png --json
```

Relative output paths are resolved from the CLI's current directory. `screenshot` is accepted as a long alias for `ss`.

## Observe

Capture a screenshot and scene tree together in one command:

```powershell
godot --wg observe --json
```

Use the same tree filters and screenshot output option when appropriate:

```powershell
godot --wg observe --include Control --root /root/Main/UI -o screenshots/ui.png --json
```

Prefer `observe` when both visual state and runtime node structure are needed after an action. Prefer `ss` or `tree` alone when only one result is needed.

## Input

Click the center of a runtime `Control` using an exact path from `tree`:

```powershell
godot --wg click /root/Main/UI/PlayButton
```

Click a window position when no suitable `Control` exists:

```powershell
godot --wg click 640 360
godot --wg click 640 360 --button right
godot --wg click /root/Main/UI/Item --double
```

`--button` accepts `left`, `right`, or `middle` and defaults to `left`. For a `Control`, WGodot injects native mouse input at its center and reports an error when it is hidden, ignores mouse input, or has no area.

For a non-`Control` node, WGodot calls a zero-argument `simulate_click()` method when the node implements one:

```gdscript
func simulate_click() -> void:
    # Run the same behavior as this object's custom input handling.
    activate()
```

Use this method as the compatibility hook for custom clickable `Node2D` or other non-`Control` objects. The click command reports an error when a non-`Control` target does not implement it. `--button` and `--double` are not passed to `simulate_click()`.

Prefer node paths because they avoid guessed coordinates.

Type text through native key events. Click or otherwise focus the intended text control first:

```powershell
godot --wg click /root/Main/UI/NameEdit
godot --wg type "Agent Name"
```

Send a logical key or key combination:

```powershell
godot --wg key Enter
godot --wg key "Ctrl+A"
godot --wg key Escape
```

Send an `InputMap` action:

```powershell
godot --wg action ui_accept
godot --wg action move_right --strength 0.5
```

A plain `key` or `action` sends a press followed by a release. Hold and release input explicitly when testing code that polls input state:

```powershell
godot --wg key W --down
godot --wg key W --up
godot --wg action move_right --down
godot --wg action move_right --up
```

These commands queue native Godot input but do not wait for a later rendered frame. Use `wait` when frame synchronization matters, then inspect the result with `observe`, `tree`, `get`, or `ss`. Add `--json` when structured acknowledgement or errors are useful.

## Source declarations and semantic rename

Resolve a registered class or qualified member to its declaration:

```powershell
godot --wg source_info GameClient
godot --wg source_info GameStatics.current_game_client
godot --wg source_info res://src/client/game_client.gd::restart_game
```

The result contains the exact script path and one-based line and column. Use `--json` when those coordinates will feed another tool.

Rename a GDScript declaration and all semantically resolved GDScript usages across the project:

```powershell
godot --wg rename GameStatics.current_game_client active_game_client
godot --wg rename GameClient.restart_game restart_current_game
godot --wg rename GameClient ActiveGameClient
```

The first dotted segment must be a registered `class_name`. Nested targets follow declared GDScript types, so each intermediate property needs enough type information to resolve its declaration. For an unnamed script, separate its member path with `::`:

```powershell
godot --wg rename res://src/client/main.gd::restart_game restart_current_game
```

For locals, parameters, ambiguous expressions, or any symbol already known by location, bypass qualified-name resolution with a one-based source position:

```powershell
godot --wg rename --at res://src/client/game_client.gd:124:18 active_player
```

Preview and fully validate a broad rename before writing:

```powershell
godot --wg rename GameStatics.current_game_client active_game_client --dry-run
godot --wg rename GameStatics.current_game_client active_game_client --json
```

`--dry-run` still performs semantic resolution and validates every proposed edit, then reports affected files and edit counts without changing them. A normal rename prepares every changed file first and replaces the set atomically with rollback on failure.

Save scripts open in Godot before renaming. WGodot refuses a rename while the Godot script editor has unsaved files, because disk edits could otherwise overwrite them. Also save files in external editors first; Godot cannot detect an unsaved VS Code buffer.

Rename operates only on semantic GDScript references in project `.gd` files. It deliberately does not rewrite string-based member names, dynamically constructed calls, scene/resource serialization, documentation text, or unrelated identifiers with the same spelling. Inspect those separately when the project intentionally uses reflective string references. The matching Godot editor must be open, but the game does not need to be running.

## GDScript check

After editing GDScript, scan all project `.gd` files for parser errors, analyzer errors, and active warnings:

```powershell
godot --wg check
```

Before validation, the matching editor scans for external filesystem changes and waits for discovery and imports to finish. This is the same editor filesystem path used after the window regains focus, so newly copied assets are imported, required `.uid` files are created, and GDScript class metadata is refreshed before checking.

The validation scan respects `.gdignore` directories. Treat a nonzero exit code as a failed check and address reported errors before continuing. The matching WGodot editor must be open; the game does not need to be running.

## Agent workflow

1. Work from the target project directory or one of its subdirectories.
2. Run the command needed for the task directly; do not use `status` as a mandatory first step.
3. Use `run` when the game is not already running, then issue game commands directly.
4. Prefer node-targeted `click` over coordinate clicks, then use `type`, `key`, or `action` for the intended interaction.
5. Use `wait` after input when the game needs a frame to react, then use `observe` for a combined visual and structural check or `get` for exact runtime state.
6. Prefer normal human-readable output for interactive work and `--json` when reliable parsing is useful.
7. Inspect the command's output and exit code before continuing.
8. Use `godot --wg help` when a requested operation is not documented here.

Rely on automatic project and game-session selection unless the task genuinely involves multiple projects or game instances.
