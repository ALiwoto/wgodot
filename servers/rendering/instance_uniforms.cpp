/**************************************************************************/
/*  instance_uniforms.cpp                                                 */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "instance_uniforms.h"

#include "servers/rendering/rendering_server_globals.h"

// wgodot-changes::begin
Ref<WInstanceShaderParameters> InstanceUniforms::get_parameters() {
	if (parameters.is_null()) {
		parameters = memnew(WInstanceShaderParameters);
	}
	return parameters;
}

void InstanceUniforms::free(RID p_self) {
	ERR_FAIL_COND(p_self.is_null());
	if (is_allocated()) {
		RSG::material_storage->global_shader_parameters_instance_free(p_self);
		_location = -1;
	}
	_allocated_slots = 0;
	if (parameters.is_valid()) {
		parameters->invalidate();
	}
}

void InstanceUniforms::materials_start() {
	if (parameters.is_valid()) {
		parameters->invalidate();
	}
}

void InstanceUniforms::materials_append(RID p_material) {
	ERR_FAIL_COND(p_material.is_null());
	List<RendererMaterialStorage::InstanceShaderParam> declarations;
	RSG::material_storage->material_get_instance_shader_parameters(p_material, &declarations);
	for (const auto &declaration : declarations) {
		get_parameters()->define(declaration.info, declaration.index, declaration.default_value);
	}
}

bool InstanceUniforms::materials_finish(RID p_self) {
	return _wgodot_finish_materials(p_self);
}

void InstanceUniforms::upload(RID p_self, int p_slot, WInstanceShaderValue p_value) {
	ERR_FAIL_COND(parameters.is_null());
	const int index = parameters->gpu_index(p_slot, p_value);
	if (index >= 0) {
		RSG::material_storage->global_shader_parameters_instance_update(p_self, index, p_value);
	}
}

Variant InstanceUniforms::get(const StringName &p_name) const {
	const int slot = parameters.is_valid() ? parameters->find(p_name) : -1;
	return slot >= 0 ? parameters->value(slot).to_variant() : Variant();
}

void InstanceUniforms::set(RID p_self, const StringName &p_name, const Variant &p_value) {
	const int slot = get_parameters()->find_or_add(p_name);
	if (p_value.get_type() == Variant::NIL) {
		upload(p_self, slot, parameters->clear(slot));
		return;
	}
	WInstanceShaderValue value = WInstanceShaderValue::from_variant(p_value);
	if (value.valid() && parameters->set(slot, value)) {
		upload(p_self, slot, value);
	}
}

Variant InstanceUniforms::get_default(const StringName &p_name) const {
	const int slot = parameters.is_valid() ? parameters->find(p_name) : -1;
	return slot >= 0 ? parameters->get(slot).default_value.to_variant() : Variant();
}

void InstanceUniforms::get_property_list(List<PropertyInfo> &r_parameters) const {
	if (parameters.is_null()) {
		return;
	}
	Vector<PropertyInfo> properties;
	for (const auto &item : parameters->snapshot()) {
		if (item.gpu_index >= 0) {
			properties.push_back(item.info);
		}
	}
	struct NameLess {
		bool operator()(const PropertyInfo &p_left, const PropertyInfo &p_right) const {
			return StringName::AlphCompare()(p_left.name, p_right.name);
		}
	};
	properties.sort_custom<NameLess>();
	for (const PropertyInfo &property : properties) {
		r_parameters.push_back(property);
	}
}
// wgodot-changes::end
