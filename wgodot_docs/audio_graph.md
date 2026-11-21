# AudioStreamGroup delayed children

`delay_range` holds a group's mixing before its selected children contribute.
By default, children start after that delay. Set `start_children_before_delay`
to prepare and start their playbacks when the parent starts instead. The parent
still holds their sample clocks until its delay expires; this supports sources
that prepare an intro and a scheduled loop together. Seeking preserves choices
and applies the same lifecycle. The setting defaults to false.

The live audio observer reports these real children as pending through the delayed
ancestor. Each group's delay remains on that group's record, in its own clock
units. It does not fabricate unsampled random branches or flatten distinct clocks.

## Per-stream stereo panning

`AudioStreamPanner` wraps one stream before its parent mixes branches. Declare
`input_channels` as 1 or 2. `mode` selects disabled, linear or equal-power angular
panning between the left/right speakers at −90/+90 degrees. `mono_gain` controls
each channel's non-spatial mono gain; non-spatial stereo keeps its identity.
`omnidirectional_radius` blends toward equal channel gains inside the given
radius, and `stereo_spread` separates the two spatial stereo emitters. Distances
use metres. This wrapper produces front stereo; it does not implement surround.

The ordinary 3D player sends its nearest listener's `listener_frame` atomically:
origin is emitter position in listener coordinates, basis transforms world axes
to listener coordinates. Without a listener, routing is non-spatial. Disable the
player's outer `panning_enabled` when this graph owns panning; distance attenuation,
filtering and bus sends remain independent. Each playback copies its settings at
instantiation, and synchronizes listener input with mixing. Child clocks, loops,
parameters and metadata-only playback are preserved.
The player's `stream_spatialization_enabled` can independently disable graph
spatialization for a component without editing the shared stream resource.

`channel_matrix(position, up, spatial)` returns the row-major 2×2 matrix used by
the mixer. The live observer captures the actual playback matrix and child;
declared channel count is checked separately against the waveform's format.
