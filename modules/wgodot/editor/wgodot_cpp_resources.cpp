// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "modules/wgodot/codecs/resource_codec.h"
#include "wgodot_cpp_names.h"
#include "wgodot_export_scene.h"
#include "wgodot_native_resource_policy.h"
#include "wgodot_native_resource_selection.h"
#include "wgodot_resource_rewrite.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_uid.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/os.h"
#include "core/string/string_builder.h"
#include "scene/resources/packed_scene.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

class WGodotCppResources {
	WGodotCppEmitter *emitter;
	WGodotExportScene scenes;
	Parser::ClassNode origin;
	HashMap<String, Ref<Resource>> definitions;
	Vector<String> paths;
	HashMap<const Resource *, int> embedded;
	Vector<Ref<Resource>> graph;
	struct StoredProperty {
		StringName name;
		Variant value;
	};
	Vector<Vector<StoredProperty>> stored_properties;
	HashMap<const PackedScene *, Ref<SceneState>> prepared_scenes;
	HashMap<int, Vector<int>> resource_dependencies;
	int collecting_resource = -1;
	HashMap<String, String> dependencies;
	StringBuilder declarations;
	StringBuilder body;
	String source_path;
	int temporary = 0;

	class CodecWriter : public WResourceCodec::NativeWriter {
		WGodotCppResources &owner;

	public:
		explicit CodecWriter(WGodotCppResources &p_owner) : owner(p_owner) {}
		String value(const Variant &p_value) override { return owner.value(p_value); }
		String symbol() override { return "codec_" + itos(owner.temporary++); }
		void include(const String &p_header) override { owner.emitter->class_native_headers.insert(p_header); }
		void line(const String &p_line) override { owner.body += "\t" + p_line + "\n"; }
	};

	void error(const String &p_message) {
		emitter->diagnostics.push_back(source_path + ": " + p_message);
	}

	const WGodotCppProject::Class *script_class(const Ref<Script> &p_script) {
		if (p_script.is_null()) {
			return nullptr;
		}
		Parser *parser = emitter->project.find_parser(p_script->get_path());
		const auto *result = parser ? emitter->project.find_class(parser->get_tree()) : nullptr;
		if (!result) {
			error("No generated native class for script " + p_script->get_path());
		}
		return result;
	}

	String native_class(const StringName &p_name) {
		Parser::DataType type;
		type.kind = Parser::DataType::NATIVE;
		type.native_type = p_name;
		return emitter->class_name(type, &origin);
	}

	const WGodotCppProject::Class *resource_class(const Ref<Resource> &p_resource) {
		const Ref<Script> script = p_resource->get_script();
		return script_class(script);
	}

	const WGodotCppProject::Class *node_class(const Ref<SceneState> &p_state, const NodePath &p_path) {
		for (int i = 0; i < p_state->get_node_count(); i++) {
			if (p_state->get_node_path(i) != p_path) {
				continue;
			}
			for (int j = 0; j < p_state->get_node_property_count(i); j++) {
				if (p_state->get_node_property_name(i, j) == SNAME("script")) {
					const Ref<Script> script = p_state->get_node_property_value(i, j);
					return script_class(script);
				}
			}
			if (!p_state->get_node_type(i).is_empty()) {
				return nullptr;
			}
		}
		int closest = -1;
		String prefix;
		const String path = path_text(p_path);
		for (int i = 0; i < p_state->get_node_count(); i++) {
			const Ref<PackedScene> instance = p_state->get_node_instance(i);
			if (instance.is_null()) {
				continue;
			}
			const String candidate = path_text(p_state->get_node_path(i));
			if ((candidate == "." || candidate == path || path.begins_with(candidate + "/")) && (closest < 0 || candidate.length() > prefix.length())) {
				closest = i;
				prefix = candidate;
			}
		}
		if (closest >= 0) {
			const Ref<PackedScene> instance = p_state->get_node_instance(closest);
			const String relative = prefix == "." ? path : path == prefix ? "."
																		  : path.substr(prefix.length() + 1);
			return node_class(instance->get_state(), NodePath(relative));
		}
		const Ref<SceneState> base = p_state->get_base_scene_state();
		return base.is_valid() ? node_class(base, p_path) : nullptr;
	}

	const Parser::VariableNode *field(const WGodotCppProject::Class *p_class, const StringName &p_name) {
		if (!p_class) {
			return nullptr;
		}
		const auto *owner = emitter->member_owner(p_class->node, p_name);
		if (!owner) {
			return nullptr;
		}
		const auto &member = owner->node->get_member(p_name);
		return member.type == Parser::ClassNode::Member::VARIABLE ? member.variable : nullptr;
	}

	void collect(const Variant &p_value) {
		if (p_value.get_type() == Variant::OBJECT) {
			const Ref<Resource> resource = p_value;
			if (resource.is_null() || Object::cast_to<Script>(resource.ptr())) {
				return;
			}
			const String path = resource->get_path();
			const String root = path.get_slice("::", 0);
			if (!path.is_empty() && root != source_path) {
				emitter->resource_id(path);
				return;
			}
			if (!embedded.has(resource.ptr())) {
				embedded.insert(resource.ptr(), graph.size());
				graph.push_back(resource);
			}
			resource_dependencies[collecting_resource].push_back(embedded[resource.ptr()]);
		} else if (p_value.get_type() == Variant::ARRAY) {
			const Array array = p_value;
			for (const Variant &value : array) {
				collect(value);
			}
		} else if (p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary dictionary = p_value;
			for (const KeyValue<Variant, Variant> &entry : dictionary) {
				collect(entry.key);
				collect(entry.value);
			}
		} else if (p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME || p_value.get_type() == Variant::PACKED_STRING_ARRAY) {
			auto register_path = [&](const String &p_path) {
				emitter->resource_id(p_path);
				return p_path;
			};
			WGodotResourceRewrite::value(p_value, PropertyInfo(), register_path);
			if (p_value.get_type() != Variant::PACKED_STRING_ARRAY && String(p_value).contains("#include")) {
				(void)WGodotResourceRewrite::text(p_value, source_path, register_path);
			}
		}
	}

