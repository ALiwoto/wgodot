// wgodot-changes::file
#include "tween.h"

bool Tween::wgodot_can_append() const {
	ERR_FAIL_COND_V_MSG(!valid, false, "Tween invalid. Either finished or created outside scene tree.");
	ERR_FAIL_COND_V_MSG(started, false, "Can't append to a Tween that has started. Use stop() first.");
	return true;
}

PropertyTweener::PropertyTweener(const Object *p_target, const Variant &p_initial, const Variant &p_to, double p_duration) :
		target(p_target->get_instance_id()), initial_val(p_initial), base_final_val(p_to), final_val(p_to), duration(p_duration) {
	if (p_target->is_ref_counted()) {
		ref_copy = p_target;
	}
}

Variant PropertyTweener::wgodot_read_property(Object *p_target) const {
	return p_target->get_indexed(property);
}

void PropertyTweener::wgodot_write_property(Object *p_target, const Variant &p_value) const {
	p_target->set_indexed(property, p_value);
}
