# Audio inspection

`audio/driver/dummy_metadata_only` is an opt-in, restart-required setting for the
Dummy driver. Vorbis leaves use their final Ogg granule position instead of
allocating a decoder. Their normal resampler clocks, loop/seek behavior and cue
graphs remain active; bus waveform mixing and effects are bypassed. Other leaf
codecs retain normal decoding and report `is_metadata_only() == false`. The live
audio inventory reports those unsupported leaves explicitly. This mode does not
validate waveform output or audible effects.

Editor builds expose `WAudioStepper` for offline inspection with Dummy audio:

1. Call `WAudioStepper.begin(get_tree().root)` before pausing the SceneTree. Keep
   the returned reference. The mixer clock is global, so the complete SceneTree
   root is required.
2. Hold the SceneTree, advance normal gameplay/animation inputs, then call
   `advance(seconds)` once per simulation frame. It invokes the existing audio
   player updates, including spatial panning and pending starts, then advances
   the normal mixer. Fractional sample frames carry into the next call.
3. Inspection redraws do not advance audio. `get_advanced_frames()` exposes the
   exact number of requested sample frames; playback positions may include the
   normal mixer/resampler lookahead.
4. Restore the scene's pause state and call `finish()` to release the clock.

The scope uses the ordinary player implementations. It suppresses wall-clock
player updates for its subtree, interprets the inspection's SceneTree pause as
a held clock, and preserves explicit stream pauses and authored process modes.
The Dummy driver's mutex serializes manual mixing against its ordinary thread.
All manual scene operations run on the main thread. An atomic flag suppresses
ordinary audio updates from worker process groups without reading scope state
on those threads. Only one scope may own the global audio clock.

Doppler velocity histories still use engine frame timestamps. The stepper
rejects Doppler-enabled 3D players explicitly until that clock is controlled;
ordinary spatial panning, attenuation and routing use the normal player code.
