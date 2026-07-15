# WGodot Features

This file tracks user-facing wgodot features. It intentionally avoids internal export-file and C++ implementation details.

## GDScript Safety

1. `@override`: marks a method as intentionally overriding a parent method. When `wgodot/gdscript/strict_override_checking` is enabled, overrides must use it. `wgodot/gdscript/disable_strict_override_checking_for_addons` defaults to `true` and disables this requirement for every script under `res://addons/`.

2. `@private`: limits a variable or function to the current class/file.

3. `@protected`: limits a variable or function to the current class and child classes.

4. `@readonly`: allows a variable to be assigned only during initialization or in-place mutation.

5. `@static_class`: marks a class as static-only and rejects instance-style usage.

6. Strict signal/callable checking: `wgodot/gdscript/strict_signal_callable_checking` catches obvious invalid signal/callable connections.

7. Strict type checking: `wgodot/gdscript/strict_type_checking` rejects `Variant` declaration types, untyped `Array`/`Dictionary` element types, untyped function returns, and dynamic member/call/index access that cannot be resolved to a fully known non-`Variant` type. In `if` branches, strict checking also understands local/parameter narrowing from `is` tests, including `or` alternatives when the same identifier is narrowed to multiple possible types and the accessed property exists with the same non-`Variant` result type on every alternative. These checks are tooling-only and are disabled in non-tools runtime/template builds. `wgodot/gdscript/disable_strict_type_checking_for_addons` defaults to `true` and disables these checks for every script under `res://addons/`.

8. Embedded GDScript blocking: `wgodot/gdscript/disable_embedded_gdscript` prevents exported resources from carrying embedded script source.

9. Project GDScript CLI check: `godot --wg check` first asks the matching editor to scan external filesystem changes and finish imports, including required `.uid` creation and script-class metadata updates. When the editor has no unsaved buffers, it reloads externally changed open scripts. It always invalidates cached project parsers so changed class APIs and their transitive dependents are analyzed from current disk sources. It then scans all project `.gd` files under `res://`, respecting `.gdignore` directories, and prints parse errors, analyzer errors, and active GDScript warnings. The editor must be open, but the game need not be running. WGodot CLI searches upward from the current directory for `project.godot`; normal Godot options such as `--path` can be placed before `--wg`.

## Agent CLI

See the [WGodot CLI skill](./wgodot-cli/SKILL.md) for agent-oriented usage, commands, and workflow guidance.

The `list` command accepts native Godot classes and built-in Variant types such as `Vector3` and `Quaternion`. The `source_info` command returns editor-help signatures, documentation, and status notes for built-in Godot API symbols when no project source declaration exists.

While a WGodot full game pause is active, `godot --wg resume <node-path>` resumes idle processing and input only for the selected runtime node and its descendants. Fixed physics ticks and physics simulation remain paused. `godot --wg pause` clears the selected subtree, and `godot --wg resume` without a path resumes the full game.

WGodot breakpoints support unique names, live-frame conditions, and one-shot removal. Multiple logical breakpoints may share one physical source line; WGodot evaluates every enabled condition directly from the executing GDScript VM frame before entering Godot's hard-break loop, without requesting debugger/DAP scopes. It exposes all matching names and IDs, removes only matching one-shot records, skips the hard break entirely when none match, and pauses with explicit details when condition evaluation fails. Managed logical-breakpoint stops do not focus the game/editor or change the game's mouse mode. Breakpoint remove, enable, and disable actions accept either an ID or name.

## Export Protection

10. De-const/de-enum: `wgodot/export/deconst_exports` removes exported constant and enum declarations, inlines their values where possible, folds constant indexed uses to the indexed value, keeps dynamic indexed containers as parenthesized literals, and converts stripped enum type hints to `int`.

11. `@no_mangle`: keeps the annotated declaration from being renamed or stripped by export transforms. For de-const/de-enum, only `@no_mangle` on the constant or enum declaration itself prevents stripping; containing class/function/property `@no_mangle` does not stop usages from being inlined.

12. `@no_string_mangle`: keeps hardcoded strings inside the annotated script, class, or function from export-time string obfuscation. If a constant string is inlined elsewhere by de-const, the usage scope controls whether the inlined string is obfuscated.

13. `@obfuscate`: explicitly marks a declaration for configured export-time obfuscation without making it private. On a class, eligible members are obfuscated unless they use `@no_mangle`.

14. Name obfuscation: `wgodot/export/obfuscate_names` renames exported GDScript locals, parameters, private members including signals, `@obfuscate` declarations, and obfuscated `class_name` entries.

15. Built-in/native name aliasing: `wgodot/export/obfuscate_builtin_names` aliases used engine/native class names, built-in types, built-in functions, and typed native/built-in methods/properties. Dynamic string reflection such as `get("name")`, `set("name", value)`, and `call("name")` is not rewritten.

16. Obfuscation strategy: `wgodot/export/obfuscation_strategy` exposes `Short`, `Hash`, and `Unicode`. Currently only `Short` is implemented.

17. Script path obfuscation: `@obfuscate_path` with `wgodot/export/obfuscate_file_paths` renames marked exported scripts using `wgodot/export/obfuscate_file_paths_strategy` (`Short`, `Hash`, or `Unicode`), rewrites exact `res://...` string literals that refer to those scripts, and updates exported global class paths without adding runtime remap files.

18. String obfuscation: `wgodot/export/obfuscate_strings` replaces exported hardcoded `String`, `StringName`, and `NodePath` literals with resource-backed marker literals, folds constant string concatenations into one marker, and decodes the original values while parsing exported scripts. Dynamic reflection strings are decoded to their original text; declarations referenced through strings still need `@no_mangle` if name obfuscation would otherwise rename them.

19. Dead-code injection: `wgodot/export/dead_code_injection_enabled` injects embedded class-scope dead-code snippets before export transforms, controlled by `wgodot/export/min_in_class_dead_code_injection` and `wgodot/export/max_in_class_dead_code_injection`. `deadcode*.txt` snippets are used for normal classes, `static_deadcode*.txt` snippets are used for `@static_class` classes, and injected code is skipped for `@no_mangle` classes before being processed by the normal obfuscation/export cleanup passes.

20. No-export source blocks: full-line `#wgodot::no_export::begin` and `#wgodot::no_export::end` comment markers remove editor/debug-only GDScript blocks from exported scripts before export analysis and transforms. Leading whitespace before the marker is allowed, multiple blocks per file are allowed, and nested blocks are rejected with a warning.

21. Export cleanup: exported GDScript strips wgodot annotations, normal comments, doc comments, and empty physical lines. Original project source files are not changed.

22. Export timing logs: `wgodot/export/timing_logs_enabled` emits UTC-timestamped export timing summaries and slow transform breakdowns. `wgodot/export/timing_verbose_logs_enabled` adds high-frequency per-script checkpoints, and `wgodot/export/timing_slow_threshold_msec` controls the slow-log threshold.

## Annotation Documentation

WGodot annotations are registered in `modules/gdscript/wgodot_annotations.cpp` and documented in `modules/gdscript/doc_classes/@GDScript_wgodot.xml` for editor help, completion, and language-server users.

## Core API Helpers

- `StreamPeer.get_data_bytes(bytes)` returns the `PackedByteArray` payload from `get_data(bytes)` directly, avoiding untyped `Array` indexing in strict type checking.