	String value(const Variant &p_value) {
		if (p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME) {
			auto path = [&](const String &p_original) { return "res://" + itos(emitter->resource_id(p_original)); };
			String rewritten = WGodotResourceRewrite::value(p_value, PropertyInfo(), path);
			if (rewritten.contains("#include")) {
				rewritten = WGodotResourceRewrite::text(rewritten, source_path, path);
			}
			const String literal = "String::utf8(" + quoted(rewritten) + ")";
			return p_value.get_type() == Variant::STRING_NAME ? "StringName(" + literal + ")" : literal;
		}
		if (p_value.get_type() >= Variant::PACKED_BYTE_ARRAY && p_value.get_type() <= Variant::PACKED_VECTOR4_ARRAY) {
			const String type = Variant::get_type_name(p_value.get_type());
			const uint64_t count = p_value.get_indexed_size();
			if (count == 0) {
				return type + "()";
			}
			const String name = "data_" + itos(temporary++);
			const String element_type = name + "_element";
			declarations += "using " + element_type + " = std::remove_pointer_t<decltype(std::declval<" + type + ">().ptrw())>;\n";
			Vector<String> elements;
			for (uint64_t i = 0; i < count; i++) {
				bool valid;
				bool out_of_bounds;
				elements.push_back(element_type + "(" + value(p_value.get_indexed(i, valid, out_of_bounds)) + ")");
			}
			declarations += "static const " + element_type + " " + name + "[] = {\n\t" + String(",\n\t").join(elements) + "\n};\n";
			return "WGodotNative::packed_data<" + type + ">(" + name + ")";
		}
		if (p_value.get_type() == Variant::OBJECT) {
			const Ref<Resource> resource = p_value;
			if (resource.is_null()) {
				return "Variant()";
			}
			if (Object::cast_to<Script>(resource.ptr())) {
				error("A stored Script resource cannot be used in a native game: " + resource->get_path());
				return "Variant()";
			}
			if (const int *index = embedded.getptr(resource.ptr())) {
				return "resource_" + itos(*index);
			}
			const String path = resource->get_path();
			if (const String *existing = dependencies.getptr(path)) {
				return *existing;
			}
			const String name = "dependency_" + itos(dependencies.size());
			dependencies.insert(path, name);
			body += "\tRef<Resource> " + name + " = ResourceLoader::load(WGodotResourcePaths::to_path(" + itos(emitter->resource_id(path)) + "), String(), dependency_cache);\n\tERR_FAIL_COND_V(" + name + ".is_null(), Ref<Resource>());\n";
			return name;
		}
		if (p_value.get_type() == Variant::ARRAY) {
			const Array array = p_value;
			const String name = "array_" + itos(temporary++);
			body += "\tArray " + name + ";\n";
			if (array.is_typed()) {
				StringName type = array.get_typed_class_name();
				const Ref<Script> script = array.get_typed_script();
				if (const auto *game = script_class(script)) {
					type = game->cpp_name;
					emitter->class_dependencies.insert(game->cpp_name);
				}
				body += "\t" + name + ".set_typed(Variant::Type(" + itos(array.get_typed_builtin()) + "), StringName(" + quoted(type) + "), Variant());\n";
			}
			for (const Variant &element : array) {
				const String item = value(element);
				body += "\t" + name + ".push_back(" + item + ");\n";
			}
			return name;
		}
		if (p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary dictionary = p_value;
			const String name = "dictionary_" + itos(temporary++);
			body += "\tDictionary " + name + ";\n";
			if (dictionary.is_typed()) {
				StringName key = dictionary.get_typed_key_class_name();
				StringName item = dictionary.get_typed_value_class_name();
				if (const auto *game = script_class(dictionary.get_typed_key_script())) {
					key = game->cpp_name;
				}
				if (const auto *game = script_class(dictionary.get_typed_value_script())) {
					item = game->cpp_name;
				}
				body += "\t" + name + ".set_typed(Variant::Type(" + itos(dictionary.get_typed_key_builtin()) + "), StringName(" + quoted(key) + "), Variant(), Variant::Type(" + itos(dictionary.get_typed_value_builtin()) + "), StringName(" + quoted(item) + "), Variant());\n";
			}
			for (const KeyValue<Variant, Variant> &entry : dictionary) {
				const String key = value(entry.key);
				const String item = value(entry.value);
				body += "\t" + name + ".set(" + key + ", " + item + ");\n";
			}
			return name;
		}
		return emitter->literal(p_value, &origin);
	}

	Variant encode(const Variant &p_value, const Parser::DataType &p_type) {
		if (p_type.wgodot_resource_path) {
			return emitter->resource_id(p_value);
		}
		if (p_type.builtin_type == Variant::ARRAY && p_type.has_container_element_type(0) && p_value.get_type() == Variant::ARRAY) {
			const Array source = p_value;
			Array result;
			for (const Variant &item : source) {
				result.push_back(encode(item, p_type.get_container_element_type(0)));
			}
			return result;
		}
		if (p_type.builtin_type == Variant::DICTIONARY && p_type.has_container_element_type(0) && p_type.has_container_element_type(1) && p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary source = p_value;
			Dictionary result;
			for (const KeyValue<Variant, Variant> &entry : source) {
				result[encode(entry.key, p_type.get_container_element_type(0))] = encode(entry.value, p_type.get_container_element_type(1));
			}
			return result;
		}
		return p_value;
	}

