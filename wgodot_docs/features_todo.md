# WGodot Feature TODO

This is the rough planned order for larger GDScript / WGodot language features.
We may implement smaller unrelated features between these items as needed.

WGodot strictness features should default to enabled. This fork is meant for projects
that want stricter correctness and a clearer path toward native-friendly code.

## `@native`

Add a small, practical first version of `@native`.

Near-term scope:

- Allow selected scripts/classes/functions to opt into native-friendly restrictions.
- Reuse strict typing and interface information where available.
- Start with validation and metadata before trying full native code generation.

Long-term goal:

- Build toward something like IL2CPP for GDScript: a future GDScript2Native pipeline.
- Eventually, a project should be able to toggle a setting and compile its entire GDScript codebase to native code.
- This is a very long-term goal; the first `@native` feature should stay small and doable.
