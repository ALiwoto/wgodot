// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "core/object/class_db.h"
#include "modules/gdscript/gdscript_analyzer.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::emit_animation_bindings() {
	// Standalone/shared animations do not have a unique scene owner. Index
	// declared script properties, then resolve only paths actually used by the
	// exported animations. Inherited properties share their owner's binding.
	HashMap<StringName, Vector<const WGodotCppProject::Class *>> owners;
	for (const auto &entry : project.get_classes()) {
		if (entry.node->wgodot_static_class || entry.node->wgodot_is_interface || !files.has(entry.cpp_name + ".h") || !ClassDB::is_parent_class(native_base(entry.node->self_type), SNAME("Node"))) {
			continue;
		}
		for (const auto &member : entry.node->members) {
			if (member.type == Parser::ClassNode::Member::VARIABLE && !member.variable->is_static) {
				owners[member.get_name()].push_back(&entry);
			}
		}
	}
	Vector<String> paths;
	for (const NodePath &path : animation_property_paths) {
		paths.push_back(path_text(path));
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
	for (const String &text : paths) {
		const NodePath path(text);
		const auto *candidates = owners.getptr(path.get_subname(0));
		if (!candidates) {
			continue; // Native engine properties retain their existing dispatch.
		}
		String resolver;
		const String resolver_name = "resolve_animation_" + itos(binding_index);
		for (const auto *entry : *candidates) {
			current_class = entry;
			function_failed = false;
			GDScriptAnalyzer *analyzer = project.find_analyzer(entry->script_path);
			Parser::SubscriptNode origin;
			origin.start_line = entry->node->start_line;
			WGodotGDScriptPropertyPath resolved;
			resolved.path = path;
			Parser::DataType base = GDScriptAnalyzer::type_from_metatype(entry->node->self_type);
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
			const String descriptor = "animation_property_" + itos(binding_index++);
			const String read = property_path_accessor(resolved, &origin, false, writeback_start, true);
			const String write = property_path_accessor(resolved, &origin, true, writeback_start, true);
			String target_id;
			if (writeback_start > 0) {
				WGodotGDScriptPropertyPath resource_path = resolved;
				resource_path.segments.resize(writeback_start);
				const String getter = property_path_accessor(resource_path, &origin, false);
				target_id = ",\n[](Object *object) -> ObjectID {\n\tauto resource = (" + getter + ")(static_cast<" + entry->cpp_name + " *>(object));\n\tauto *target = WGodotNative::object_pointer(resource);\n\treturn target ? target->get_instance_id() : ObjectID();\n}";
			}
			definitions += "// " + String(entry->node->fqcn) + " " + text + "\n";
			definitions += "static constexpr WGodotAnimationProperty " + descriptor + " = {\n" + read + ",\n" + write + target_id + "\n};\n";
			resolver += "\tif (Object::cast_to<" + entry->cpp_name + ">(target)) {\n\t\treturn &" + descriptor + ";\n\t}\n";
		}
		if (!resolver.is_empty()) {
			definitions += "static const WGodotAnimationProperty *" + resolver_name + "(Object *target) {\n" + resolver + "\treturn nullptr;\n}\n";
			registrations += "\tWGodotAnimationProperty::register_path(NodePath(" + quoted(text) + "), &" + resolver_name + ");\n";
		}
	}
	current_class = nullptr;
	HashSet<String> headers = class_native_headers;
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
	source += "\nvoid register_main_game_animation_bindings() {\n\tusing namespace WGodotGame;\n" + registrations + "}\n";
	store_file("animation_bindings.cpp", source);
}