	String typed_value(const Variant &p_value, const Parser::DataType &p_type) {
		const String convert = emitter->is_warray(p_type) || emitter->is_wdictionary(p_type) ? "deserialize_value" : "convert";
		return "WGodotNative::" + convert + "<" + emitter->type(p_type, &origin) + ">(" + value(encode(p_value, p_type)) + ")";
	}

	void assign(const String &p_target, const StringName &p_native_type, const WGodotCppProject::Class *p_class, const StringName &p_name, const Variant &p_value) {
		if (const auto *variable = field(p_class, p_name)) {
			const Parser::DataType type = emitter->variable_type(variable);
			const String argument = typed_value(p_value, type);
			body += "\t" + p_target + "->write_" + symbol(p_name) + "(" + argument + ");\n";
			return;
		}
		const String argument = value(p_value);
		const StringName setter = ClassDB::get_property_setter(p_native_type, p_name);
		const MethodBind *method = ClassDB::get_method(p_native_type, setter);
		const String key = method ? String(method->get_instance_class()) + "::" + String(method->get_name()) : String();
		const int property_index = ClassDB::get_property_index(p_native_type, p_name);
		if (method && !method->is_vararg() && method->get_argument_count() == (property_index >= 0 ? 2 : 1) && emitter->native_methods.has(key)) {
			Vector<String> arguments;
			arguments.push_back("static_cast<" + native_class(method->get_instance_class()) + " *>(" + p_target + ".ptr())");
			const int index = property_index;
			if (index >= 0) {
				arguments.push_back("WGodotNative::convert<" + emitter->native_argument_type(method->get_argument_info(0), &origin) + ">(" + itos(index) + ")");
			}
			arguments.push_back("WGodotNative::convert<" + emitter->native_argument_type(method->get_argument_info(index >= 0 ? 1 : 0), &origin) + ">(" + argument + ")");
			body += "\t" + emitter->native_access(method, &origin) + "(" + String(", ").join(arguments) + ");\n";
		} else {
			body += "\t" + p_target + "->set(StringName(" + quoted(p_name) + "), " + argument + ");\n";
		}
	}

	String property(const StringName &p_native_type, const WGodotCppProject::Class *p_class, const StringName &p_name) {
		const String descriptor = "property_" + itos(temporary++);
		String read;
		String write;
		if (const auto *variable = field(p_class, p_name)) {
			emitter->class_dependencies.insert(p_class->cpp_name);
			const String receiver = "static_cast<" + p_class->cpp_name + " *>(p_node)";
			read = "WGodotNative::serialized_value(" + receiver + "->read_" + symbol(p_name) + "())";
			write = receiver + "->write_" + symbol(p_name) + "(WGodotNative::deserialize_value<" + emitter->type(emitter->variable_type(variable), variable) + ">(p_value))";
		} else {
			const MethodBind *getter = ClassDB::get_method(p_native_type, ClassDB::get_property_getter(p_native_type, p_name));
			const MethodBind *setter = ClassDB::get_method(p_native_type, ClassDB::get_property_setter(p_native_type, p_name));
			auto mapped = [&](const MethodBind *p_method) {
				return p_method && !p_method->is_vararg() && emitter->native_methods.has(String(p_method->get_instance_class()) + "::" + String(p_method->get_name()));
			};
			if (!mapped(getter) || !mapped(setter)) {
				return String();
			}
			const int index = ClassDB::get_property_index(p_native_type, p_name);
			const int index_args = index >= 0 ? 1 : 0;
			if (getter->get_argument_count() != index_args || setter->get_argument_count() != index_args + 1) {
				return String();
			}
			const String receiver = "static_cast<" + native_class(p_native_type) + " *>(p_node)";
			String getter_args = receiver;
			String setter_args = receiver;
			if (index >= 0) {
				getter_args += ", WGodotNative::convert<" + emitter->native_argument_type(getter->get_argument_info(0), &origin) + ">(" + itos(index) + ")";
				setter_args += ", WGodotNative::convert<" + emitter->native_argument_type(setter->get_argument_info(0), &origin) + ">(" + itos(index) + ")";
			}
			setter_args += ", WGodotNative::convert<" + emitter->native_argument_type(setter->get_argument_info(index_args), &origin) + ">(p_value)";
			read = "Variant(" + emitter->native_access(getter, &origin) + "(" + getter_args + "))";
			write = emitter->native_access(setter, &origin) + "(" + setter_args + ")";
		}
		declarations += "static const WGodotSceneProperty " + descriptor + " = {\n\t[](const Node *p_source) -> Variant { Node *p_node = const_cast<Node *>(p_source); return " + read + "; },\n\t[](Node *p_node, const Variant &p_value) { " + write + "; }\n};\n";
		return descriptor;
	}

