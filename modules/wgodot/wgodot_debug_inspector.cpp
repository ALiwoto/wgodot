// wgodot-changes::file
/**************************************************************************/
/*  wgodot_debug_inspector.cpp                                            */
/**************************************************************************/

#include "wgodot_debug_inspector.h"

#include "wgodot_debug_value.h"

#include "core/io/resource.h"
#include "core/object/object.h"
#include "core/object/script_language.h"
#include "core/templates/hash_set.h"
#include "core/variant/variant_parser.h"

namespace WGodotDebugInspector {

namespace {

Dictionary make_error(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = "debug_inspect";
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

String get_script_type(Object *p_object) {
	ScriptInstance *instance = p_object->get_script_instance();
	if (instance != nullptr) {
		Ref<Script> script = instance->get_script();
		if (script.is_valid() && !script->get_global_name().is_empty()) {
			return script->get_global_name();
		}
	}
	return p_object->get_class();
}

String format_type(const PropertyInfo &p_property, const Variant &p_value) {
	if (!p_property.class_name.is_empty()) {
		return p_property.class_name;
	}
	if (p_property.type == Variant::OBJECT && !p_property.hint_string.is_empty()) {
		return p_property.hint_string;
	}
	if (p_property.type == Variant::NIL) {
		return (p_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT) || p_value.get_type() == Variant::NIL ? "Variant" : Variant::get_type_name(p_value.get_type());
	}
	return Variant::get_type_name(p_property.type);
}

String format_value(const Variant &p_value, bool p_expand) {
	if (p_value.get_type() == Variant::OBJECT) {
		Ref<Resource> resource = p_value;
		if (resource.is_valid() && !resource->get_path().is_empty()) {
			String text;
			VariantWriter::write_to_string(resource->get_path(), text);
			return text;
		}
		Object *object = p_value.get_validated_object();
		return object ? vformat("<%s#%d>", get_script_type(object), static_cast<int64_t>(object->get_instance_id())) : "null";
	}
	if (p_value.get_type() == Variant::NIL) {
		return "null";
	}
	if (!p_expand) {
		String compact;
		if (WGodotDebugValue::format_compact(p_value, compact)) {
			return compact;
		}
	}
	String text;
	if (VariantWriter::write_to_string(p_value, text) != OK) {
		text = p_value.stringify();
	}
	return text;
}

} // namespace

Dictionary inspect_object(const Dictionary &p_options) {
	const int64_t requested_id = p_options.get("object_id", 0);
	if (requested_id <= 0) {
		return make_error("invalid_object_id", "A positive live object ID is required.");
	}

	Object *object = ObjectDB::get_instance(ObjectID(static_cast<uint64_t>(requested_id)));
	if (object == nullptr) {
		return make_error("object_not_found", vformat("Live object %d no longer exists.", requested_id));
	}

	HashSet<StringName> current_script_members;
	HashSet<StringName> all_script_members;
	const String expanded_member = p_options.get("expanded_member", String());
	if (ScriptInstance *instance = object->get_script_instance()) {
		Ref<Script> script = instance->get_script();
		bool current = true;
		while (script.is_valid()) {
			HashSet<StringName> names;
			script->get_members(&names);
			for (const StringName &name : names) {
				all_script_members.insert(name);
				if (current) {
					current_script_members.insert(name);
				}
			}
			current = false;
			script = script->get_base_script();
		}
	}

	Array members;
	HashSet<StringName> emitted;
	List<PropertyInfo> properties;
	object->get_property_list(&properties, true);
	for (const PropertyInfo &property : properties) {
		if (property.name.is_empty() || property.name.begins_with("@") ||
				property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) {
			continue;
		}

		const StringName name = property.name;
		const bool script_member = all_script_members.has(name) || (property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE);
		if (!script_member && property.name.contains("/")) {
			continue;
		}
		if (emitted.has(name)) {
			continue;
		}

		bool valid = false;
		const Variant value = object->get(name, &valid);
		if (!valid) {
			continue;
		}

		Dictionary member;
		member["name"] = String(name);
		member["type"] = format_type(property, value);
		member["variant_type"] = Variant::get_type_name(value.get_type());
		member["value"] = format_value(value, property.name == expanded_member);
		member["builtin"] = !script_member;
		member["current"] = script_member && current_script_members.has(name);
		if (value.get_type() == Variant::OBJECT) {
			Object *member_object = value.get_validated_object();
			if (member_object != nullptr) {
				member["object_id"] = static_cast<int64_t>(member_object->get_instance_id());
			}
		}
		members.push_back(member);
		emitted.insert(name);
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "debug_inspect";
	response["object_id"] = requested_id;
	response["type"] = get_script_type(object);
	response["members"] = members;
	return response;
}

} // namespace WGodotDebugInspector
