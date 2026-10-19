# Resource codecs

`modules/wgodot/codecs/` owns custom text grammars and native resource construction. `WResourceCodec` registers a resource type, its owned properties/assignments, text reader/writer and C++ writer. The text loader, dependency scanner, saver and gd2cpp dispatch through that registry; resource-specific cases belong in the codec, not the generic exporter.

## WAnimation / WTracks v2

`WAnimation` inherits `Animation` and uses its actual key structs, storage and playback. It works inside ordinary `.tres` and `.tscn` files. Standard `Animation` serialization is unchanged.

```text
[sub_resource type="WAnimation" id="motion"]
resource_name = "example"
length = 1.0
tracks = WTracks(
    2, "Skeleton3D:",
    {},
    [
        [0, 1],
    ],
    [
        (2, ".Root", 1, 0, c(0, 0, 0, 1)),
        (1, ".Root", 1, 0, [(0, 0, 0), (1, 0, 0)]),
        (3, ".Root", 1, 0, d),
    ]
)
```

Arguments are version, path prefix, scalar declarations, clocks and track records. A record is `(type, path, interpolation, clock_index, payload[, options])`. Track and clock order are preserved. Each clock and each track stays on one line, including its numeric arrays.

The prefix is textual path compression, not a scene root: `.Root` above expands to `Skeleton3D:Root`, still relative to the AnimationMixer's root. An explicit `NodePath("../Other:property")` preserves a literal leading dot.

| Type | Payload components / key type |
| --- | --- |
| 0: value | Declared property type; `ValueKey` holds a genuine Godot `Variant` |
| 1: position_3d | x, y, z; `VectorKey` |
| 2: rotation_3d | x, y, z, w; `RotationKey` |
| 3: scale_3d | x, y, z; `VectorKey` |
| 4: blend_shape | weight; `ScalarKey` |
| 5: method | `{ "method": name, "args": [...] }`; `MethodKey` |
| 6: bezier | value, in_x, in_y, out_x, out_y; `BezierKey` |
| 7: audio | stream reference, optional start/end offsets; `AudioKey` |
| 8: animation | animation name; `AnimationKey` |

Type/interpolation numbers are Godot's enums; interpolation 5 is WGodot's normalized linear rotation interpolation. Options preserve `flags` (disabled=1, imported=2, no loop wrap=4), `update`, `audio_blend`, `transitions` and Bezier `handles`.

`c(value)` repeats one value at every time in the selected clock. `d` uses the transform/blend-shape default: zero position/weight, identity quaternion or unit scale. Neither removes keys or changes timing. Other payloads are arrays of scalars or component tuples. Property tracks declare `bool`, `int`, `float`, `string`, `vec2`, `vec3`, `vec4`, `quat`, `color`, `transform3d`, `vector2_array`, `resource` or `variant` once; for example `float[0.0, 1.0]` or `vec3(1, 2, 3)`.

Numeric clocks and transform components default to float32. `f64[...]` clocks and `f64` transform payloads preserve wider native data. Property values retain their Variant precision/type; audio offsets retain double precision. Non-finite components are rejected. Compressed Animation tracks must be decompressed before writing WTracks. V1 remains readable; writers emit v2.

Scalar declarations use `{c_1 = -0.7071068, ...}` with base36 IDs. Writers alias repeated numeric literals only when references plus the declaration save bytes; integers/bools in generic Variant payloads retain their types. Existing profitable IDs survive edits. Decoding copies each referenced value into its own key: editing one occurrence cannot mutate another. Editor-only save hints preserve IDs through duplication/cache loads; playback never looks up scalar IDs.

## Loading and native export

In an editor build, WGodot's existing text-resource cache stores a disposable compressed binary representation under `.godot/resource_cache/`. Valid cache hits bypass text decoding. Source changes, cache revision changes or a missing cache require text decoding again. This happens on resource loading, not only during the editor's import command. Authored `.tres`/`.tscn` files remain plaintext.

gd2cpp resolves scalar aliases during export. Its animation codec emits `LocalVector<WAnimation::RotationKey>` (and the other actual key types), direct `.time` / `.value` / `.transition` assignments, `append_track()` and `set_keyframes(..., std::move(keys))`. No text parsing, scalar-ID lookup, per-key reflection or intermediate numeric Variant arrays are needed to construct these tracks. Genuine property/method argument Variants remain as required by Godot's own key types.

## Opt-in diagnostics

Detailed decode timing/counters are compiled only with `TOOLS_ENABLED && DEBUG_ENABLED` and disabled by default, including unoptimized debug editors. Set `WGODOT_ANIMATION_CODEC_TRACE=1` before launching the editor to emit one `ANIMATION_CODEC` report per decode. Disabled tracing does not collect counters, read timers or write reports. `wg perf` animation regions collect only during an explicitly started capture.

`WResourceLoadMeasurement.read_text(path, disk_cache)` is an explicitly invoked debug API: it measures the real text loader with fresh resources and optionally enables the disk cache. `compare_animations()` checks decoded settings/keys; callers should additionally inspect track-specific settings when checking non-transform tracks. Benchmark with tracing disabled and separate text decoding, cache loading, teardown and playback timings.