	bool connection(const Ref<SceneState> &p_state, int p_index, const String &p_state_name) {
		const auto *source = node_class(p_state, p_state->get_connection_source(p_index));
		const auto *target = node_class(p_state, p_state->get_connection_target(p_index));
		const StringName method_name = p_state->get_connection_method(p_index);
		const StringName signal_name = p_state->get_connection_signal(p_index);
		const auto *method_owner = target ? emitter->member_owner(target->node, method_name) : nullptr;
		const auto *signal_owner = source ? emitter->member_owner(source->node, signal_name) : nullptr;
		const Parser::FunctionNode *method = method_owner && method_owner->node->get_member(method_name).type == Parser::ClassNode::Member::FUNCTION ? method_owner->node->get_member(method_name).function : nullptr;
		const Parser::SignalNode *signal = signal_owner && signal_owner->node->get_member(signal_name).type == Parser::ClassNode::Member::SIGNAL ? signal_owner->node->get_member(signal_name).signal : nullptr;
		if (!method && !signal) {
			return false;
		}
		String callback;
		if (method) {
			emitter->class_dependencies.insert(method_owner->cpp_name);
			const auto *slot_owner = method_owner;
			while (!method->is_static && slot_owner->node->base_type.kind == Parser::DataType::CLASS) {
				const auto *parent = emitter->member_owner(slot_owner->node->base_type.class_type, method_name);
				if (!parent || parent->node->get_member(method_name).type != Parser::ClassNode::Member::FUNCTION) {
					break;
				}
				slot_owner = parent;
			}
			const String slot = (slot_owner->cpp_name + "::" + String(method_name)).sha256_text().substr(0, 16);
			callback = "WGodotNative::method_callable(";
			if (!method->is_static) {
				callback += "static_cast<" + method_owner->cpp_name + " *>(p_target), ";
			}
			callback += "&" + method_owner->cpp_name + "::m_" + symbol(method_name) + ", UINT64_C(0x" + slot + "))";
			Vector<String> parameters;
			Vector<String> defaults;
			for (const auto *parameter : method->parameters) {
				parameters.push_back(emitter->type(parameter->type_constraint, parameter));
				if (parameter->initializer) {
					defaults.push_back(emitter->converted(parameter->initializer, parameter->type_constraint, parameter));
				}
			}
			if (!defaults.is_empty()) {
				callback += ".with_defaults(std::make_tuple(" + String(", ").join(defaults) + "))";
			}
			// Signal connections discard results, including native coroutine tasks.
			// Adapt before binding/unbinding so neither path needs an engine result ABI.
			callback = "WGodotNative::WCallable<void(" + String(", ").join(parameters) + ")>::adapt(" + callback + ")";
			const Array binds = p_state->get_connection_binds(p_index);
			if (binds.size() > int(method->parameters.size())) {
				error("Scene connection binds exceed the native target's parameter count.");
				return false;
			}
			Vector<String> arguments;
			for (int i = 0; i < binds.size(); i++) {
				const auto *parameter = method->parameters[method->parameters.size() - binds.size() + i];
				arguments.push_back("WGodotNative::deserialize_value<" + emitter->type(parameter->type_constraint, parameter) + ">(p_binds[" + itos(i) + "])");
			}
			if (!arguments.is_empty()) {
				callback += ".bind(" + String(", ").join(arguments) + ")";
			}
		} else {
			callback = "Callable(p_target, StringName(" + quoted(method_name) + ")).bindv(p_binds)";
		}
		const int unbinds = p_state->get_connection_unbinds(p_index);
		String connect;
		if (signal) {
			emitter->class_dependencies.insert(signal_owner->cpp_name);
			Vector<String> arguments;
			for (const auto *parameter : signal->parameters) {
				arguments.push_back(emitter->type(parameter->type_constraint, parameter));
			}
			const String signature = "WGodotNative::WCallable<void(" + String(", ").join(arguments) + ")>";
			if (!method) {
				if (unbinds > 0) {
					callback += ".unbind(" + itos(unbinds) + ")";
				}
				callback = signature + "::from_callable(" + callback + ")";
			} else if (unbinds > 0) {
				callback = signature + "::unbind<" + itos(unbinds) + ">(" + callback + ")";
			}
			connect = "auto signal = static_cast<" + signal_owner->cpp_name + " *>(p_source)->s_" + symbol(signal_name) + ".signal();\n\t\tauto callback = " + callback + ";\n\t\tif (!signal.is_connected(callback)) { signal.connect(callback, p_flags); }";
		} else {
			callback += ".to_callable()";
			if (unbinds > 0) {
				callback += ".unbind(" + itos(unbinds) + ")";
			}
			connect = "auto callback = " + callback + ";\n\t\tconst StringName signal(" + quoted(signal_name) + ");\n\t\tif (!p_source->is_connected(signal, callback)) { p_source->connect(signal, callback, p_flags); }";
		}
		const String descriptor = "connection_" + itos(temporary++);
		declarations += "static const WGodotSceneConnection " + descriptor + " = {\n\t[](Node *p_source, Node *p_target, const Array &p_binds, int p_flags) {\n\t\t" + connect + "\n\t}\n};\n";
		body += "\t" + p_state_name + "->set_native_connection(" + itos(p_index) + ", &" + descriptor + ");\n";
		return true;
	}

