// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"
#include "modules/gdscript/gdscript_analyzer.h"
#include "scene/resources/wgodot_animation_values.h"
#include "scene/animation/wgodot_animation_property.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

static_assert(int(WAnimationValueKind::Generic) < 64, "Animation binding kind masks require a wider representation.");

void WGodotCppEmitter::emit_animation_bindings() {
	// Standalone/shared animations do not have a unique scene owner. Index
	// declared script properties, then resolve only paths actually used by the
	// exported animations. Inherited properties share their owner's binding.
	struct Owner {
		const WGodotCppProject::Class *script = nullptr;
		Parser::DataType datatype;
	};
	HashMap<StringName, Vector<Owner>> owners;
	for (const auto &entry : project.get_classes()) {
		if (entry.node->wgodot_static_class || entry.node->wgodot_is_interface || !files.has(entry.cpp_name + ".h") || !ClassDB::is_parent_class(native_base(entry.node->self_type), SNAME("Node"))) {
			continue;
		}
		for (const auto &member : entry.node->members) {
			if (member.type == Parser::ClassNode::Member::VARIABLE && !member.variable->is_static) {
				owners[member.get_name()].push_back({ &entry, GDScriptAnalyzer::type_from_metatype(entry.node->self_type) });
			}
		}
	}
	LocalVector<StringName> native_classes;
	ClassDB::get_class_list(native_classes);
	native_classes.sort();
	for (const StringName &name : native_classes) {
		if (ClassDB::get_api_type(name) != ClassDB::API_CORE || !ClassDB::is_parent_class(name, SNAME("Node"))) {
			continue;
		}
		List<PropertyInfo> properties;
		ClassDB::get_property_list(name, &properties, true);
		Parser::DataType datatype;
		datatype.kind = Parser::DataType::NATIVE;
		datatype.type_source = Parser::DataType::ANNOTATED_EXPLICIT;
		datatype.native_type = name;
		for (const PropertyInfo &property : properties) {
			if (!(property.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP | PROPERTY_USAGE_CATEGORY)) && !ClassDB::get_property_setter(name, property.name).is_empty()) {
				owners[property.name].push_back({ nullptr, datatype });
			}
		}
	}
	Vector<String> paths;
	for (const auto &entry : animation_property_paths) {
		paths.push_back(path_text(entry.key));
	}
	paths.sort();
	class_dependencies.clear();
	class_native_headers.clear();
	class_call_headers.clear();
	current_function = nullptr;
	temporary_index = 0;
	String definitions;
	String registrations;
	int binding_index = 0;
	Parser native_parser;
	GDScriptAnalyzer native_analyzer(&native_parser);
	WGodotCppProject::Class native_context;
	native_context.script_path = "<native animation bindings>";
	for (const String &text : paths) {
		const NodePath path(text);
		if (WGodotAnimationProperty::supports_engine_path(path)) {
			const uint64_t kinds = animation_property_paths[path];
			for (int kind = int(WAnimationValueKind::Bool); kind <= int(WAnimationValueKind::Generic); kind++) {
				if ((kinds & (uint64_t(1) << kind)) && !WGodotAnimationProperty::supports_engine_value(path, WAnimationValueKind(kind))) {
					diagnostics.push_back("Unsupported typed shader animation value for: " + text);
				}
			}
			continue; // The engine supplies typed, per-instance shader bindings.
		}
		const auto *candidates = owners.getptr(path.get_subname(0));
		if (!candidates) {
			diagnostics.push_back("Animation property has no statically typed owner: " + text);
			continue;
		}
		String resolver;
		const String resolver_name = "resolve_animation_" + itos(binding_index);
		for (const Owner &owner : *candidates) {
			const auto *entry = owner.script ? owner.script : &native_context;
			current_class = entry;
			function_failed = false;
			GDScriptAnalyzer *analyzer = owner.script ? project.find_analyzer(entry->script_path) : &native_analyzer;
			Parser::SubscriptNode origin;
			origin.start_line = owner.script ? entry->node->start_line : 0;
			WGodotGDScriptPropertyPath resolved;
			resolved.path = path;
			Parser::DataType base = owner.datatype;
			for (const StringName &name : path.get_subnames()) {
				WGodotGDScriptPropertyPath::Segment segment;
				segment.name = name;
				segment.base_type = base;
				if (!analyzer->wgodot_resolve_property_path_segment(segment, &origin) || segment.datatype.is_variant() || !segment.datatype.is_hard_type() || native_only(segment.datatype)) {
					break;
				}
				resolved.segments.push_back(segment);
				base = segment.datatype;
			}
			if (resolved.segments.size() != path.get_subname_count()) {
				continue; // A similarly named field on another type is not this target.
			}
			// Node::get_node_and_resource resolves leading Resource references
			// before set_indexed starts. Do not invoke their setters on writeback.
			int writeback_start = 0;
			for (int i = 0; i + 1 < resolved.segments.size(); i++) {
				if (!ClassDB::is_parent_class(native_base(resolved.segments[i].datatype), SNAME("Resource"))) {
					break;
				}
				writeback_start = i + 1;
			}
			bool writable = true;
			for (int i = writeback_start; i < resolved.segments.size(); i++) {
				const auto &segment = resolved.segments[i];
				writable &= !segment.datatype.is_read_only && !segment.datatype.is_constant;
				if (segment.member.type == Parser::ClassNode::Member::VARIABLE) {
					writable &= !segment.member.variable->wgodot_readonly && !segment.member.variable->is_static;
				}
			}
			if (!writable) {
				continue;
			}
			const auto &destination = resolved.segments[resolved.segments.size() - 1].datatype;
			String value_type;
			if (destination.kind == Parser::DataType::ENUM) {
				value_type = "int64_t";
			} else if (destination.kind == Parser::DataType::BUILTIN) {
				switch (destination.builtin_type) {
#define W_ANIMATION_CPP_TYPE(m_name, m_type, m_variant) \
					case Variant::m_variant: value_type = #m_type; break;
					W_ANIMATION_VALUE_TYPES(W_ANIMATION_CPP_TYPE)
#undef W_ANIMATION_CPP_TYPE
					default: break;
				}
			} else if (ClassDB::is_parent_class(native_base(destination), SNAME("Resource"))) {
				value_type = "Ref<Resource>";
			}
			if (value_type.is_empty()) {
				continue;
			}
			const String target_type = class_name(owner.datatype, &origin);
			const String descriptor = "animation_property_" + itos(binding_index++);
			const String read = property_path_accessor(resolved, &origin, false, writeback_start, true, value_type);
			const String write = property_path_accessor(resolved, &origin, true, writeback_start, true, value_type);
			String target_id;
			if (writeback_start > 0) {
				WGodotGDScriptPropertyPath resource_path = resolved;
				resource_path.segments.resize(writeback_start);
				const String getter = property_path_accessor(resource_path, &origin, false);
				target_id = ",\n[](Object *object) -> ObjectID {\n\tauto resource = (" + getter + ")(static_cast<" + target_type + " *>(object));\n\tauto *target = WGodotNative::object_pointer(resource);\n\treturn target ? target->get_instance_id() : ObjectID();\n}";
			}
			definitions += "// " + owner.datatype.to_string() + " " + text + "\n";
			definitions += "static constexpr WGodotAnimationPropertyTyped<" + value_type + "> " + descriptor + " {\n" + read + ",\n" + write + target_id + "\n};\n";
			resolver += "\tif (Object::cast_to<" + target_type + ">(target)) {\n\t\treturn &" + descriptor + ";\n\t}\n";
		}
		if (!resolver.is_empty()) {
			definitions += "static const WGodotAnimationProperty *" + resolver_name + "(Object *target) {\n" + resolver + "\treturn nullptr;\n}\n";
			registrations += "\tWGodotAnimationProperty::register_path(NodePath(" + quoted(text) + "), &" + resolver_name + ");\n";
		} else {
			diagnostics.push_back("Animation property cannot be bound through concrete typed accessors: " + text);
		}
	}
	current_class = nullptr;
	HashSet<String> headers(class_native_headers);
	for (const String &header : class_call_headers) {
		headers.insert(header);
	}
	for (const String &dependency : class_dependencies) {
		headers.insert(dependency + ".h");
	}
	Vector<String> ordered_headers;
	for (const String &header : headers) {
		ordered_headers.push_back(header);
	}
	ordered_headers.sort();
	String source = "// Code generated by WGodot gd2cpp. DO NOT EDIT.\n#include \"scene/animation/wgodot_animation_property.h\"\n#include \"modules/wgodot/native/wgodot_native_support.h\"\n";
	for (const String &header : ordered_headers) {
		source += "#include \"" + header + "\"\n";
	}
	source += "\nnamespace WGodotGame {\n" + definitions + "} // namespace WGodotGame\n";
	source += "\nvoid register_main_game_animation_bindings() {\n\tusing namespace WGodotGame;\n\tWGodotAnimationProperty::require_native_bindings();\n" + registrations + "}\n";
	store_file("animation_bindings.cpp", source);
}
