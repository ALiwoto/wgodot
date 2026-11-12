// wgodot-changes::file
#include "gdscript_compiler.h"

#include "gdscript.h"
#include "wgodot_record_declarations.h"

namespace {
using Parser = GDScriptParser;

WRecordFieldType field_type(const GDScriptDataType &p_type) {
	WRecordFieldType result;
	result.builtin = p_type.builtin_type;
	result.scalar = p_type.wgodot_scalar;
	result.value_container = p_type.wgodot_value;
	if (p_type.builtin_type == Variant::WRECORD) {
		result.record_name = p_type.native_type;
	} else {
		result.class_name = p_type.native_type;
		if (p_type.script_type) {
			result.declaration = p_type.script_type->get_instance_id();
		}
	}
	for (int index = 0; p_type.has_container_element_type(index); ++index) {
		result.elements.push_back(field_type(p_type.get_container_element_type(index)));
	}
	return result;
}


} // namespace

Error GDScriptCompiler::_wgodot_compile_record(GDScript *p_script, const Parser::ClassNode *p_class) {
	Vector<WRecordSchema::Field> fields;
	Vector<Variant> defaults;
	for (const auto &member : p_class->members) {
		if (member.type != Parser::ClassNode::Member::VARIABLE) {
			continue;
		}
		const Parser::VariableNode *variable = member.variable;
		const GDScriptDataType type = _gdtype_from_datatype(variable->type_constraint, p_script);
		if (!type.has_type() || type.builtin_type == Variant::NIL || type.builtin_type == Variant::CALLABLE || type.builtin_type == Variant::SIGNAL) {
			_set_error("Record fields require concrete data types.", variable);
			return ERR_COMPILATION_FAILED;
		}
		const bool container = type.builtin_type == Variant::ARRAY || type.builtin_type == Variant::DICTIONARY ||
				(type.builtin_type >= Variant::PACKED_BYTE_ARRAY && type.builtin_type <= Variant::PACKED_VECTOR4_ARRAY);
		if (container && !type.wgodot_value) {
			_set_error("Record container fields require @value so record copies own their changes.", variable);
			return ERR_COMPILATION_FAILED;
		}
		WRecordSchema::Field field{ variable->identifier->name, field_type(type) };
		Variant value;
		if (variable->initializer && !WGodotRecordDeclarations::default_literal(variable->initializer, value)) {
			_set_error("Record defaults must be data literals or record construction with literal fields.", variable);
			return ERR_COMPILATION_FAILED;
		}
		if (!variable->initializer && field.type.builtin != Variant::WRECORD && field.type.builtin != Variant::OBJECT) {
			Callable::CallError construction_error;
			Variant::construct(field.type.builtin, value, nullptr, 0, construction_error);
			if (construction_error.error != Callable::CallError::CALL_OK) {
				_set_error("Record field has no default value.", variable);
				return ERR_COMPILATION_FAILED;
			}
		}
		fields.push_back(field);
		defaults.push_back(value);
	}
	Ref<WRecordSchema> schema;
	schema.instantiate();
	schema->initialize(p_class->fqcn, p_script->path, fields, defaults);
	if (!schema->is_sealed()) {
		_set_error("Invalid record declaration or default data.", p_class);
		return ERR_COMPILATION_FAILED;
	}
	p_script->wgodot_record_schema = schema;
	WRecordSchemas::publish(schema);
	p_script->valid = true;
	return OK;
}