	void scene(const Ref<PackedScene> &p_scene, const String &p_target) {
		const Ref<SceneState> state = prepared_scenes[p_scene.ptr()];
		Dictionary bundled = state->get_bundled_scene();
		PackedStringArray names = bundled["names"];
		const PackedInt32Array nodes = bundled["nodes"];
		PackedInt32Array output;
		Array variants = bundled["variants"];
		const int blank = names.size();
		names.push_back(String());
		HashSet<int> used_names;
		HashSet<int> used_variants;
		if (bundled.has("base_scene")) {
			used_variants.insert(int(bundled["base_scene"]));
		}
		const String state_name = "state_" + itos(temporary++);
		StringBuilder attachments;
		int offset = 0;
		for (int i = 0; i < state->get_node_count(); i++) {
			const int start = offset;
			const int parent = nodes[offset++];
			const int owner = nodes[offset++];
			int type = nodes[offset++];
			const int name = nodes[offset++];
			const int instance = nodes[offset++];
			if (instance >= 0) {
				used_variants.insert(instance & SceneState::FLAG_MASK);
			}
			const int count = nodes[offset++];
			const auto *game = node_class(state, state->get_node_path(i));
			used_names.insert(names.find(String(state->get_node_name(i))));
			if (type != SceneState::TYPE_INSTANTIATED) {
				String constructor;
				if (game) {
					emitter->class_dependencies.insert(game->cpp_name);
					type = names.size();
					names.push_back(game->cpp_name);
					constructor = game->cpp_name;
				} else if (emitter->native_headers.has(state->get_node_type(i))) {
					constructor = native_class(state->get_node_type(i));
				}
				if (!constructor.is_empty()) {
					attachments += "\t" + state_name + "->set_native_node(" + itos(i) + ", []() -> Node * { return memnew(" + constructor + "); });\n";
				}
				used_names.insert(type);
			}
			PackedInt32Array properties;
			for (int j = 0; j < count; j++) {
				int property_name = nodes[offset++];
				int property_value = nodes[offset++];
				const StringName original = state->get_node_property_name(i, j);
				if (original == SNAME("script")) {
					if (nodes[start + 2] == SceneState::TYPE_INSTANTIATED || instance >= 0) {
						error("Replacing the script on an inherited or instanced node is unsupported: " + path_text(state->get_node_path(i)));
					}
					continue;
				}
				const StringName native_type = game ? emitter->native_base(game->node->self_type) : state->get_node_type(i);
				const String descriptor = property(native_type, game, original);
				if (!descriptor.is_empty()) {
					property_name = (property_name & ~SceneState::FLAG_PROP_NAME_MASK) | blank;
					attachments += "\t" + state_name + "->set_native_property(" + itos(i) + ", " + itos(properties.size() / 2) + ", &" + descriptor + ");\n";
					if (const auto *variable = field(game, original)) {
						if (!(property_name & SceneState::FLAG_PATH_PROPERTY_IS_NODE)) {
							property_value = variants.size();
							variants.push_back(encode(state->get_node_property_value(i, j), emitter->variable_type(variable)));
						}
					}
				} else {
					used_names.insert(property_name & SceneState::FLAG_PROP_NAME_MASK);
				}
				properties.push_back(property_name);
				properties.push_back(property_value);
				used_variants.insert(property_value);
			}
			output.push_back(parent);
			output.push_back(owner);
			output.push_back(type);
			output.push_back(name);
			output.push_back(instance);
			output.push_back(properties.size() / 2);
			output.append_array(properties);
			const int groups = nodes[offset++];
			output.push_back(groups);
			for (int j = 0; j < groups; j++) {
				const int group = nodes[offset++];
				used_names.insert(group);
				output.push_back(group);
			}
		}
		StringBuilder previous_body = std::move(body);
		body = StringBuilder();
		PackedInt32Array connections = bundled["conns"];
		offset = 0;
		for (int i = 0; i < state->get_connection_count(); i++) {
			if (connection(state, i, state_name)) {
				connections.set(offset + 2, blank);
				connections.set(offset + 3, blank);
			} else {
				used_names.insert(connections[offset + 2]);
				used_names.insert(connections[offset + 3]);
			}
			const int binds = connections[offset + 5];
			for (int j = 0; j < binds; j++) {
				used_variants.insert(connections[offset + 6 + j]);
			}
			offset += 7 + binds;
		}
		attachments += body.as_string();
		body = std::move(previous_body);
		for (int i = 0; i < names.size(); i++) {
			if (!used_names.has(i)) {
				names.set(i, String());
			}
		}
		bundled["names"] = names;
		bundled["nodes"] = output;
		bundled["conns"] = connections;
		for (int i = 0; i < variants.size(); i++) {
			if (!used_variants.has(i)) {
				variants[i] = Variant();
			}
		}
		bundled["variants"] = variants;
		const String definition = value(bundled);
		body += "\tRef<SceneState> " + state_name + " = " + p_target + "->get_state();\n\t" + state_name + "->set_bundled_scene(" + definition + ");\n";
		body += attachments.as_string();
	}

	void prepare_factory(const String &p_path, const Ref<Resource> &p_resource) {
		source_path = p_path;
		embedded.clear();
		graph.clear();
		resource_dependencies.clear();
		dependencies.clear();
		declarations = StringBuilder();
		body = StringBuilder();
		temporary = 0;
		emitter->class_call_headers.clear();
		emitter->class_native_headers.clear();
		emitter->class_dependencies.clear();
		embedded.insert(p_resource.ptr(), 0);
		graph.push_back(p_resource);
		for (int i = 0; i < graph.size(); i++) {
			collecting_resource = i;
			const Ref<PackedScene> packed = graph[i];
			if (packed.is_valid()) {
				const Ref<SceneState> state = scenes.filter(packed);
				prepared_scenes.insert(packed.ptr(), state);
				const Dictionary bundled = state->get_bundled_scene();
				collect(bundled["variants"]);
				for (int node = 0; node < state->get_node_count(); node++) {
					const auto *game = node_class(state, state->get_node_path(node));
					for (int property = 0; property < state->get_node_property_count(node); property++) {
						if (const auto *variable = field(game, state->get_node_property_name(node, property))) {
							encode(state->get_node_property_value(node, property), emitter->variable_type(variable));
						}
					}
				}
			}
			List<PropertyInfo> properties;
			graph[i]->get_property_list(&properties);
			const auto *game = resource_class(graph[i]);
			const WResourceCodec *codec = WResourceCodec::find(graph[i]->get_class());
			Vector<StoredProperty> stored;
			for (const PropertyInfo &property : properties) {
				if (game && emitter->project.is_editor_member(game->node, property.name)) {
					continue;
				}
				if (!(property.usage & PROPERTY_USAGE_STORAGE) || property.name == SNAME("script") || property.name == SNAME("resource_path") || (packed.is_valid() && property.name == SNAME("_bundled")) || (codec && codec->owns_property(property.name))) {
					continue;
				}
				const Variant value = graph[i]->get(property.name);
				collect(value);
				if (const auto *variable = field(game, property.name)) {
					encode(value, emitter->variable_type(variable));
				}
				stored.push_back({ property.name, value });
			}
			stored_properties.push_back(std::move(stored));
			if (codec) {
				// Numeric animation keys stay typed; only reference-bearing keys need discovery.
				List<PropertyInfo> references;
				WGodotNativeResourcePolicy::get_reference_properties(graph[i], references);
				for (const PropertyInfo &property : references) {
					if (codec->owns_property(property.name)) {
						collect(graph[i]->get(property.name));
					}
				}
			}
		}
	}

