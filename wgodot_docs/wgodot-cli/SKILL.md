---
name: wgodot-cli
description: Use WGodot's agent-oriented command-line interface to work with a WGodot project, query a running editor and game session, and check project GDScript. Use when an agent needs to run `godot --wg` commands while developing, inspecting, or testing a Godot project.
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

`status` does not start the editor or game. If it reports that no editor was found, ensure a WGodot editor is open on the same project. If it reports that no game is running, start the project in that editor before using commands that operate on the running game.

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
3. Prefer normal human-readable output for interactive work and `--json` when reliable parsing is useful.
4. Inspect the command's output and exit code before continuing.
5. Use `godot --wg help` when a requested operation is not documented here.

Rely on automatic project and game-session selection unless the task genuinely involves multiple projects or game instances.
