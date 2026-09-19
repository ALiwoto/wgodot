# WGodot Features

This file lists most of the useful WGodot features, if you want to read their implementations, feel free to explore the codebase.

## GDScript Safety

It's highly recommended that you keep these features enabled for [gd2cpp](./gd2cpp.md).
Most of these features are turned on by default.

### @override

Option: `wgodot/gdscript/strict_override_checking`

When you override a parent method, you have to use this.

Addon option: `wgodot/gdscript/disable_strict_override_checking_for_addons` (default: true)
Use it to disable the option under `res://addons/` folder.

### @private

Limits a variable or function to the current class/file.

### @protected

Limits a variable or function to the current class and child classes.

### readonly

Forces a class field to either be assigned inline or at class constructor (_init).

### @static_class

Only allows static members inside of a class (consts are also allowed).
We highly recommend using this annotation for classes that only has static logic, it will improve the codegen in gd2cpp (e.g. the class won't be registered in godot's ClassDB system).

### Strict signal/callable checking

Option: `wgodot/gdscript/strict_signal_callable_checking`

Catches invalid signal/callable connections.
Mandatory for using `gd2cpp` feature.

### Strict type checking

Option: `wgodot/gdscript/strict_type_checking`

It rejects EVERYTHING that is not strongly typed.
for Array / Dictionary you **have** to use types on them: `Array[int]` or `Dictionary[String, int]`.

in gd2cpp these will get converted to [WArray<T>](/modules/wgodot/native/wgodot_native_warray.h) and [WDictionary<K, V>](/modules/wgodot/native/wgodot_native_wdictionary.h) (because original Godot ones will still store Variant under the hood, which compromises performance).

For most of the cases though all you have to do is to convert your vars from:
`var something = 123`
to
`var something := 123`


And instead of using `Object.call("method", ...)` and `Object.call_deferred("method", ...)`

use: `object.method.call(args)` or `object.method.call_deferred(args)`.

`JavaScriptObject.call` is exempt.

keywords of `is` and `as` are supposed. You can also use `or` between your ises but don't abuse them too much because I haven't stress tested them.

