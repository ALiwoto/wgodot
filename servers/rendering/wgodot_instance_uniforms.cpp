// wgodot-changes::file

#include "instance_uniforms.h"

#include "servers/rendering/rendering_server_globals.h"

bool InstanceUniforms::_wgodot_finish_materials(RID p_self) {
	ERR_FAIL_COND_V(p_self.is_null(), false);

	// Explicit instance_index declarations may leave holes. Reserve through the
	// highest live index, rather than 16 vectors for every mesh using any uniform.
	int32_t required_slots = 0;
	for (const KeyValue<StringName, Item> &entry : _parameters) {
		if (entry.value.is_valid()) {
			required_slots = MAX(required_slots, entry.value.index + 1);
		}
	}

	const int32_t previous_location = _location;
	if (required_slots == 0) {
		free(p_self);
		return previous_location != _location;
	}

	if (is_allocated() && required_slots > _allocated_slots) {
		// Keep the parameter values while moving to a larger allocation.
		RSG::material_storage->global_shader_parameters_instance_free(p_self);
		_location = -1;
		_allocated_slots = 0;
	}
	if (!is_allocated()) {
		_location = RSG::material_storage->global_shader_parameters_instance_allocate(p_self, required_slots);
		if (!is_allocated()) {
			return previous_location != _location;
		}
		_allocated_slots = required_slots;
	}

	for (const KeyValue<StringName, Item> &entry : _parameters) {
		const Item &item = entry.value;
		if (item.is_valid()) {
			RSG::material_storage->global_shader_parameters_instance_update(p_self, item.index, item.value, item.flags);
		}
	}
	return previous_location != _location;
}
