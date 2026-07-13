// wgodot-changes::file
/**************************************************************************/
/*  wgodot_member_list.cpp                                                */
/**************************************************************************/

#include "wgodot_member_list.h"

#include "core/config/project_settings.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/vector.h"
#include "core/variant/variant_parser.h"
#include "modules/modules_enabled.gen.h"
#include "scene/main/node.h"
#include "scene/main/scene_tree.h"
#include "scene/main/window.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#include "modules/gdscript/gdscript_parser.h"
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

enum ResolvedTypeKind {
	RESOLVED_TYPE_NONE,
	RESOLVED_TYPE_SCRIPT,
	RESOLVED_TYPE_NATIVE,
	RESOLVED_TYPE_BUILTIN,
	RESOLVED_TYPE_NATIVE_ENUM,
	RESOLVED_TYPE_BUILTIN_ENUM,
	RESOLVED_TYPE_SCRIPT_ENUM,
};

struct ResolvedType {
	ResolvedTypeKind kind = RESOLVED_TYPE_NONE;
	Ref<Script> script;
	StringName native_class;
	Variant::Type builtin_type = Variant::NIL;
	StringName enum_name;
};

struct ResolvedMethod {
	bool valid = false;
	bool builtin = false;
	MethodInfo info;
	Ref<Script> owner_script;
	StringName native_owner;
	String source_path;
	int start_line = 0;
	int end_line = 0;
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

String get_script_declared_name(const Ref<Script> &p_script) {
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
	return String();
}

String get_script_declaration(const Ref<Script> &p_script) {
	const String declared_name = get_script_declared_name(p_script);
	String declaration = p_script->get_global_name().is_empty() ? "class " + (declared_name.is_empty() ? "<not-named>" : declared_name) : "class_name " + declared_name;

	String path = p_script->get_path();
	const int subresource_separator = path.find("::");
	if (subresource_separator >= 0) {
		path = path.left(subresource_separator);
	}
	if (!path.is_empty()) {
		declaration += ", file: " + path;
	}

	Ref<Script> base_script = p_script->get_base_script();
	String base_name = get_script_declared_name(base_script);
	if (base_name.is_empty()) {
		base_name = p_script->get_instance_base_type();
	}
	if (!base_name.is_empty()) {
		declaration += ", extends " + base_name;
	}
	return declaration;
}

String get_native_declaration(const StringName &p_class) {
	String declaration = "class " + String(p_class);
	const StringName parent = ClassDB::get_parent_class(p_class);
	if (!parent.is_empty()) {
		declaration += ", extends " + String(parent);
	}
	return declaration;
}

String get_object_declaration(Object *p_object) {
	if (p_object == nullptr) {
		return String();
	}
	Ref<Script> script = p_object->get_script();
	return script.is_valid() ? get_script_declaration(script) : get_native_declaration(p_object->get_class_name());
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

void collect_native_enum(const StringName &p_class, const StringName &p_enum, MemberCollection &r_collection) {
	List<StringName> constants;
	ClassDB::get_enum_constants(p_class, p_enum, &constants);
	for (const StringName &constant : constants) {
		bool valid = false;
		const int64_t value = ClassDB::get_integer_constant(p_class, constant, &valid);
		if (valid) {
			add_member(r_collection, MEMBER_CONST, constant, "int", String(constant) + ": int = " + itos(value), String(), 1);
		}
	}
}

void collect_builtin_type(Variant::Type p_type, MemberCollection &r_collection) {
	List<StringName> members;
	Variant::get_member_list(p_type, &members);
	for (const StringName &member : members) {
		const String type = Variant::get_type_name(Variant::get_member_type(p_type, member));
		add_member(r_collection, MEMBER_VAR, member, type, String(member) + ": " + type, String(), 1);
	}

	Variant value;
	Callable::CallError call_error;
	Variant::construct(p_type, value, nullptr, 0, call_error);
	if (call_error.error == Callable::CallError::CALL_OK) {
		List<MethodInfo> methods;
		value.get_method_list(&methods);
		collect_methods(methods, r_collection, false, 1);
	}

	HashSet<StringName> enum_constants;
	List<StringName> enums;
	Variant::get_enums_for_type(p_type, &enums);
	for (const StringName &enum_name : enums) {
		List<StringName> enumerations;
		Variant::get_enumerations_for_enum(p_type, enum_name, &enumerations);
		int count = 0;
		for (const StringName &enumeration : enumerations) {
			bool valid = false;
			Variant::get_enum_value(p_type, enum_name, enumeration, &valid);
			if (valid) {
				enum_constants.insert(enumeration);
				count++;
			}
		}
		const String display = String(enum_name) + ": enum type, " + itos(count) + (count == 1 ? " member" : " members");
		add_member(r_collection, MEMBER_ENUM, enum_name, "enum", display, "enum", 1);
	}

	List<StringName> constants;
	Variant::get_constants_for_type(p_type, &constants);
	for (const StringName &constant : constants) {
		if (enum_constants.has(constant)) {
			continue;
		}
		bool valid = false;
		const Variant constant_value = Variant::get_constant_value(p_type, constant, &valid);
		if (valid) {
			const String type = Variant::get_type_name(constant_value.get_type());
			add_member(r_collection, MEMBER_CONST, constant, type, String(constant) + ": " + type + " = " + format_value(constant_value), String(), 1);
		}
	}
}

void collect_builtin_enum(Variant::Type p_type, const StringName &p_enum, MemberCollection &r_collection) {
	List<StringName> enumerations;
	Variant::get_enumerations_for_enum(p_type, p_enum, &enumerations);
	for (const StringName &enumeration : enumerations) {
		bool valid = false;
		const int value = Variant::get_enum_value(p_type, p_enum, enumeration, &valid);
		if (valid) {
			add_member(r_collection, MEMBER_CONST, enumeration, "int", String(enumeration) + ": int = " + itos(value), String(), 1);
		}
	}
}

void collect_script_enum(const Ref<Script> &p_script, const StringName &p_enum, MemberCollection &r_collection) {
	HashMap<StringName, Variant> constants;
	p_script->get_constants(&constants);
	const Variant *enum_value = constants.getptr(p_enum);
	if (enum_value == nullptr || enum_value->get_type() != Variant::DICTIONARY) {
		return;
	}
	const Dictionary values = *enum_value;
	const Array keys = values.keys();
	for (const Variant &key : keys) {
		const StringName name = key;
		const Variant value = values[key];
		add_member(r_collection, MEMBER_CONST, name, "int", String(name) + ": int = " + format_value(value), String(), 3);
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

void collect_object(Object *p_object, MemberCollection &r_collection, bool p_exclude_builtin) {
	if (!p_exclude_builtin) {
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
	}
	Ref<Script> script = p_object->get_script();
	if (script.is_valid()) {
		collect_script(script, r_collection);
	}
}

void collect_resolved_type(const ResolvedType &p_type, MemberCollection &r_collection, bool p_exclude_builtin) {
	switch (p_type.kind) {
		case RESOLVED_TYPE_SCRIPT:
			if (!p_exclude_builtin) {
				collect_native_class(p_type.script->get_instance_base_type(), r_collection, true);
			}
			collect_script(p_type.script, r_collection);
			break;
		case RESOLVED_TYPE_NATIVE:
			if (!p_exclude_builtin) {
				collect_native_class(p_type.native_class, r_collection, true);
			}
			break;
		case RESOLVED_TYPE_BUILTIN:
			if (!p_exclude_builtin) {
				collect_builtin_type(p_type.builtin_type, r_collection);
			}
			break;
		case RESOLVED_TYPE_NATIVE_ENUM:
			if (!p_exclude_builtin) {
				collect_native_enum(p_type.native_class, p_type.enum_name, r_collection);
			}
			break;
		case RESOLVED_TYPE_BUILTIN_ENUM:
			if (!p_exclude_builtin) {
				collect_builtin_enum(p_type.builtin_type, p_type.enum_name, r_collection);
			}
			break;
		case RESOLVED_TYPE_SCRIPT_ENUM:
			collect_script_enum(p_type.script, p_type.enum_name, r_collection);
			break;
		default:
			break;
	}
}

String get_resolved_type_name(const ResolvedType &p_type) {
	switch (p_type.kind) {
		case RESOLVED_TYPE_SCRIPT:
			return get_script_display_name(p_type.script);
		case RESOLVED_TYPE_NATIVE:
			return p_type.native_class;
		case RESOLVED_TYPE_BUILTIN:
			return Variant::get_type_name(p_type.builtin_type);
		case RESOLVED_TYPE_NATIVE_ENUM:
			return String(p_type.native_class) + "." + String(p_type.enum_name);
		case RESOLVED_TYPE_BUILTIN_ENUM:
			return Variant::get_type_name(p_type.builtin_type) + "." + String(p_type.enum_name);
		case RESOLVED_TYPE_SCRIPT_ENUM:
			return get_script_display_name(p_type.script) + "." + String(p_type.enum_name);
		default:
			return String();
	}
}

String get_resolved_declaration(const ResolvedType &p_type) {
	switch (p_type.kind) {
		case RESOLVED_TYPE_SCRIPT:
			return get_script_declaration(p_type.script);
		case RESOLVED_TYPE_NATIVE:
			return get_native_declaration(p_type.native_class);
		case RESOLVED_TYPE_BUILTIN:
			return "builtin " + Variant::get_type_name(p_type.builtin_type);
		case RESOLVED_TYPE_NATIVE_ENUM:
		case RESOLVED_TYPE_BUILTIN_ENUM:
		case RESOLVED_TYPE_SCRIPT_ENUM:
			return "enum " + get_resolved_type_name(p_type) + ", extends int";
		default:
			return String();
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

#ifdef MODULE_GDSCRIPT_ENABLED
const GDScriptParser::FunctionNode *find_function_node(const GDScriptParser::ClassNode *p_class, const String &p_fqcn, const StringName &p_method) {
	if (p_class->fqcn == p_fqcn && p_class->has_function(p_method)) {
		return p_class->get_member(p_method).function;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			const GDScriptParser::FunctionNode *function = find_function_node(member.m_class, p_fqcn, p_method);
			if (function) {
				return function;
			}
		}
	}
	return nullptr;
}

void populate_method_location(ResolvedMethod &r_method) {
	GDScript *gdscript = Object::cast_to<GDScript>(r_method.owner_script.ptr());
	if (gdscript == nullptr) {
		return;
	}
	GDScript *root_script = gdscript->get_root_script();
	if (root_script == nullptr) {
		return;
	}
	String path = root_script->get_path();
	const int subresource_separator = path.find("::");
	if (subresource_separator >= 0) {
		path = path.left(subresource_separator);
	}

	GDScriptParser parser;
	if (parser.parse(root_script->get_source_code(), path, false) != OK) {
		return;
	}
	const GDScriptParser::FunctionNode *function = find_function_node(parser.get_tree(), gdscript->get_fully_qualified_name(), r_method.info.name);
	if (function == nullptr && parser.get_tree()->has_function(r_method.info.name)) {
		function = parser.get_tree()->get_member(r_method.info.name).function;
	}
	if (function == nullptr) {
		return;
	}
	r_method.start_line = function->start_line;
	r_method.end_line = function->end_line;
	if (ProjectSettings::get_singleton()) {
		path = ProjectSettings::get_singleton()->globalize_path(path);
	}
	r_method.source_path = path;
}
#endif

bool find_declared_method(const ResolvedType &p_owner, const StringName &p_name, ResolvedMethod &r_method) {
	if (p_owner.kind == RESOLVED_TYPE_SCRIPT) {
#ifdef MODULE_GDSCRIPT_ENABLED
		Ref<Script> current = p_owner.script;
		while (current.is_valid()) {
			GDScript *gdscript = Object::cast_to<GDScript>(current.ptr());
			if (gdscript) {
				GDScriptFunction *const *function = gdscript->debug_get_member_functions().getptr(p_name);
				if (function) {
					r_method.valid = true;
					r_method.info = (*function)->get_method_info();
					r_method.owner_script = current;
					populate_method_location(r_method);
					return true;
				}
			}
			current = current->get_base_script();
		}
#endif
		List<MethodInfo> script_methods;
		p_owner.script->get_script_method_list(&script_methods);
		for (const MethodInfo &method : script_methods) {
			if (method.name == p_name) {
				r_method.valid = true;
				r_method.info = method;
				r_method.owner_script = p_owner.script;
				return true;
			}
		}
		const StringName native_base = p_owner.script->get_instance_base_type();
		if (ClassDB::get_method_info(native_base, p_name, &r_method.info)) {
			r_method.valid = true;
			r_method.builtin = true;
			r_method.native_owner = native_base;
			return true;
		}
	}
	if (p_owner.kind == RESOLVED_TYPE_NATIVE && ClassDB::get_method_info(p_owner.native_class, p_name, &r_method.info)) {
		r_method.valid = true;
		r_method.builtin = true;
		r_method.native_owner = p_owner.native_class;
		return true;
	}
	if (p_owner.kind == RESOLVED_TYPE_BUILTIN) {
		Variant value;
		Callable::CallError call_error;
		Variant::construct(p_owner.builtin_type, value, nullptr, 0, call_error);
		if (call_error.error == Callable::CallError::CALL_OK) {
			List<MethodInfo> methods;
			value.get_method_list(&methods);
			for (const MethodInfo &method : methods) {
				if (method.name == p_name) {
					r_method.valid = true;
					r_method.builtin = true;
					r_method.info = method;
					return true;
				}
			}
		}
	}
	return false;
}

String get_method_declaration(const ResolvedMethod &p_method) {
	String declaration = format_method_signature(p_method.info);
	if (p_method.info.flags & METHOD_FLAG_STATIC) {
		declaration = "static " + declaration;
	}
	if (p_method.builtin) {
		declaration = "(built-in) " + declaration;
	}
	return declaration;
}

String get_method_location(const ResolvedMethod &p_method) {
	if (p_method.owner_script.is_null() || p_method.start_line <= 0) {
		return String();
	}
	return "defined at " + p_method.source_path + ":" + itos(p_method.start_line) + "-" + itos(MAX(p_method.start_line, p_method.end_line));
}

bool enrich_native_property(const StringName &p_class, const StringName &p_name, PropertyInfo &r_property) {
	if (!ClassDB::get_property_info(p_class, p_name, &r_property)) {
		return false;
	}
	const StringName getter = ClassDB::get_property_getter(p_class, p_name);
	MethodInfo getter_info;
	if (!getter.is_empty() && ClassDB::get_method_info(p_class, getter, &getter_info)) {
		const PropertyInfo &return_info = getter_info.return_val;
		if (return_info.type == r_property.type && (!return_info.class_name.is_empty() || (return_info.usage & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD)))) {
			const StringName property_name = r_property.name;
			r_property = return_info;
			r_property.name = property_name;
		}
	}
	return true;
}

bool find_declared_property(const ResolvedType &p_owner, const StringName &p_name, PropertyInfo &r_property) {
	if (p_owner.kind == RESOLVED_TYPE_SCRIPT) {
		List<PropertyInfo> static_properties;
		p_owner.script->get_property_list(&static_properties);
		bool found_static = false;
		for (const PropertyInfo &property : static_properties) {
			if (property.name == p_name && (property.usage & PROPERTY_USAGE_SCRIPT_VARIABLE)) {
				r_property = property;
				found_static = true;
			}
		}
		if (found_static) {
			return true;
		}

		List<PropertyInfo> properties;
		p_owner.script->get_script_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (property.name == p_name && !should_skip_property(property)) {
				r_property = property;
				return true;
			}
		}
		return enrich_native_property(p_owner.script->get_instance_base_type(), p_name, r_property);
	}
	if (p_owner.kind == RESOLVED_TYPE_NATIVE) {
		return enrich_native_property(p_owner.native_class, p_name, r_property);
	}
	if (p_owner.kind == RESOLVED_TYPE_BUILTIN && Variant::has_member(p_owner.builtin_type, p_name)) {
		r_property = PropertyInfo(Variant::get_member_type(p_owner.builtin_type, p_name), p_name);
		return true;
	}
	return false;
}

bool resolve_enum_name(const StringName &p_qualified_name, const StringName &p_native_context, ResolvedType &r_type) {
	String qualified_name = p_qualified_name;
	if (qualified_name.begins_with("_")) {
		qualified_name = qualified_name.substr(1);
	}
	String owner_name;
	String enum_name;
	const int separator = qualified_name.rfind(".");
	if (separator >= 0) {
		owner_name = qualified_name.left(separator);
		enum_name = qualified_name.substr(separator + 1);
	} else {
		owner_name = p_native_context;
		enum_name = qualified_name;
	}

	if (ClassDB::class_exists(owner_name) && ClassDB::has_enum(owner_name, enum_name)) {
		r_type.kind = RESOLVED_TYPE_NATIVE_ENUM;
		r_type.native_class = owner_name;
		r_type.enum_name = enum_name;
		return true;
	}
	if (ScriptServer::is_global_class(owner_name)) {
		Dictionary load_error;
		Ref<Script> script;
		if (load_named_class(owner_name, script, load_error)) {
			HashMap<StringName, Variant> constants;
			script->get_constants(&constants);
			if (constants.has(enum_name) && constants[enum_name].get_type() == Variant::DICTIONARY) {
				r_type.kind = RESOLVED_TYPE_SCRIPT_ENUM;
				r_type.script = script;
				r_type.enum_name = enum_name;
				return true;
			}
		}
	}
	const Variant::Type builtin_type = Variant::get_type_by_name(owner_name);
	if (builtin_type < Variant::VARIANT_MAX && Variant::has_enum(builtin_type, enum_name)) {
		r_type.kind = RESOLVED_TYPE_BUILTIN_ENUM;
		r_type.builtin_type = builtin_type;
		r_type.enum_name = enum_name;
		return true;
	}
	return false;
}

bool resolve_property_type(const PropertyInfo &p_property, const StringName &p_native_context, ResolvedType &r_type) {
	r_type = ResolvedType();
	if (p_property.type == Variant::INT && (p_property.usage & (PROPERTY_USAGE_CLASS_IS_ENUM | PROPERTY_USAGE_CLASS_IS_BITFIELD)) && !p_property.class_name.is_empty()) {
		if (resolve_enum_name(p_property.class_name, p_native_context, r_type)) {
			return true;
		}
	}
	if (p_property.type == Variant::OBJECT) {
		StringName class_name = p_property.class_name;
		if (class_name.is_empty() && p_property.hint == PROPERTY_HINT_RESOURCE_TYPE) {
			class_name = p_property.hint_string.get_slice(",", 0);
		}
		if (ScriptServer::is_global_class(class_name)) {
			Dictionary load_error;
			if (!load_named_class(class_name, r_type.script, load_error)) {
				return false;
			}
			r_type.kind = RESOLVED_TYPE_SCRIPT;
			return true;
		}
		if (ClassDB::class_exists(class_name)) {
			r_type.kind = RESOLVED_TYPE_NATIVE;
			r_type.native_class = class_name;
			return true;
		}
		return false;
	}
	if (p_property.type > Variant::NIL && p_property.type < Variant::VARIANT_MAX) {
		r_type.kind = RESOLVED_TYPE_BUILTIN;
		r_type.builtin_type = p_property.type;
		return true;
	}
	return false;
}

bool resolve_metadata_target(const String &p_target, ResolvedType &r_type, ResolvedMethod &r_method, Dictionary &r_error) {
	const int separator = p_target.find_char('.');
	if (separator <= 0 || separator == p_target.length() - 1) {
		return false;
	}
	const String class_name = p_target.substr(0, separator);
	if (ScriptServer::is_global_class(class_name)) {
		Dictionary load_error;
		Ref<Script> script;
		if (!load_named_class(class_name, script, load_error)) {
			r_error = load_error;
			return true;
		}
		r_type.kind = RESOLVED_TYPE_SCRIPT;
		r_type.script = script;
	} else if (ClassDB::class_exists(class_name)) {
		r_type.kind = RESOLVED_TYPE_NATIVE;
		r_type.native_class = class_name;
	} else {
		const Variant::Type builtin_type = Variant::get_type_by_name(class_name);
		if (builtin_type >= Variant::VARIANT_MAX) {
			return false;
		}
		r_type.kind = RESOLVED_TYPE_BUILTIN;
		r_type.builtin_type = builtin_type;
	}

	Vector<StringName> member_path;
	if (!parse_member_path(p_target.substr(separator + 1), member_path)) {
		r_error = make_error("invalid_member_path", "Invalid nested list target: " + p_target);
		return true;
	}
	for (int i = 0; i < member_path.size(); i++) {
		const StringName member = member_path[i];
		if (i == member_path.size() - 1 && find_declared_method(r_type, member, r_method)) {
			return true;
		}
		PropertyInfo property;
		if (!find_declared_property(r_type, member, property)) {
			r_error = make_error("member_type_not_found", "No declared type information was found for: " + p_target);
			return true;
		}
		const StringName native_context = r_type.kind == RESOLVED_TYPE_SCRIPT ? r_type.script->get_instance_base_type() : r_type.native_class;
		ResolvedType next_type;
		if (!resolve_property_type(property, native_context, next_type)) {
			r_error = make_error("member_type_unresolved", "The declared type could not be resolved for: " + p_target);
			return true;
		}
		r_type = next_type;
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
		return make_error("target_required", "list requires a node path, class name, script path, or nested target.");
	}

	const String type_filter = String(p_options.get("filter", String())).strip_edges();
	const bool exclude_builtin = p_options.get("exclude_builtin", false);
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
	String declaration;
	String location;
	String resolution = "metadata";
	Variant target_holder;
	if (target_name.begins_with("/")) {
		if ((bool)p_options.get("metadata_only", false)) {
			return make_error("live_node_required", "A running game is required to list a runtime node path: " + target_name);
		}
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
		declaration = get_object_declaration(node);
		resolution = "live";
		collect_object(node, collection, exclude_builtin);
	} else if (target_name.begins_with("res://")) {
		Ref<Script> script = ResourceLoader::load(target_name, "Script");
		if (script.is_null() || !script->is_valid()) {
			return make_error("script_load_failed", "Could not load script resource: " + target_name);
		}
		target_kind = "class";
		target_type = get_script_display_name(script);
		declaration = get_script_declaration(script);
		if (!exclude_builtin) {
			collect_native_class(script->get_instance_base_type(), collection, true);
		}
		collect_script(script, collection);
	} else if (ScriptServer::is_global_class(target_name)) {
		Ref<Script> script;
		Dictionary load_error;
		if (!load_named_class(target_name, script, load_error)) {
			return load_error;
		}
		target_kind = "class";
		target_type = target_name;
		declaration = get_script_declaration(script);
		if (!exclude_builtin) {
			collect_native_class(script->get_instance_base_type(), collection, true);
		}
		collect_script(script, collection);
	} else if (ClassDB::class_exists(target_name)) {
		target_kind = "class";
		target_type = target_name;
		declaration = get_native_declaration(target_name);
		if (!exclude_builtin) {
			collect_native_class(target_name, collection, true);
		}
	} else {
		bool resolved = false;
		Dictionary live_error;
		if (!(bool)p_options.get("metadata_only", false)) {
			Object *nested_object = nullptr;
			if (resolve_nested_target(target_name, target_holder, nested_object, live_error) && live_error.is_empty()) {
				Script *nested_script_object = Object::cast_to<Script>(nested_object);
				if (nested_script_object) {
					Ref<Script> nested_script(nested_script_object);
					target_kind = "class";
					target_type = get_script_display_name(nested_script);
					declaration = get_script_declaration(nested_script);
					if (!exclude_builtin) {
						collect_native_class(nested_script->get_instance_base_type(), collection, true);
					}
					collect_script(nested_script, collection);
				} else {
					Node *nested_node = Object::cast_to<Node>(nested_object);
					target_kind = nested_node ? "node" : "object";
					target_type = get_object_display_type(nested_object);
					declaration = get_object_declaration(nested_object);
					collect_object(nested_object, collection, exclude_builtin);
				}
				resolution = "live";
				resolved = true;
			}
		}

		if (!resolved) {
			ResolvedType metadata_type;
			ResolvedMethod metadata_method;
			Dictionary metadata_error;
			if (resolve_metadata_target(target_name, metadata_type, metadata_method, metadata_error) && metadata_error.is_empty()) {
				if (metadata_method.valid) {
					target_kind = "function";
					target_type = format_property_type(metadata_method.info.return_val, true);
					declaration = get_method_declaration(metadata_method);
					location = get_method_location(metadata_method);
				} else {
					target_kind = "type";
					target_type = get_resolved_type_name(metadata_type);
					declaration = get_resolved_declaration(metadata_type);
					collect_resolved_type(metadata_type, collection, exclude_builtin);
				}
				resolved = true;
			} else if (!metadata_error.is_empty()) {
				return metadata_error;
			}
		}
		if (!resolved) {
			return live_error.is_empty() ? make_error("target_not_found", "No runtime node or named/native class was found: " + target_name) : live_error;
		}
	}

	Dictionary response;
	response["ok"] = true;
	response["command"] = "list";
	response["target"] = target_name;
	response["target_kind"] = target_kind;
	response["target_type"] = target_type;
	response["declaration"] = declaration;
	response["resolution"] = resolution;
	if (!location.is_empty()) {
		response["location"] = location;
	}
	response["sections"] = make_sections(collection, included_kinds, type_filter);
	return response;
}

} // namespace WGodotMemberList