	void emit_factory(WGodotCppEmitter &p_worker) {
		emitter = &p_worker;
		const String &p_path = source_path;
		const Ref<Resource> resource = graph[0];
		WGodotCppProject::Class resource_origin{ source_path, String(), &origin };
		emitter->current_class = &resource_origin;
		emitter->function_failed = false;
		emitter->temporary_index = 0;
		emitter->class_call_headers.clear();
		emitter->class_native_headers.clear();
		emitter->class_dependencies.clear();
		for (int i = 0; i < graph.size(); i++) {
			const auto *game = resource_class(graph[i]);
			const bool mapped = game || emitter->native_headers.has(graph[i]->get_class());
			const String type = game ? game->cpp_name : mapped ? native_class(graph[i]->get_class())
															   : "Resource";
			if (game) {
				emitter->class_dependencies.insert(type);
			}
			const String constructor = mapped ? "WGodotNative::instantiate<" + type + ">()" : "Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(StringName(" + quoted(graph[i]->get_class()) + "))))";
			body += "\tRef<" + type + "> resource_" + itos(i) + " = " + constructor + ";\n\tERR_FAIL_COND_V(resource_" + itos(i) + ".is_null(), Ref<Resource>());\n";
			if (i > 0) {
				body += "\tresource_" + itos(i) + "->set_path_cache(" + quoted("res://" + itos(emitter->resource_id(p_path)) + "::" + itos(i)) + ");\n";
			}
		}
		// Allocate the whole graph, then populate dependencies before their owners.
		// Setters may inspect a child resource immediately (e.g. shader parameters).
		Vector<int> order;
		HashSet<int> visited;
		auto visit = [&](auto &&p_visit, int p_index) -> void {
			if (visited.has(p_index)) {
				return;
			}
			visited.insert(p_index);
			if (const Vector<int> *children = resource_dependencies.getptr(p_index)) {
				for (int child : *children) {
					p_visit(p_visit, child);
				}
			}
			order.push_back(p_index);
		};
		visit(visit, 0);
		for (int i : order) {
			const String target = "resource_" + itos(i);
			const Ref<PackedScene> packed = graph[i];
			if (packed.is_valid()) {
				scene(packed, target);
			}
			const auto *game = resource_class(graph[i]);
			for (const StoredProperty &property : stored_properties[i]) {
				assign(target, graph[i]->get_class(), game, property.name, property.value);
			}
			if (const WResourceCodec *codec = WResourceCodec::find(graph[i]->get_class())) {
				CodecWriter writer(*this);
				if (codec->write_native(graph[i].ptr(), target, writer) != OK) {
					error("Resource codec could not generate native construction for " + String(graph[i]->get_class()));
					return;
				}
			}

		}
		body += "\treturn resource_0;\n";
		String source = "// wgodot-changes::file\n// Generated native resource factory.\n#include \"core/io/resource_loader.h\"\n#include \"core/io/wgodot_resource_paths.h\"\n#include \"scene/resources/packed_scene.h\"\n#include \"modules/wgodot/native/wgodot_native_serialization.h\"\n#include \"modules/wgodot/native/wgodot_native_callback.h\"\n";
		HashSet<String> includes;
		for (const String &header : emitter->class_call_headers) {
			includes.insert(header);
		}
		for (const String &header : emitter->class_native_headers) {
			includes.insert(header);
		}
		for (const String &game : emitter->class_dependencies) {
			includes.insert(game + ".h");
		}
		Vector<String> ordered;
		for (const String &include : includes) {
			ordered.push_back(include);
		}
		ordered.sort();
		for (const String &include : ordered) {
			source += "#include " + quoted(include) + "\n";
		}
		const int64_t id = emitter->resource_id(p_path);
		source += "\nnamespace WGodotGame {\n" + declarations.as_string() + "Ref<Resource> create_resource_" + itos(id) + "(ResourceFormatLoader::CacheMode p_cache_mode) {\n\tconst auto dependency_cache = p_cache_mode == ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP || p_cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE_DEEP ? p_cache_mode : ResourceFormatLoader::CACHE_MODE_REUSE;\n" + body.as_string() + "}\n}\n";
		emitter->store_file("resource_" + itos(id) + ".cpp", source);
		const auto *game = resource_class(resource);
		emitter->compiled_resources[p_path] = game ? game->cpp_name : String(resource->get_class());
		emitter->current_class = nullptr;
	}

	void scan(const String &p_path) {
		const Ref<DirAccess> directory = DirAccess::open(p_path);
		if (directory.is_null() || directory->file_exists(".gdignore")) {
			return;
		}
		directory->list_dir_begin();
		for (String name = directory->get_next(); !name.is_empty(); name = directory->get_next()) {
			if (name.begins_with(".")) {
				continue;
			}
			const String path = p_path.path_join(name);
			if (directory->current_is_dir()) {
				scan(path);
				continue;
			}
			if (!emitter->project.get_target().includes(path)) {
				continue;
			}
			if (!WGodotNativeResourcePolicy::is_authored(path)) {
				continue;
			}
			paths.push_back(path);
		}
	}