`wgodot/gdscript/disable_strict_type_checking_for_addons` defaults to `true` and disables these checks for every script under `res://addons/`. (for this one gd2cpp will generate c++, but will route all of them through ClassDB registration and reflection calling, which is extremely bad for performance. however, modifying plugins/addons is really hard, so yeah it's up to you).

**IMPORTANT**: This feature is strictly mandatory for gd2cpp.


### Embedded GDScript blocking

Option: `wgodot/gdscript/disable_embedded_gdscript`

Throws error if you embed gdscript into resources (like scenes).

There are a few reasons:
  - maintainability: scripts embedded inside of scenes or other resources are very hard to find/navigate.
  - gd2cpp: the entire performance assumption in gd2cpp is that your entire gdscript code is compiled into c++.

### Project GDScript check

`godot --wg check`: basically rescans everything and reports error to you. it's like when you open the editor and it throws lots of errors at you. I had to constantly reopen the editor while fixing bugs, so I made this option.

**UI Option**: The Output toolbar's **Re-analyze** button does the same thing, but flushes out the console first so fresh errors get thrown out.


### Checked tween property paths

strict type checking validates constant `Tween.tween_property()` arg, e.g. "modulate:a" is actually analyzed and included as pure strongly typed c++ in gd2cpp.

### Typed tree broadcasts

`SceneTree.call_group_as(NodeType, "method", ...)` iterates through all the nodes with NodeType in scene tree and calls the method on them

if you call a function in your args, they will only be called once.

notes:
  - invalid nodes (like the ones that are already freed or removed) are skipped.
  - newly added nodes are also not guaranteed to be included.
  - We recommend using "queue_*" methods, e.g. queue_redraw to not block the main thread for heavy operation.

gd2cpp flattens this entire call into a single loop iteration.

Strict-type-checking feature entirely rejects `call_group` and `call_group_flags` because they need too much reflections and I don't like reflections.


<hr />

## Interface support

Basically: interfaces define what signature members must be inside of a class. they are like a contract.
You can use the keyword `interface_name` (taken from `class_name`) to define a global interface.

(these used to be only supported inside of gdscript, but now they are fully supported in c++ too).
As you know, official godot has a "single-hierarchy" system (basically like C# where you can't inherit multiple class).
However, in wgodot, you can implement as many interfaces as you like (if they conflict you will get a hard error).
And in the generated c++ code, they will actually be generated as fully pure c++ inheritance! (but they won't be registered as parent in godot's type system)


definition example:

```gdscript
interface_name Destroyable

func destroy_me() -> void
```

`implements A, B` adds contracts without changing native inheritance.

Interfaces supports all kind of members (however, you can't have a default implementation in them, they have to be bodyless). vars and signals are also supported. All that matters in an interface decleration is the signature.

Type hints, `is`/`as` and all other type system also recognize interfaces.

gd2cpp also fully supports interfaces.

## Reusable UI

These used to be my custom UI framework stuff, I've ported them into the engine so I can use them everywhere.

The `wgodot_ui` module provides:
  - `FlatElement : Label`: a flat element that doesn't really do anything special, it's just there so I can cast it/use it with ElementBase interface
  - `ButtonElement : Button`:
  - `TextBoxElement : LineEdit`
  - `SmoothScrollElement : ScrollContainer`.

They are highly reusable and stuff.

Build with `module_wgodot_ui_enabled=no` to omit the module.

`SmoothScrollElement` supports an optional uniform virtual grid for performance reason.
Basically when you have wayyy too many objects in your list, you should use this virtual grid to constantly re-use the controls **only visible to the users**.

## Agent CLI

See the [WGodot CLI skill](./wgodot-cli/SKILL.md) for LLM agent stuff.

It supports many features for completely automating the editor from the CLI, humans can also use it easily.

Supports (conditional) breakpoints, pause/step, var inspection, method calling and many other things.


## Export Protection (A.K.A Obfuscation)

**IMPORTANT**: These features are for pre-gd2cpp era, many of these are no longer needed since gd2cpp has all of these features in itself.
They will soon get deprecated and removed, to remove the code maintainability costs.

### De-const/de-enum

Option: `wgodot/export/deconst_exports`

inlines constants where they are used. so in your exported binary you won't see their name. makes reverse engineering a bit harder.

### @no_mangle

will be removed soon due to gd2cpp.

### @no_string_mangle

keeps hardcoded strings instead of obfuscating it.
can to be used on the declaration site, e.g. use it on an entire func/class/etc.

### String obfuscation

Option: `wgodot/export/obfuscate_strings`

Obfuscates hardcoded strings and saves their real value into a file in resources.
This option won't probably be removed because it can still be used in gd2cpp, but recovering the strings will still be an easy task for an skilled reverse-engineer, so don't rely on it too much (e.g. don't store your API secret stuff in the game client only because this option obfuscates them, they can still dump the entire memory and read everything your game client has).

You can also exclude individual strings with `@no_string_mangle`.

### No-export source blocks

add `#wgodot::no_export::begin` and `#wgodot::no_export::end` to make code not get exported. It's basically like c++'s `#ifndef` thingy.

whitespace and multiple blocks per file are fine, nesting is unsupported.

### Export timing logs

Option: `wgodot/export/timing_logs_enabled`

Basically I had to make this feature back when I was hitting my wall to the head trying to figure out what stuff is taking so much time in export pipeline.

Emits UTC timings for slow operations.

Other options:

  - `wgodot/export/timing_verbose_logs_enabled`: adds more verbose logs.
  - `wgodot/export/timing_slow_threshold_msec` controls the slow-log threshold.

### Diagnostic redaction

Option: `wgodot/export/redact_diagnostics` (default `false`)

strips print functions with keywords like ERZ_123 etc.

**#TODO** This is not yet supported in gd2cpp.

Writes the original args to `<output-basename>.diagnostics.json` (keep it private).
This still need more work is not that stable.

## Annotation Documentation

  - `modules/gdscript/wgodot_annotations.cpp`: registration
  - `modules/gdscript/doc_classes/@GDScript_wgodot.xml`: documentation

## Core API Helpers

  - `StreamPeer.get_data_bytes(bytes_count)` returns `PackedByteArray`

## Startup Diagnosis

Option: `--wgodot-startup-profile`

enables startup timings (including export templates).

For Android, put it in the export preset's Extra Args.

Behavior:
  - operations that take less than 1 ms are not logged.
  - Profiling stops after the first frame
  - when the flag is not provided, it doesn't do any timing reads or logging
