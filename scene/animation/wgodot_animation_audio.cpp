// wgodot-changes::file
#include "wgodot_animation_audio.h"

#include "scene/resources/animation.h"
#include "scene/resources/audio/audio_stream.h"

bool wgodot_animation_audio_has_ended(const AudioStream &p_stream, double p_start_offset, double p_end_offset) {
	double length = p_stream.get_length();
	if (length <= 0.0 || (p_stream.has_loop() && !Animation::is_greater_approx(p_end_offset, 0.0))) {
		// Unknown-duration streams and untrimmed loops have no finite end.
		return false;
	}
	// Some decoders wrap out-of-range seeks to zero. An expired animation
	// cue must be skipped before asking its decoder to start playback.
	return Animation::is_greater_or_equal_approx(p_start_offset, length - p_end_offset);
}
