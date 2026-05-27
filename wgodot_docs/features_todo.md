# WGodot Feature TODO

This is the rough planned order for larger GDScript / WGodot language features.
We may implement smaller unrelated features between these items as needed.

WGodot strictness features should default to enabled. This fork is meant for projects
that want stricter correctness and a clearer path toward native-friendly code.

## 1. Strict Signal / Callable Checking

Add project-level strict validation of obvious signal/callable connections. It should be
enabled by default, with a setting available for projects that need a temporary escape hatch.

Initial scope:

- Validate only static, analyzable cases.
- Compare known signal argument count/types against known callable signatures.
- Ignore dynamic or Variant-heavy cases instead of guessing.
- Avoid expensive flow analysis.

Examples to support:

```gdscript
button.pressed.connect(self._on_button_pressed)
some_signal.connect(_some_method)
some_signal.connect(func(a: int) -> void: pass)
```

Examples to skip in the first version:

```gdscript
some_signal.connect(callback_variable)
some_signal.connect(Callable(obj, method_name_string))
some_signal.connect(obj.get("callback"))
```

## 2. Interfaces

Add interface support so strict code can replace runtime checks such as `has_method()`.

Target shape:

```gdscript
interface_name Damageable

func take_damage(amount: int) -> int
```

```gdscript
class_name Enemy
extends CharacterBody2D
implements Damageable

func take_damage(amount: int) -> int:
	return amount
```

Important goals:

- Interfaces are compile-time contracts.
- Interface methods are abstract signatures.
- Start with methods only; avoid fields/properties at first.
- Classes should explicitly declare implemented interfaces.
- `if body is Damageable:` should narrow the type enough for safe calls.

## 3. Strict Type Checking

Add strict typing mode for projects that want native-friendly GDScript. It should default
to enabled in WGodot, with project settings for migration or compatibility if needed.

Initial goals:

- Reject untyped variables, parameters, and return values.
- Reject dynamic member access where the analyzer cannot prove the target.
- Reject unsafe Variant-heavy code in strict mode.
- Keep this standalone; strict signal checking and interfaces should be useful without requiring strict typing.

This mode is a foundation for future native compilation.

## 4. `@native`

Add a small, practical first version of `@native`.

Near-term scope:

- Allow selected scripts/classes/functions to opt into native-friendly restrictions.
- Reuse strict typing and interface information where available.
- Start with validation and metadata before trying full native code generation.

Long-term goal:

- Build toward something like IL2CPP for GDScript: a future GDScript2Native pipeline.
- Eventually, a project should be able to toggle a setting and compile its entire GDScript codebase to native code.
- This is a very long-term goal; the first `@native` feature should stay small and doable.
