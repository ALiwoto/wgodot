---
name: wgodot-cli
description: Use WGodot's agent-oriented command-line interface to run or stop a project, inspect the runtime scene tree, capture screenshots, inject mouse, keyboard, text, and InputMap action input, query editor sessions, and check project GDScript. Use when an agent needs `godot --wg` commands while developing, inspecting, or testing a WGodot project.
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

Commands that operate on a running game, including `tree`, `ss`, `observe`, `click`, `type`, `key`, and `action`, also accept `--session <id>`.

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

These commands queue native Godot input but do not wait for a later rendered frame. Inspect the result with a separate `observe`, `tree`, or `ss` command. Add `--json` when structured acknowledgement or errors are useful.

## GDScript check

After editing GDScript, scan all project `.gd` files for parser errors, analyzer errors, and active warnings:

```powershell
godot --wg check
```

The scan respects `.gdignore` directories. Treat a nonzero exit code as a failed check and address reported errors before continuing.

This command works without a running editor because it checks the project directly.

## Agent workflow

1. Work from the target project directory or one of its subdirectories.
2. Run the command needed for the task directly; do not use `status` as a mandatory first step.
3. Use `run` when the game is not already running, then issue game commands directly.
4. Prefer node-targeted `click` over coordinate clicks, then use `type`, `key`, or `action` for the intended interaction.
5. Use `observe` after input for a combined visual and structural check; use filtered `tree` output for large interfaces.
6. Prefer normal human-readable output for interactive work and `--json` when reliable parsing is useful.
7. Inspect the command's output and exit code before continuing.
8. Use `godot --wg help` when a requested operation is not documented here.

Rely on automatic project and game-session selection unless the task genuinely involves multiple projects or game instances.
