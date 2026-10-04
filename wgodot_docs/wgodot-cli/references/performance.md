# Performance diagnosis

Run commands from the project directory with its WGodot editor open. Use `wg` instead of `godot --wg` when the project's wrapper supplies the executable and flag. Check installed `help` if a command is unavailable; source changes require a rebuilt editor and CLI executable.

All commands below accept `--session <id>`, `--json`, and `--output <path>`. Output files contain JSON even without `--json`; their parent directory must exist. Prefer project-local temporary report files. Commands use the normal editor-selected session. Profiler reports can also read retained data after the game exits; another run clears it.

## Script profiler

This reads and controls the same script/server Profiler as the editor UI. It does not scrape the screen or add game-side instrumentation. Starting a capture clears the previous capture; an already running profiler is rejected, so inspect/stop it first. UI-started captures are readable too.

For a bounded capture of the currently visible workload:

```powershell
godot --wg profile capture --frames 240 --timeout 15 --sort self --limit 40 --output profile.json
godot --wg profile report --sort inclusive --limit 40
godot --wg profile report --filter _process --sort calls --json
godot --wg profile frames --sort frame_time --limit 10
godot --wg profile report --view frame --frame 12345 --sort self
```

Use an actual frame number from `frames`. Function filtering is a case-insensitive substring of the signature/name, including script paths. A class name only matches if present in that signature/name; otherwise filter by its script filename.

For a specific interaction, bracket it with explicit start/stop:

```powershell
godot --wg profile start
# Perform the relevant interaction through the game or CLI.
godot --wg profile stop --sort self --output interaction.json
godot --wg profile status
godot --wg profile clear
```

`start` returns after sending the start request; it intentionally keeps profiling until `stop`. `capture` waits for at least the requested number of received frame records, then stops and waits for the runtime's accumulated totals. A timeout stops the capture and returns partial totals if they arrive. Debugger transport/editor polling can overshoot the requested frame count. Disconnecting the CLI cancels a capture it owns; it does not stop an independently started capture. Resume a hard debugger pause before controlling profiling. If a pause prevents final totals from arriving, resume and retry `report`/`status`.

| Action/options | Meaning/default |
| --- | --- |
| `start`, `capture`: `--max-functions 16..512` | Runtime function cap per record; default 512, without changing editor preferences |
| `start`, `capture`: `--native` | Also collect native calls made by scripts; off by default |
| `capture`: `--frames 1..1800` | Received frame target; default 120 |
| `capture`, `stop`: `--timeout 1..30` | Seconds; default 10. Capture allows another 5 seconds for stop totals |
| `report`: `--view total\|frame` | Default total; latest frame when `frame` has no `--frame` |
| `report`: `--frame <number>` | Retained engine frame number; requires `--view frame` |
| `report`, `stop`, `capture`: `--sort self\|inclusive\|calls\|native` | Descending; default self |
| `report`, `stop`, `capture`: `--filter <text>` | Function signature/name substring |
| `frames`: `--sort frame\|frame_time\|process\|physics` | Descending; default newest frame first |
| Reports: `--limit 1..1000` | Rows shown; default 30 |

Interpretation:

- Function rows contain milliseconds, call counts, average self/inclusive milliseconds per call, signature, script and line. `view: accumulated` covers the whole capture; `view: frame` covers one frame. A large accumulated total is not a per-frame stall. Inclusive time includes callees, so inclusive rows overlap and must not be summed.
- Self and native/internal values are the engine's profiler measurements. Native recording changes attribution and adds overhead; compare captures with the same setting. Native rows are collated across callers, not a call tree or reliable per-caller breakdown. Prefer a separate `--native` capture when investigating engine API costs.
- The runtime sends its most expensive functions by inclusive time, up to `max_functions`, **before** CLI filtering/sorting. `possibly_truncated` means that cap was reached; absence is not proof a function never ran. The runtime's `debug/settings/profiler/max_functions` buffer is a further ceiling. The Script Functions category is a sum of transmitted self times, not total CPU time.
- Accumulated totals come from the runtime on stop, never from adding sampled top-function lists. Frame history uses the editor's existing bounded ring (`debugger/profiler_frame_history_size`); older frames can disappear while accumulated totals still cover the full capture. `frames_received` counts delivered records, not necessarily every engine frame.
- Frame/process/physics timings are available on single-frame reports and `frames`. These are omitted from accumulated reports because the final packet's frame timings are not capture totals. Physics frame time is the physics interval, distinct from physics execution time.

