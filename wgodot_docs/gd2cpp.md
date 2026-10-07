# gd2cpp

What this is not: This is not a simple drop-in replacement for official godot/gdscript.

Once you port your entire project from official godot to wgodot, you will get lots of analyzer errors.

However, solving them is easy, all you have to do is keep your gdscript code strongly typed (see [strict-type-checking](./features.md#strict-type-checking)).


## How it works

Basically this is a transpiler that translates your gdscript code into sane c++, and that c++ code then should be used as a "main game" module and compiled with the engine. Then you can use that as the "custom template" to finalize your game with its resources (such as scenes, textures, etc).

So your entire game project will be split into two parts:
  - Engine AND your translated code -> compiled into machine code
  - resources: as .pck file (which can be embedded into your executable as well. however, there is no guarantee that they won't be decompiled/reverse-engineered)

## Client/server targets

`wg export-cpp <absolute-directory> --target client|server` selects ownership before native generation. Default: `client`. Server export requires `res://wgodot_targets.cfg`; projects without it retain shared ownership for all content.

```ini
[paths]
res://src/client/="client"
res://src/server/="server"
res://assets/ui/*="client"
res://maps/*/private/*="server"
res://src/server/public_?.gd="shared"

[client]
application/run/main_scene="res://src/client/main.tscn"

[server]
application/run/main_scene="res://src/server/main.tscn"
application/config/icon=""
```

Rules are case-sensitive, evaluated in file order; **last matching rule wins**. Unmatched paths are shared. A trailing `/` means everything below that directory. Godot wildcards apply: `*` matches any sequence including `/`; `?` matches one character except `.`. `**` has no separate meaning. Mark external assets as well as scripts/scenes. Imported payloads and sidecars follow their source's ownership. The policy file is never packaged.

For a subtree, set String metadata `wgodot_target` to `client` or `server` on its root. Descendants follow exclusion of their parent. Example scene property: `metadata/wgodot_target = "server"`. Declare ownership where the node is created; inherited-node overrides cannot change it. Whole-scene ownership belongs in `[paths]`. Keep shared collision/navigation separate from presentation subtrees. Filtering works on serialized scene state without running node constructors or scripts.

Excluded scripts, autoloads, nodes and embedded resources are omitted. Retained cross-target resource references, serialized node references, signal connections crossing the boundary and animation tracks into removed subtrees fail export. References constructed dynamically by game code cannot be proven safe by this filter; keep them typed/authored. Runtime `is_server()` branches do not remove code.

`[client]` and `[server]` override existing project settings for packaging. Autoload ownership follows its script/scene path. These sections are build inputs, not a place for secrets. Match the export preset's `wgodot/native_target` to the generation target and set `wgodot/native_module` to its output directory. Regenerate and rebuild after project/target settings change; export checks the target, input hashes and template identity. This selects content; engine feature/build profiles are configured separately.

Windows build scripts share `get_native_game_build.ps1 -GameName <name> -GameTarget client|server`: pass its `ModuleDirectory` to `wg export-cpp` with the matching `--target`, then run `build_wgodot.ps1 -Game -GameName <name> -GameTarget client|server`. Layout: `generated/<name>/<target>/main_game`; SCons suffix: `<name>.<target>.game`, covering objects, libraries and executables. Omitting the game name omits that path/suffix component; target defaults to client. The build rejects a mismatched manifest before invoking SCons. Regenerate old unsplit modules into this layout; keep staged templates/manifests and packaged outputs separate by target too. `-Release` selects release templates; optimization remains `none` unless `-Optimize` is supplied.

## Editor-only declarations

`@editor_only` excludes an entire script class, inner class, method, field/property, constant, signal or enum from **both native targets**. It remains available to GDScript in the editor. Put the annotation before `class_name`/`extends` to exclude a whole script, or immediately before a class member:

```gdscript
@editor_only
class_name PreviewTools
extends RefCounted
```

```gdscript
extends Node3D

@editor_only
@export var preview_label: String = "Authoring only"

@editor_only
func rebuild_preview() -> void:
    print(self.preview_label)
```

gd2cpp removes declarations before native analysis; references from retained code fail export, including references that could otherwise fall back to a same-named base member. Serialized editor-only field values are removed from scenes/resources before collecting dependencies. A retained scene attaching an editor-only class or connecting to an editor-only signal/method is an invalid runtime dependency. This annotation does not automatically remove unmarked standalone assets or replace `@tool`.