	void seed_settings() {
		ProjectSettings *settings = ProjectSettings::get_singleton();
		const auto &overrides = emitter->project.get_target().get_settings();
		List<PropertyInfo> properties;
		settings->get_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (property.name.begins_with("autoload/")) {
				continue; // Autoload ownership and native wrappers are handled below.
			}
			const Variant original = overrides.has(property.name) ? overrides[property.name] : settings->get(property.name);
			if (!overrides.has(property.name) && original.get_type() == Variant::STRING && original == settings->property_get_revert(property.name)) {
				const String path = original;
				if ((path.begins_with("res://") || path.begins_with("uid://")) && !ResourceLoader::exists(path)) {
					continue; // Optional default resources need not exist in the project.
				}
			}
			WGodotResourceRewrite::value(original, property, [&](const String &p_path) {
				emitter->resource_id(p_path);
				return p_path;
			});
		}
	}

	void scene_dependencies(const Variant &p_value, const String &p_source, HashSet<const Resource *> &r_seen) {
		if (p_value.get_type() == Variant::OBJECT) {
			const Ref<Resource> resource = p_value;
			if (resource.is_null() || Object::cast_to<Script>(resource.ptr())) {
				return;
			}
			const String path = resource->get_path().get_slice("::", 0);
			if (!path.is_empty() && path != p_source) {
				emitter->resource_id(path);
				return;
			}
			if (r_seen.has(resource.ptr())) {
				return;
			}
			r_seen.insert(resource.ptr());
			const Ref<PackedScene> scene = resource;
			if (scene.is_valid()) {
				scene_dependencies(scenes.filter(scene)->get_bundled_scene(), p_source, r_seen);
				return;
			}
			List<PropertyInfo> properties;
			WGodotNativeResourcePolicy::get_reference_properties(resource, properties);
			for (const PropertyInfo &property : properties) {
				scene_dependencies(resource->get(property.name), p_source, r_seen);
			}
		} else if (p_value.get_type() == Variant::ARRAY) {
			const Array values = p_value;
			for (const Variant &value : values) {
				scene_dependencies(value, p_source, r_seen);
			}
		} else if (p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary values = p_value;
			for (const KeyValue<Variant, Variant> &entry : values) {
				scene_dependencies(entry.key, p_source, r_seen);
				scene_dependencies(entry.value, p_source, r_seen);
			}
		} else if (p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME || p_value.get_type() == Variant::PACKED_STRING_ARRAY) {
			// Scene placeholders and stored resource paths are serialized as strings.
			WGodotResourceRewrite::value(p_value, PropertyInfo(), [&](const String &p_path) {
				emitter->resource_id(p_path);
				return p_path;
			});
		}
	}

	void file_dependencies(const String &p_path) {
		List<String> external_paths;
		ResourceLoader::get_dependencies(p_path, &external_paths);
		for (const String &dependency : external_paths) {
			String path = dependency.get_slice("::", 0);
			if (path.begins_with("uid://")) {
				const ResourceUID::ID uid = ResourceUID::get_singleton()->text_to_id(path);
				path = ResourceUID::get_singleton()->has_id(uid) ? ResourceUID::get_singleton()->get_id_path(uid) : dependency.get_slice("::", 2);
			}
			if (path.is_empty()) {
				emitter->diagnostics.push_back("Cannot resolve dependency of " + p_path + ": " + dependency);
				continue;
			}
			if (path.is_relative_path()) {
				path = p_path.get_base_dir().path_join(path);
			}
			emitter->resource_id(path);
		}
	}

	void generate_reachable() {
		WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
		const int concurrency = MIN(emitter->jobs, pool->get_thread_count());
		Vector<WGodotCppEmitter *> workers;
		for (int i = 0; i < concurrency; i++) {
			workers.push_back(memnew(WGodotCppEmitter(*emitter)));
		}
		struct Batch {
			Vector<WGodotCppResources *> factories;
			WGodotCppEmitter *const *workers = nullptr;
			void generate(uint32_t p_index, void *) {
				factories[p_index]->emit_factory(*workers[p_index]);
			}
		} batch;
		batch.workers = workers.ptr();
		uint64_t completed = 0;
		uint64_t preparation_msec = 0;
		uint64_t generation_msec = 0;
		uint64_t last_progress = OS::get_singleton()->get_ticks_msec();
		bool failed = false;
		auto flush = [&]() {
			if (batch.factories.is_empty()) {
				return;
			}
			// Preparation is complete. The project, resource catalog and resources
			// are read-only until the group joins; no loaders or script getters run here.
			const uint64_t started = OS::get_singleton()->get_ticks_msec();
			const auto group = pool->add_template_group_task(&batch, &Batch::generate, static_cast<void *>(nullptr), batch.factories.size(), concurrency, true, "Generate native resource factories");
			pool->wait_for_group_task_completion(group);
			generation_msec += OS::get_singleton()->get_ticks_msec() - started;
			for (const auto *worker : workers) {
				failed |= !worker->diagnostics.is_empty() || worker->output_error != OK;
			}
			for (WGodotCppResources *factory : batch.factories) {
				memdelete(factory); // Release resource graphs on the preparing thread.
			}
			completed += batch.factories.size();
			batch.factories.clear();
			const uint64_t now = OS::get_singleton()->get_ticks_msec();
			if (now - last_progress >= 5000) {
				print_line(vformat("Native factories: %d generated (%d workers)", completed, concurrency));
				last_progress = now;
			}
		};
		auto queue = [&](const String &p_path, const Ref<Resource> &p_resource) {
			const uint64_t started = OS::get_singleton()->get_ticks_msec();
			WGodotCppResources *factory = memnew(WGodotCppResources(*emitter));
			factory->prepare_factory(p_path, p_resource);
			preparation_msec += OS::get_singleton()->get_ticks_msec() - started;
			batch.factories.push_back(factory);
			if (batch.factories.size() == concurrency) {
				flush();
			}
		};
		HashSet<String> visited;
		for (const auto &preload : emitter->preloads) {
			emitter->resource_id(preload.path);
		}
		while (emitter->diagnostics.is_empty()) {
			if (failed) {
				break;
			}
			Vector<String> pending;
			for (const KeyValue<String, int64_t> &entry : emitter->resource_ids) {
				if (!visited.has(entry.key)) {
					pending.push_back(entry.key);
				}
			}
			if (pending.is_empty()) {
				break;
			}
			pending.sort();
			for (const String &path : pending) {
				if (failed || !emitter->diagnostics.is_empty()) {
					break;
				}
				visited.insert(path);
				if (path.get_extension() == "gd") {
					continue;
				}
				if (const Ref<Resource> *synthetic = definitions.getptr(path)) {
					queue(path, *synthetic);
					continue;
				}
				if (!FileAccess::exists(path) && !ResourceLoader::exists(path)) {
					emitter->diagnostics.push_back("Required export resource does not exist: " + path);
					continue;
				}
				const bool authored = WGodotNativeResourcePolicy::is_authored(path);
				if (authored || ResourceLoader::get_resource_type(path) == "PackedScene") {
					const Ref<Resource> resource = ResourceLoader::load(path);
					if (resource.is_null()) {
						emitter->diagnostics.push_back("Cannot load required export resource: " + path);
						continue;
					}
					if (authored && WGodotNativeResourcePolicy::needs_factory(resource)) {
						paths.push_back(path);
						emitter->resource_sources[path] = FileAccess::get_sha256(path);
						queue(path, resource);
						continue;
					}
					const Ref<PackedScene> scene = resource;
					if (scene.is_valid()) {
						HashSet<const Resource *> seen;
						scene_dependencies(scene, path, seen);
						continue;
					}
				}
				file_dependencies(path);
			}
			flush();
		}
		flush();
		for (auto *worker : workers) {
			emitter->merge_resource_worker(*worker);
			memdelete(worker);
		}
		print_line(vformat("Native factories: %d generated (%d workers)", completed, concurrency));
		print_line(vformat("Native factory preparation: %d ms; parallel generation/write: %d ms", preparation_msec, generation_msec));
	}

