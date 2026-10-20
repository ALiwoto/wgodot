// wgodot-changes::file
#include "visual_instance_3d.h"

#include "core/object/class_db.h"
#include "servers/rendering/rendering_server.h"

const Ref<WInstanceShaderParameters> &GeometryInstance3D::get_instance_shader_parameters() const {
	if (instance_shader_parameters.is_null()) {
		instance_shader_parameters = RS::get_singleton()->instance_geometry_get_shader_parameters(get_instance());
	}
	return instance_shader_parameters;
}

int GeometryInstance3D::get_instance_shader_parameter_slot(const StringName &p_name) const {
	return get_instance_shader_parameters()->find_or_add(p_name);
}

void GeometryInstance3D::_send_instance_shader_parameter(int p_slot, const WInstanceShaderValue &p_value) {
	RS::get_singleton()->instance_geometry_update_shader_parameter(get_instance(), p_slot, p_value);
}

void GeometryInstance3D::clear_instance_shader_parameter(const StringName &p_name) {
	const int slot = get_instance_shader_parameter_slot(p_name);
	_send_instance_shader_parameter(slot, instance_shader_parameters->clear(slot));
}

void GeometryInstance3D::set_instance_shader_parameter(const StringName &p_name, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) {
		clear_instance_shader_parameter(p_name);
		return;
	}
	const int slot = get_instance_shader_parameter_slot(p_name);
	WInstanceShaderValue value = WInstanceShaderValue::from_variant(p_value);
	if (value.valid() && instance_shader_parameters->set(slot, value)) {
		_send_instance_shader_parameter(slot, value);
	}
}

Variant GeometryInstance3D::get_instance_shader_parameter(const StringName &p_name) const {
	const auto &parameters = get_instance_shader_parameters();
	const int slot = parameters->find(p_name);
	return slot >= 0 ? parameters->value(slot).to_variant() : Variant();
}

void GeometryInstance3D::_bind_typed_instance_shader_parameters() {
#define BIND_INSTANCE_PARAMETER(Name) \
	ClassDB::bind_method(D_METHOD("set_instance_shader_parameter_" #Name, "name", "value"), &GeometryInstance3D::set_instance_shader_parameter_##Name); \
	ClassDB::bind_method(D_METHOD("get_instance_shader_parameter_" #Name, "name"), &GeometryInstance3D::get_instance_shader_parameter_##Name);
	BIND_INSTANCE_PARAMETER(bool)
	BIND_INSTANCE_PARAMETER(int)
	BIND_INSTANCE_PARAMETER(float)
	BIND_INSTANCE_PARAMETER(vector2)
	BIND_INSTANCE_PARAMETER(vector3)
	BIND_INSTANCE_PARAMETER(vector4)
	BIND_INSTANCE_PARAMETER(vector2i)
	BIND_INSTANCE_PARAMETER(vector3i)
	BIND_INSTANCE_PARAMETER(vector4i)
	BIND_INSTANCE_PARAMETER(color)
#undef BIND_INSTANCE_PARAMETER
	ClassDB::bind_method(D_METHOD("clear_instance_shader_parameter", "name"), &GeometryInstance3D::clear_instance_shader_parameter);
#ifdef TOOLS_ENABLED
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_bool", "get_instance_shader_parameter_bool", Variant::STRING_NAME, Variant::BOOL });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_int", "get_instance_shader_parameter_int", Variant::STRING_NAME, Variant::INT });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_float", "get_instance_shader_parameter_float", Variant::STRING_NAME, Variant::FLOAT });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_vector2", "get_instance_shader_parameter_vector2", Variant::STRING_NAME, Variant::VECTOR2 });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_vector3", "get_instance_shader_parameter_vector3", Variant::STRING_NAME, Variant::VECTOR3 });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_vector4", "get_instance_shader_parameter_vector4", Variant::STRING_NAME, Variant::VECTOR4 });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_vector2i", "get_instance_shader_parameter_vector2i", Variant::STRING_NAME, Variant::VECTOR2I });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_vector3i", "get_instance_shader_parameter_vector3i", Variant::STRING_NAME, Variant::VECTOR3I });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_vector4i", "get_instance_shader_parameter_vector4i", Variant::STRING_NAME, Variant::VECTOR4I });
	ClassDB::add_native_property_pattern(get_class_static(), { "instance_shader_parameters/", "", "set_instance_shader_parameter_color", "get_instance_shader_parameter_color", Variant::STRING_NAME, Variant::COLOR });
#endif

}
