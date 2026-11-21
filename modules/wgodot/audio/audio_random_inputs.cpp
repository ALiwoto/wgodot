// wgodot-changes::file
#include "audio_stepper.h"

#ifdef TOOLS_ENABLED
#include "core/math/math_funcs.h"
#include "core/os/thread.h"
#include "modules/audio_graph/audio_stream_group.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

SafeFlag WAudioStepper::random_controlled;

void WAudioStepper::random_error(const String &p_error) {
	if (history.errors.size() < 16) {
		history.errors.push_back(p_error);
	}
}

Error WAudioStepper::begin_random_frame() {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAUTHORIZED);
	ERR_FAIL_COND_V(active != this || stepping || releasing, ERR_BUSY);
	// Group resources have no scene owner. Random replay requires the whole-tree
	// offline clock, preventing changes to players outside the inspection scope.
	ERR_FAIL_COND_V(!SceneTree::get_singleton() || root_id != SceneTree::get_singleton()->get_root()->get_instance_id(), ERR_UNAUTHORIZED);
	ERR_FAIL_COND_V(!random_inputs.is_empty(), ERR_ALREADY_IN_USE);
	random_controlled.set();
	return OK;
}

Error WAudioStepper::queue_random_input(const String &p_stream, double p_unit, const Vector<uint8_t> &p_used) {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAUTHORIZED);
	ERR_FAIL_COND_V(active != this || !random_controlled.is_set() || stepping || releasing, ERR_UNCONFIGURED);
	ERR_FAIL_COND_V(p_stream.is_empty() || !Math::is_finite(p_unit) || p_unit < 0 || p_unit > 1, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(p_used.is_empty() || p_used.size() > 256 || random_inputs.size() >= 4096, ERR_INVALID_PARAMETER);
	for (uint8_t used : p_used) {
		ERR_FAIL_COND_V(used > 1, ERR_INVALID_PARAMETER);
	}
	RandomInput input;
	input.stream = p_stream;
	input.unit = p_unit;
	input.used = p_used;
	random_inputs.push_back(input);
	return OK;
}

bool WAudioStepper::random_input(AudioStreamGroup *p_group, double &r_unit) {
	if (!random_controlled.is_set()) {
		return false;
	}
	r_unit = -1;
	if (!Thread::is_main_thread()) {
		event_thread_error.set();
		return true;
	}
	ERR_FAIL_NULL_V(active, true);
	for (int i = 0; i < active->random_inputs.size(); i++) {
		RandomInput &input = active->random_inputs.write[i];
		if (input.consumed || input.stream != p_group->get_path()) {
			continue;
		}
		input.consumed = true;
		if (input.used.size() != p_group->streams.size()) {
			active->random_error("Audio random input branch count differs: " + input.stream);
			return true;
		}
		if (p_group->mode == AudioStreamGroup::SHUFFLE) {
			ObjectID id = p_group->get_instance_id();
			if (!active->random_original_used.has(id)) {
				active->random_original_used.insert(id, p_group->used);
				p_group->used = input.used;
			} else if (p_group->used != input.used) {
				active->random_error("Audio shuffle history differs before choice: " + input.stream);
				return true;
			}
		}
		r_unit = input.unit;
		return true;
	}
	active->random_error("Missing independent audio random input: " + p_group->get_path());
	return true;
}

Error WAudioStepper::end_random_frame() {
	ERR_FAIL_COND_V(!Thread::is_main_thread(), ERR_UNAUTHORIZED);
	ERR_FAIL_COND_V(active != this || stepping || releasing, ERR_BUSY);
	bool unused = false;
	for (const RandomInput &input : random_inputs) {
		if (!input.consumed) {
			unused = true;
			random_error("Unused independent audio random input: " + input.stream);
		}
	}
	random_inputs.clear();
	return unused || event_thread_error.is_set() || !history.errors.is_empty() ? ERR_INVALID_DATA : OK;
}

void WAudioStepper::end_random_inputs() {
	ERR_FAIL_COND(!Thread::is_main_thread());
	ERR_FAIL_COND(active != this || stepping);
	random_controlled.clear();
	random_inputs.clear();
	for (const KeyValue<ObjectID, Vector<uint8_t>> &entry : random_original_used) {
		AudioStreamGroup *group = Object::cast_to<AudioStreamGroup>(ObjectDB::get_instance(entry.key));
		if (group) {
			MutexLock lock(group->selection_mutex);
			group->used = entry.value;
		}
	}
	random_original_used.clear();
}
#endif