public:
	explicit WGodotCppResources(WGodotCppEmitter &p_emitter) : emitter(&p_emitter), scenes(p_emitter.project, p_emitter.diagnostics) {}
	void generate() {
		WGodotNativeResourceSelection selection;
		String selection_error;
		if (selection.load(emitter->project.get_target().get_name(), selection_error) != OK) {
			emitter->diagnostics.push_back(selection_error);
			return;
		}
		emitter->resource_selection_fingerprint = selection.get_fingerprint();
		if (selection.exports_all()) {
			scan("res://");
		} else {
			seed_settings();
			Vector<String> roots;
			for (const String &path : selection.get_roots()) {
				roots.push_back(path);
			}
			roots.sort();
			for (const String &path : roots) {
				emitter->resource_id(path);
			}
		}
		for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &entry : ProjectSettings::get_singleton()->get_autoload_list()) {
			if (!emitter->project.get_target().includes(entry.value.path)) {
				continue;
			}
			if (entry.value.path.get_extension() != "gd") {
				if (!selection.exports_all()) {
					emitter->resource_id(entry.value.path);
				}
				continue;
			}
			const Ref<Script> script = ResourceLoader::load(entry.value.path);
			const auto *game = script_class(script);
			if (!game) {
				continue;
			}
			Ref<PackedScene> packed;
			packed.instantiate();
			const Ref<SceneState> state = packed->get_state();
			const int node = state->add_node(-1, -1, state->add_name(emitter->native_base(game->node->self_type)), state->add_name(entry.key), -1, -1, Node::UNIQUE_SCENE_ID_UNASSIGNED);
			state->add_node_property(node, state->add_name("script"), state->add_value(script));
			const String path = "res://.wgodot/native/autoload_" + String(entry.key).sha256_text().substr(0, 16) + ".tscn";
			definitions.insert(path, packed);
			paths.push_back(path);
		}
		paths.sort();
		for (const String &path : paths) {
			emitter->resource_id(path);
		}
		// Both selection modes use the same bounded factory pipeline.
		paths.clear();
		for (const auto &definition : definitions) {
			paths.push_back(definition.key);
		}
		generate_reachable();
		paths.sort();
		if (!emitter->diagnostics.is_empty() || emitter->output_error != OK) {
			return;
		}

		String registration = "// wgodot-changes::file\n#include \"core/io/wgodot_native_resources.h\"\n\nnamespace WGodotGame {\n";
		for (const String &path : paths) {
			registration += "Ref<Resource> create_resource_" + itos(emitter->resource_id(path)) + "(ResourceFormatLoader::CacheMode p_cache_mode);\n";
		}
		registration += "}\n\nvoid register_main_game_resources() {\n";
		if (!paths.is_empty()) {
			registration += "\tstatic const WGodotNativeResources::Definition definitions[] = {\n";
			for (const String &path : paths) {
				const int64_t id = emitter->resource_id(path);
				registration += "\t\t{" + itos(id) + ", " + quoted(emitter->compiled_resources[path]) + ", &WGodotGame::create_resource_" + itos(id) + "},\n";
			}
			registration += "\t};\n\tWGodotNativeResources::initialize(definitions, " + itos(paths.size()) + ");\n";
		}
		registration += "}\n";
		emitter->store_file("game_resources.cpp", registration);
	}
};

void WGodotCppEmitter::emit_resources() {
	compiled_resources.clear();
	compiled_resource_aliases.clear();
	resource_sources.clear();
	WGodotCppResources(*this).generate();
}
