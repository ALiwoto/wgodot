# WGodot Features

1. `@override`: allows you to override a function, if gdscript/wgodot/strict_override_checking is true, the engine will force you to use it.

2. `@private`: won't allow the usage of var/func outside of the current class.

3. `@protected`: won't allow the usage of var/func outside of the current class and its children.

4. `@readonly`: only allows setting the variable in-place, or if the variable is a class-field, only in class initializer.

5. `@static_class`: will mark the class as static, it will prevent usage of non-static or non-const members and initializers.

6. Added new option `debug/gdscript/wgodot/disable_embedded_gdscript` (true by default): embedding gdscript source inside of .tres files is considered bad practice and makes code readability extremely hard.

7. Fixed a bug where gdscript source code were automatically getting embedded into a .tres file.

8. Added Strict Signal / Callable Checking: Added new options `debug/gdscript/wgodot/strict_override_checking` and `debug/gdscript/wgodot/strict_signal_callable_checking` (both default to true); It's a project-level strict validation of obvious signal/callable connections

9. de-const: controlled by `debug/gdscript/wgodot/deconst_exports` (true by default). For example, if you declare:

```gd
const MAX_ENEMY_HEALTH = 100

# later on:

self.check_health(MAX_ENEMY_HEALTH)
```

in gdscript code, then export it, and decompile the code, you should not see `MAX_ENEMY_HEALTH` and should ONLY see:

```gd
self.check_health(100)
```

10. `@no_mangle`: marks a named declaration as something export-time transforms must not rename, remove, or rewrite. On constants, it protects only that constant. On classes, functions, and properties with getter/setter bodies, it protects declarations inside that subtree from name obfuscation and from removing constants declared inside that subtree. Usages inside the subtree still follow the declaration they reference; for example, a use of an outside `@private` field is renamed if that outside field declaration is renamed.

11. Name obfuscation: controlled by `debug/gdscript/wgodot/obfuscate_names` (true by default). During export, function parameters, local variables, `for` iterators, match-pattern binds, `@private` field/property/method names, and explicit `@obfuscate` field/property/method names are renamed before text, binary-token, and compressed-binary-token script export. Local variables marked `@no_mangle` are not renamed, and `@no_mangle` function/class/property scopes are skipped recursively. Combining `@no_mangle` with `@private` or `@obfuscate` opts that declaration out of export-time name obfuscation.

12. `@obfuscate`: marks a function or variable/property declaration for export-time name obfuscation without applying the access restrictions of `@private`. This is explicit and does not try to rewrite string-reflection calls such as `get("member_name")`; use `@no_mangle` if a dynamically accessed member must keep its original name.

13. Export annotation stripping: wgodot strips `@private`, `@no_mangle`, and `@obfuscate` annotations from exported GDScript after they have been used by export-time transforms, so exported code does not keep those reverse-engineering hints.

14. Obfuscation strategy: controlled by `debug/gdscript/wgodot/obfuscation_strategy` (`Short`, `Hash`, `Unicode`). The setting is exposed as an enum, but only `Short` is implemented right now; selecting `Hash` or `Unicode` falls back to `Short` for now.

## Annotation documentation

WGodot-specific GDScript annotations are registered in `modules/gdscript/wgodot_annotations.cpp`. Their editor-visible help text is documented in `modules/gdscript/doc_classes/@GDScript_wgodot.xml`, which is merged into the built-in `@GDScript` docs by the wgodot duplicate-doc merge hook in `editor/doc/doc_tools.cpp`.

When adding or changing a wgodot annotation, update both places so code completion, editor help, and the language server show useful explanations instead of only listing the annotation name.
