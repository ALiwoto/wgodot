// wgodot-changes::file
/**************************************************************************/
/*  wgodot_member_list.cpp                                                */
/**************************************************************************/

#include "wgodot_member_list.h"

#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/variant_parser.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#endif

namespace WGodotMemberList {

namespace {

enum MemberKind {
	MEMBER_CONST,
	MEMBER_ENUM,
	MEMBER_VAR,
	MEMBER_STATIC_VAR,
	MEMBER_FUNC,
	MEMBER_STATIC_FUNC,
	MEMBER_SIGNAL,
	MEMBER_CLASS,
	MEMBER_KIND_MAX,
};

constexpr const char *SECTION_TITLES[MEMBER_KIND_MAX] = {
	"Consts",
	"Enums",
	"Vars",
	"Static Vars",
	"Functions",
	"Static Functions",
	"Signals",
	"Classes",
};

constexpr const char *KIND_NAMES[MEMBER_KIND_MAX] = {
	"const",
	"enum",
	"var",
	"static_var",
	"func",
	"static_func",
	"signal",
	"class",
};

struct MemberCollection {
	HashMap<StringName, Dictionary> sections[MEMBER_KIND_MAX];
};

Dictionary make_error(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["command"] = "list";
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

String format_value(const Variant &p_value) {
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value.get_validated_object();
		return object ? vformat("<%s#%d>", object->get_class(), static_cast<int64_t>(object->get_instance_id())) : "null";
	}
	if (p_value.get_type() == Variant::ARRAY || p_value.get_type() == Variant::DICTIONARY) {
		return p_value.stringify();
	}

	String result;
	if (VariantWriter::write_to_string(p_value, result) != OK) {
		result = p_value.stringify();
	}
	return result;
}

String format_property_type(const PropertyInfo &p_property, bool p_is_return = false) {
	if (p_property.type == Variant::NIL) {
		if (p_property.usage & PROPERTY_USAGE_NIL_IS_VARIANT) {
			return "Variant";
		}
		return p_is_return ? "void" : "null";
	}
	if (p_property.type == Variant::INT && (p_property.usage & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD))) {
		return p_property.class_name.is_empty() ? "<unknown enum>" : String(p_property.class_name);
	}
	if (p_property.type == Variant::ARRAY && p_property.hint == PROPERTY_HINT_ARRAY_TYPE) {
		return p_property.hint_string.is_empty() ? "Array[<unknown type>]" : "Array[" + p_property.hint_string + "]";
	}
	if (p_property.type == Variant::DICTIONARY && p_property.hint == PROPERTY_HINT_DICTIONARY_TYPE) {
		return p_property.hint_string.is_empty() ? "Dictionary[<unknown type>, <unknown type>]" : "Dictionary[" + p_property.hint_string.replace(";", ", ") + "]";
	}
	if (p_property.type == Variant::OBJECT && !p_property.class_name.is_empty()) {
		return p_property.class_name;
	}
	return Variant::get_type_name(p_property.type);
}

String get_script_display_name(const Ref<Script> &p_script) {
	if (p_script.is_null()) {
		return String();
	}
	if (!p_script->get_global_name().is_empty()) {
		return p_script->get_global_name();
	}
#ifdef MODULE_GDSCRIPT_ENABLED
	GDScript *gdscript = Object::cast_to<GDScript>(p_script.ptr());
	if (gdscript && !gdscript->get_local_name().is_empty()) {
		return gdscript->get_local_name();
	}
#endif
	return p_script->get_instance_base_type();
}

String get_object_display_type(Object *p_object) {
	if (p_object == nullptr) {
		return "Object";
	}
	Ref<Script> script = p_object->get_script();
	const String script_name = get_script_display_name(script);
	return script_name.is_empty() ? String(p_object->get_class()) : script_name;
}

String format_method_signature(const MethodInfo &p_method, bool p_signal = false) {
	String signature = p_method.name + "(";
	const int default_start = p_method.arguments.size() - p_method.default_arguments.size();
	for (int i = 0; i < p_method.arguments.size(); i++) {
		if (i > 0) {
			signature += ", ";
		}
		const PropertyInfo &argument = p_method.arguments[i];
		const String argument_name = argument.name.is_empty() ? "arg" + itos(i + 1) : String(argument.name);
		signature += argument_name + ": " + format_property_type(argument);
		if (i >= default_start) {
			signature += " = " + format_value(p_method.default_arguments[i - default_start]);
		}
	}
	if (p_method.flags & METHOD_FLAG_VARARG) {
		if (!p_method.arguments.is_empty()) {
			signature += ", ";
		}
		signature += "...args";
	}
	signature += ")";
	if (!p_signal) {
		signature += " -> " + format_property_type(p_method.return_val, true);
	}
	return signature;
}

bool should_skip_property(const PropertyInfo &p_property) {
	return p_property.name.is_empty() || (p_property.usage & (PROPERTY_USAGE_INTERNAL | PROPERTY_USAGE_CATEGORY | PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP));
}

void add_member(MemberCollection &r_collection, MemberKind p_kind, const StringName &p_name, const String &p_type, const String &p_display, const String &p_filter_type, int p_priority) {
	if (p_name.is_empty() || String(p_name).begins_with("@")) {
		return;
	}
	const Dictionary *existing = r_collection.sections[p_kind].getptr(p_name);
	if (existing && (int)existing->get("priority", 0) >= p_priority) {
		return;
	}
	Dictionary member;
	member["kind"] = KIND_NAMES[p_kind];
	member["name"] = p_name;
	member["type"] = p_type;
	member["display"] = p_display;
	member["filter_type"] = p_filter_type.is_empty() ? p_type : p_filter_type;
	member["priority"] = p_priority;
	r_collection.sections[p_kind][p_name] = member;
}

void collect_properties(const List<PropertyInfo> &p_properties, MemberCollection &r_collection, MemberKind p_kind, int p_priority) {
	for (const PropertyInfo &property : p_properties) {
		if (should_skip_property(property)) {
			continue;
		}
		const String type = format_property_type(property);
		add_member(r_collection, p_kind, property.name, type, String(property.name) + ": " + type, String(), p_priority);
	}
}

void collect_methods(const List<MethodInfo> &p_methods, MemberCollection &r_collection, bool p_signals, int p_priority) {
	for (const MethodInfo &method : p_methods) {
		if (method.name.is_empty() || method.name.begins_with("@")) {
			continue;
		}
		const MemberKind kind = p_signals ? MEMBER_SIGNAL : ((method.flags & METHOD_FLAG_STATIC) ? MEMBER_STATIC_FUNC : MEMBER_FUNC);
		const String return_type = p_signals ? "Signal" : format_property_type(method.return_val, true);
		add_member(r_collection, kind, method.name, return_type, format_method_signature(method, p_signals), String(), p_priority);
	}
}

void collect_native_class(const StringName &p_class, MemberCollection &r_collection, bool p_collect_instance_members) {
	if (p_class.is_empty() || !ClassDB::class_exists(p_class)) {
		return;
	}

	if (p_collect_instance_members) {
		List<PropertyInfo> properties;
		ClassDB::get_property_list(p_class, &properties);
		collect_properties(properties, r_collection, MEMBER_VAR, 1);
	}

	List<MethodInfo> methods;
	ClassDB::get_method_list(p_class, &methods);
	collect_methods(methods, r_collection, false, 1);

	List<MethodInfo> signals;
	ClassDB::get_signal_list(p_class, &signals);
	collect_methods(signals, r_collection, true, 1);

	HashSet<StringName> enum_constants;
	List<StringName> enums;
	ClassDB::get_enum_list(p_class, &enums);
	for (const StringName &enum_name : enums) {
		List<StringName> constants;
		ClassDB::get_enum_constants(p_class, enum_name, &constants);
		for (const StringName &constant : constants) {
			enum_constants.insert(constant);
		}
		const String display = String(enum_name) + ": enum type, " + itos(constants.size()) + (constants.size() == 1 ? " member" : " members");
		add_member(r_collection, MEMBER_ENUM, enum_name, "enum", display, "enum", 1);
	}

	List<String> constants;
	ClassDB::get_integer_constant_list(p_class, &constants);
	for (const String &constant : constants) {
		if (enum_constants.has(constant)) {
			continue;
		}
		bool valid = false;
		const int64_t value = ClassDB::get_integer_constant(p_class, constant, &valid);
		if (valid) {
			add_member(r_collection, MEMBER_CONST, constant, "int", constant + ": int = " + itos(value), String(), 1);
		}
	}
}

#ifdef MODULE_GDSCRIPT_ENABLED
HashSet<StringName> get_gdscript_enum_names(GDScript *p_script, const HashMap<StringName, Variant> &p_constants) {
	HashSet<StringName> enum_names;
#ifdef TOOLS_ENABLED
	const Vector<DocData::ClassDoc> documentation = p_script->get_documentation();
	for (const DocData::ClassDoc &class_doc : documentation) {
		for (const KeyValue<String, DocData::EnumDoc> &enum_entry : class_doc.enums) {
			const StringName enum_name = enum_entry.key;
			if (enum_name != SNAME("@unnamed_enums") && p_constants.has(enum_name)) {
				enum_names.insert(enum_name);
			}
		}
	}
#endif
	return enum_names;
}
#endif

void collect_script(const Ref<Script> &p_script, MemberCollection &r_collection) {
	if (p_script.is_null()) {
		return;
	}

	List<PropertyInfo> properties;
	p_script->get_script_property_list(&properties);
	collect_properties(properties, r_collection, MEMBER_VAR, 3);

	List<PropertyInfo> static_properties;
	p_script->get_property_list(&static_properties);
	int static_property_priority = 3;
	for (const PropertyInfo &property : static_properties) {
		if (should_skip_property(property) || !(property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
			continue;
		}
		const String type = format_property_type(property);
		add_member(r_collection, MEMBER_STATIC_VAR, property.name, type, String(property.name) + ": " + type, String(), static_property_priority++);
	}

	List<MethodInfo> methods;
	p_script->get_script_method_list(&methods);
	collect_methods(methods, r_collection, false, 3);

	List<MethodInfo> signals;
	p_script->get_script_signal_list(&signals);
	collect_methods(signals, r_collection, true, 3);

	Ref<Script> current = p_script;
	while (current.is_valid()) {
		HashMap<StringName, Variant> constants;
		current->get_constants(&constants);
		HashSet<StringName> class_names;
		HashSet<StringName> enum_names;

#ifdef MODULE_GDSCRIPT_ENABLED
		GDScript *gdscript = Object::cast_to<GDScript>(current.ptr());
		if (gdscript) {
			enum_names = get_gdscript_enum_names(gdscript, constants);
			for (const KeyValue<StringName, Ref<GDScript>> &subclass : gdscript->get_subclasses()) {
				class_names.insert(subclass.key);
				Ref<Script> subclass_script = subclass.value;
				Ref<Script> base_script = subclass_script->get_base_script();
				String base_type = get_script_display_name(base_script);
				if (base_type.is_empty()) {
					base_type = subclass_script->get_instance_base_type();
				}
				const String display = String(subclass.key) + (base_type.is_empty() ? ": class" : ": class extends " + base_type);
				add_member(r_collection, MEMBER_CLASS, subclass.key, "class", display, base_type, 3);
			}
		}
#endif

		for (const StringName &enum_name : enum_names) {
			const Dictionary values = constants[enum_name];
			const int count = values.size();
			const String display = String(enum_name) + ": enum type, " + itos(count) + (count == 1 ? " member" : " members");
			add_member(r_collection, MEMBER_ENUM, enum_name, "enum", display, "enum", 3);
		}
		for (const KeyValue<StringName, Variant> &constant : constants) {
			if (enum_names.has(constant.key) || class_names.has(constant.key)) {
				continue;
			}
			String type = Variant::get_type_name(constant.value.get_type());
			if (constant.value.get_type() == Variant::OBJECT) {
				type = get_object_display_type(constant.value.get_validated_object());
			}
			add_member(r_collection, MEMBER_CONST, constant.key, type, String(constant.key) + ": " + type + " = " + format_value(constant.value), String(), 3);
		}

		current = current->get_base_script();
	}
}

void collect_object(Object *p_object, MemberCollection &r_collection) {
	List<PropertyInfo> properties;
	p_object->get_property_list(&properties);
	collect_properties(properties, r_collection, MEMBER_VAR, 2);

	List<MethodInfo> methods;
	p_object->get_method_list(&methods);
	collect_methods(methods, r_collection, false, 2);

	List<MethodInfo> signals;
	p_object->get_signal_list(&signals);
	collect_methods(signals, r_collection, true, 2);

	collect_native_class(p_object->get_class_name(), r_collection, false);
	Ref<Script> script = p_object->get_script();
	if (script.is_valid()) {
		collect_script(script, r_collection);
	}
}

bool parse_member_path(const String &p_path, Vector<StringName> &r_segments) {
	r_segments.clear();
	const PackedStringArray segments = p_path.split(".");
	for (const String &segment : segments) {
		if (segment.is_empty()) {
			r_segments.clear();
			return false;
		}
		r_segments.push_back(segment);
	}
	return !r_segments.is_empty();
}

bool load_named_class(const String &p_class_name, Ref<Script> &r_script, Dictionary &r_error) {
	if (!ScriptServer::is_global_class(p_class_name)) {
		r_error = make_error("named_class_not_found", "Named script class was not found: " + p_class_name);
		return false;
	}
	const String script_path = ScriptServer::get_global_class_path(p_class_name);
	r_script = ResourceLoader::load(script_path, "Script");
	if (r_script.is_null() || !r_script->is_valid()) {
		r_error = make_error("named_class_load_failed", "Could not load named script class " + p_class_name + " from: " + script_path);
		return false;
	}
	return true;
}

bool resolve_nested_target(const String &p_target, Variant &r_value, Object *&r_object, Dictionary &r_error) {
	const int separator = p_target.find_char('.');
	if (separator <= 0 || separator == p_target.length() - 1) {
		return false;
	}

	const String class_name = p_target.substr(0, separator);
	if (!ScriptServer::is_global_class(class_name)) {
		return false;
	}

	Ref<Script> script;
	if (!load_named_class(class_name, script, r_error)) {
		return true;
	}

	Vector<StringName> member_path;
	if (!parse_member_path(p_target.substr(separator + 1), member_path)) {
		r_error = make_error("invalid_member_path", "Invalid nested list target: " + p_target);
		return true;
	}

	bool valid = false;
	r_value = script->get_indexed(member_path, &valid);
	if (!valid) {
		r_error = make_error("member_not_found", "Nested member was not found: " + p_target);
		return true;
	}
	if (r_value.get_type() != Variant::OBJECT || r_value.get_validated_object() == nullptr) {
		r_error = make_error("member_not_object", "Nested list target is not a live Object: " + p_target);
		return true;
	}
	r_object = r_value.get_validated_object();
	return true;
}

bool canonicalize_member_kind(const String &p_source, MemberKind &r_kind) {
	const String kind = p_source.strip_edges().to_lower().replace("-", "_");
	if (kind == "func" || kind == "function" || kind == "method") {
		r_kind = MEMBER_FUNC;
	} else if (kind == "static_func" || kind == "static_function" || kind == "static_method") {
		r_kind = MEMBER_STATIC_FUNC;
	} else if (kind == "signal") {
		r_kind = MEMBER_SIGNAL;
	} else if (kind == "var" || kind == "variable" || kind == "property") {
		r_kind = MEMBER_VAR;
	} else if (kind == "static_var" || kind == "static_variable" || kind == "static_property") {
		r_kind = MEMBER_STATIC_VAR;
	} else if (kind == "const" || kind == "constant") {
		r_kind = MEMBER_CONST;
	} else if (kind == "enum" || kind == "enumeration") {
		r_kind = MEMBER_ENUM;
	} else if (kind == "class" || kind == "inner_class" || kind == "nested_class") {
		r_kind = MEMBER_CLASS;
	} else {
		return false;
	}
	return true;
}

bool type_inherits(const StringName &p_candidate, const StringName &p_filter) {
	if (String(p_candidate).nocasecmp_to(p_filter) == 0) {
		return true;
	}
	if (ClassDB::class_exists(p_candidate) && ClassDB::class_exists(p_filter)) {
		return ClassDB::is_parent_class(p_candidate, p_filter);
	}
	StringName type = p_candidate;
	while (ScriptServer::is_global_class(type)) {
		if (type == p_filter) {
			return true;
		}
		type = ScriptServer::get_global_class_base(type);
	}
	return ClassDB::class_exists(type) && ClassDB::class_exists(p_filter) && ClassDB::is_parent_class(type, p_filter);
}

bool is_known_filter_type(const String &p_filter) {
	if (p_filter.is_empty() || p_filter == "*" || ClassDB::class_exists(p_filter) || ScriptServer::is_global_class(p_filter)) {
		return true;
	}
	if (Variant::get_type_by_name(p_filter) < Variant::VARIANT_MAX) {
		return true;
	}
	const String lower = p_filter.to_lower();
	return lower == "variant" || lower == "void" || lower == "null" || lower == "signal" || lower == "enum" || lower == "class";
}

Array make_sections(MemberCollection &p_collection, const HashSet<MemberKind> &p_included_kinds, const String &p_type_filter) {
	Array sections;
	for (int kind_index = 0; kind_index < MEMBER_KIND_MAX; kind_index++) {
		const MemberKind kind = static_cast<MemberKind>(kind_index);
		if (!p_included_kinds.is_empty() && !p_included_kinds.has(kind)) {
			continue;
		}

		Vector<StringName> names;
		for (const KeyValue<StringName, Dictionary> &member : p_collection.sections[kind]) {
			const String filter_type = member.value.get("filter_type", String());
			if (p_type_filter.is_empty() || p_type_filter == "*" || type_inherits(filter_type, p_type_filter)) {
				names.push_back(member.key);
			}
		}
		names.sort();
		if (names.is_empty()) {
			continue;
		}

		Array members;
		for (const StringName &name : names) {
			Dictionary member = p_collection.sections[kind][name];
			member.erase("filter_type");
			member.erase("priority");
			members.push_back(member);
		}
		Dictionary section;
		section["kind"] = KIND_NAMES[kind];
		section["title"] = SECTION_TITLES[kind];
		section["members"] = members;
		sections.push_back(section);
	}
	return sections;
}

} // namespace

Dictionary execute(const Dictionary &p_options) {
	const String target_name = p_options.get("target", String());
	if (target_name.is_empty()) {
		return make_error("target_required", "list requires a node path or class name.");
	}

	const String type_filter = String(p_options.get("filter", String())).strip_edges();
	if (!is_known_filter_type(type_filter)) {
		return make_error("invalid_type_filter", "Unknown type for --filter: " + type_filter);
	}

	HashSet<MemberKind> included_kinds;
	const PackedStringArray member_types = p_options.get("member_types", PackedStringArray());
	for (const String &member_type : member_types) {
		MemberKind kind;
		if (!canonicalize_member_kind(member_type, kind)) {
			return make_error("invalid_member_type", "Unknown member type: " + member_type);
		}
		included_kinds.insert(kind);
	}

	MemberCollection collection;
	String target_kind;
	String target_type;
	Variant target_holder;
	if (target_name.begins_with("/")) {
		SceneTree *scene_tree = SceneTree::get_singleton();
		Node *root = scene_tree ? scene_tree->get_root() : nullptr;
		if (root == nullptr) {
			return make_error("scene_tree_unavailable", "The running game has no scene tree.");
		}
		Node *node = root->get_node_or_null(NodePath(target_name));
		if (node == nullptr) {
			return make_error("node_not_found", "Target node was not found: " + target_name);
		}
		target_kind = "node";
		target_type = get_object_display_type(node);
		collect_object(node, collection);
	} else if (ScriptServer::is_global_class(target_name)) {
		Ref<Script> script;
		Dictionary load_error;
		if (!load_named_class(target_name, script, load_error)) {
			return load_error;
		}
		target_kind = "class";
		target_type = target_name;
		collect_native_class(script->get_instance_base_type(), collection, true);
		collect_script(script, collection);
	} else if (ClassDB::class_exists(target_name)) {
		target_kind = "class";
		target_type = target_name;
		collect_native_class(target_name, collection, true);
	} else {
		Object *nested_object = nullptr;
		Dictionary nested_error;
		if (!resolve_nested_target(target_name, target_holder, nested_object, nested_error)) {
			return make_error("target_not_found", "No runtime node or named/native class was found: " + target_name);
		}
		if (!nested_error.is_empty()) {
			return nested_error;
		}

		Script *nested_script_object = Object::cast_to<Script>(nested_object);
		if (nested_script_object) {
			Ref<Script> nested_script(nested_script_object);
			target_kind = "class";
			target_type = get_script_display_name(nested_script);
			collect_native_class(nested_script->get_instance_base_type(), collection, true);
			collect_script(nested_script, collection);
		} else {
			Node *nested_node = Object::cast_to<Node>(nested_object);
			target_kind = nested_node ? "node" : "object";
			target_type = get_object_display_type(nested_object);
			collect_object(nested_object, collection);
		}
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "list";
	response["target"] = target_name;
	response["target_kind"] = target_kind;
	response["target_type"] = target_type;
	response["sections"] = make_sections(collection, included_kinds, type_filter);
	return response;
}

} // namespace WGodotMemberList
