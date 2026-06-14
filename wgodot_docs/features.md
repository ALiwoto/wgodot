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

10. `@no_mangle`: when this is added on a constant, wgodot does not replace/change that constant during de-const export. The constant remains in exported source/bytecode metadata. Current scope is constants; functions and other symbols can be added later.