Capture the focused, active screen after assets have loaded. Measure steady gameplay separately from screen-opening/loading. Sort by self to find local work, by inclusive to find expensive call chains, and by calls to identify repeated work. Inspect actual code before changing it. Repeat the same viewport, population, interactions and native setting after an optimization; use `perf` without profiling for overall frame pacing. Editor GDScript timings do not establish gd2cpp/mobile release performance.

## Overall counters

```powershell
godot --wg perf
godot --wg perf --frames 240 --timeout 15 --output perf.json
```

`--frames` defaults to 0 (snapshot), maximum 1800. Timeout is 1..30 seconds, default 10. Sampling adds mean, p95, min/max and sample counts to monitors, plus actual `frame_interval_ms` statistics and elapsed seconds. Check `timed_out` and the number of captured frames.

The report includes FPS, process/physics/navigation timings, memory, rendering and pipeline-compilation counters, and object/node/resource counts. Some engine monitors update less often than once per frame: repeated values do not mean each frame cost exactly that much. Use `frame_interval_ms` for observed pacing and `profile` for function costs. Video memory includes textures and buffers; do not add those counters together. Static memory is engine-tracked allocation, not total process RSS.

## Texture inventory and rendered use

```powershell
godot --wg textures --sort bytes --limit 40
godot --wg textures --frames 120 --sort savings --headroom 1 --limit 40 --output textures.json
godot --wg textures --frames 120 --filter backend:// --json
godot --wg textures --frames 120 --unused --limit 40
```

Frames/timeout have the same ranges/defaults as `perf`. `--sort bytes|savings|ratio` defaults to bytes. Savings, ratio and `--unused` require a capture. `--limit` is 1..1000 (default 30); `--filter` searches path/resource path/name case-insensitively. Only one `perf` or texture capture can run in a game at once.

Inventory reports renderer-estimated allocation bytes, dimensions, format, mip levels, render-target status, RID and resource labels. Aliases are identified and excluded from allocation totals. WebP/PNG download size is not GPU allocation size. Transparent atlas pixels occupy GPU storage too; an `AtlasTexture` region alone does not create a smaller GPU allocation.

A capture adds submitted draw-command/frame counts, canvas node paths, maximum rendered bounds, pixels-per-texel ratio, mapping uncertainty and candidate sizes/savings. Measurements use render-target pixels after transforms and rectangular clipping, without occlusion testing. Unsupported/shader-driven mappings and render targets do not get resize recommendations. Inventory is taken at report time; aliases are resolved at capture start. Check `timed_out`, `capture_truncated`, `measurement_uncertain`, and `nodes_truncated`.

Candidate sizes preserve the source aspect ratio and use the largest observed texel scale multiplied by `--headroom` (1..4, default 1.25). Use `--headroom 1` for the measured baseline; justify any extra resolution from actual supported display sizes/zoom or another use. **A candidate covers only sampled, understood uses.** Before resizing, inspect every use, including crop/fill modes, atlases, tiles, animations, other screens, scrolling and maximum target viewport. A small clipped rectangle can still require a large source. `--unused` means unobserved in this 2D capture, not safe to delete/unload.

### Downloaded/runtime textures

GPU inventory also includes textures created from backend bytes, render targets, procedural images and unnamed textures; it is not limited to `res://` imports. Runtime textures may have only a RID until labeled. Labels improve identification, not coverage.

For a runtime asset loader, set `Texture2D.resource_name` and `RenderingServer.texture_set_path(texture.get_rid(), label)` in editor-only code, for example `backend://BUILDING_TEXTURE/10_0_7`. Do not replace an actual resource path with a pretend file path. In gd2cpp projects, use the existing `#wgodot::no_export::begin` / `#wgodot::no_export::end` block convention so diagnostic labels are omitted from generated game code. Unlabeled assets still appear by RID/class/dimensions, and sampled canvas nodes can identify their consumers.

The WGodot CLI module and renderer capture hooks are editor-only. These tools do not require shipping a monitoring service, asset inventory or CLI in the exported game.
