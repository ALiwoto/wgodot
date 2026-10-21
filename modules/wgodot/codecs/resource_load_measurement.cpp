// wgodot-changes::file
#include "resource_load_measurement.h"

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)
#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/resources/resource_format_text.h"

void WResourceLoadMeasurement::_bind_methods() {
	ClassDB::bind_static_method("WResourceLoadMeasurement", D_METHOD("read_text", "path", "disk_cache"), &WResourceLoadMeasurement::read_text, DEFVAL(false));
	ClassDB::bind_static_method("WResourceLoadMeasurement", D_METHOD("compare_animations", "expected", "actual"), &WResourceLoadMeasurement::compare_animations);
	ClassDB::bind_method(D_METHOD("get_resource"), &WResourceLoadMeasurement::get_resource);
	ClassDB::bind_method(D_METHOD("get_load_usec"), &WResourceLoadMeasurement::get_load_usec);
	ClassDB::bind_method(D_METHOD("get_error"), &WResourceLoadMeasurement::get_error);
	ClassDB::bind_method(D_METHOD("release"), &WResourceLoadMeasurement::release);
}

Ref<WResourceLoadMeasurement> WResourceLoadMeasurement::read_text(const String &p_path, bool p_disk_cache) {
	Ref<WResourceLoadMeasurement> result;
	result.instantiate();
	// Absolute paths bypass WGodotTextResourceCache; res:// opts into it. Both
	// branches read the exact same physical file through the normal text loader.
	const String path = p_disk_cache ? ProjectSettings::get_singleton()->localize_path(p_path) : ProjectSettings::get_singleton()->globalize_path(p_path);
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	result->resource = ResourceFormatLoaderText::singleton->load(path, String(), &result->error, false, nullptr, ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP);
	result->load_usec = OS::get_singleton()->get_ticks_usec() - started;
	return result;
}

uint64_t WResourceLoadMeasurement::release() {
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	resource.unref();
	return OS::get_singleton()->get_ticks_usec() - started;
}

String WResourceLoadMeasurement::compare_animations(const Ref<Animation> &p_expected, const Ref<Animation> &p_actual) {
	ERR_FAIL_COND_V(p_expected.is_null() || p_actual.is_null(), "Missing animation");
	if (p_expected->get_length() != p_actual->get_length() || p_expected->get_loop_mode() != p_actual->get_loop_mode() || p_expected->get_step() != p_actual->get_step() || p_expected->get_track_count() != p_actual->get_track_count()) {
		return "Animation settings or track count differ";
	}
	for (int track = 0; track < p_expected->get_track_count(); track++) {
		if (p_expected->track_get_type(track) != p_actual->track_get_type(track) || p_expected->track_get_path(track) != p_actual->track_get_path(track) || p_expected->track_get_interpolation_type(track) != p_actual->track_get_interpolation_type(track) || p_expected->track_get_interpolation_loop_wrap(track) != p_actual->track_get_interpolation_loop_wrap(track) || p_expected->track_is_enabled(track) != p_actual->track_is_enabled(track) || p_expected->track_is_imported(track) != p_actual->track_is_imported(track) || p_expected->track_get_key_count(track) != p_actual->track_get_key_count(track)) {
			return "Track settings differ at " + itos(track);
		}
		if (p_expected->track_get_type(track) == Animation::TYPE_VALUE && (p_expected->value_track_get_timing_curves(track) != p_actual->value_track_get_timing_curves(track) || p_expected->value_track_get_timing_curve_subdivisions(track) != p_actual->value_track_get_timing_curve_subdivisions(track))) {
			return "Timing curves differ at track " + itos(track);
		}
		for (int key = 0; key < p_expected->track_get_key_count(track); key++) {
			if (p_expected->track_get_key_time(track, key) != p_actual->track_get_key_time(track, key) || p_expected->track_get_key_transition(track, key) != p_actual->track_get_key_transition(track, key) || p_expected->track_get_key_value(track, key) != p_actual->track_get_key_value(track, key)) {
				return "Key differs at track " + itos(track) + ", key " + itos(key);
			}
		}
	}
	return String();
}
#endif
