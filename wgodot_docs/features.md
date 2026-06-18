# WGodot Features

This file tracks user-facing wgodot features. It intentionally avoids internal export-file and C++ implementation details.

## GDScript Safety

1. `@override`: marks a method as intentionally overriding a parent method. When `wgodot/gdscript/strict_override_checking` is enabled, overrides must use it.

2. `@private`: limits a variable or function to the current class/file.

3. `@protected`: limits a variable or function to the current class and child classes.

4. `@readonly`: allows a variable to be assigned only during initialization or in-place mutation.

5. `@static_class`: marks a class as static-only and rejects instance-style usage.

6. Strict signal/callable checking: `wgodot/gdscript/strict_signal_callable_checking` catches obvious invalid signal/callable connections.

7. Strict type checking: `wgodot/gdscript/strict_type_checking` rejects `Variant` declaration types, untyped `Array`/`Dictionary` element types, untyped function returns, and dynamic member/call/index access that cannot be resolved to a fully known non-`Variant` type.

8. Embedded GDScript blocking: `wgodot/gdscript/disable_embedded_gdscript` prevents exported resources from carrying embedded script source.

## Export Protection

9. De-const/de-enum: `wgodot/export/deconst_exports` removes exported constant and enum declarations, inlines their values where possible, and converts stripped enum type hints to `int`.

10. `@no_mangle`: keeps a declaration from being renamed or stripped by export transforms. On classes/functions/properties, it protects declarations inside that scope too.

11. `@obfuscate`: explicitly marks a declaration for configured export-time obfuscation without making it private. On a class, eligible members are obfuscated unless they use `@no_mangle`.

12. Name obfuscation: `wgodot/export/obfuscate_names` renames exported GDScript locals, parameters, private members, `@obfuscate` declarations, and obfuscated `class_name` entries.

13. Built-in/native name aliasing: `wgodot/export/obfuscate_builtin_names` aliases used engine/native class names, built-in types, built-in functions, and typed native/built-in methods/properties. Dynamic string reflection such as `get("name")`, `set("name", value)`, and `call("name")` is not rewritten.

14. Obfuscation strategy: `wgodot/export/obfuscation_strategy` exposes `Short`, `Hash`, and `Unicode`. Currently only `Short` is implemented.

15. Script path obfuscation: `@obfuscate_path` with `wgodot/export/obfuscate_file_paths` renames marked exported scripts using `wgodot/export/obfuscate_file_paths_strategy` (`Short`, `Hash`, or `Unicode`), rewrites exact `res://...` string literals that refer to those scripts, and updates exported global class paths without adding runtime remap files.

16. Export cleanup: exported GDScript strips wgodot annotations, normal comments, doc comments, and empty physical lines. Original project source files are not changed.

## Annotation Documentation

WGodot annotations are registered in `modules/gdscript/wgodot_annotations.cpp` and documented in `modules/gdscript/doc_classes/@GDScript_wgodot.xml` for editor help, completion, and language-server users.
