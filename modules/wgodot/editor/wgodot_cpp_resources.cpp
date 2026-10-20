// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_memory_profile.h"
#include "wgodot_cpp_cache.h"
#include "modules/wgodot/codecs/native_literals.h"
#include "wgodot_cpp_source.h"
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
#include "scene/resources/wgodot_scene_types.h"
#include "scene/property_utils.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

class WGodotCppResources {
	using Value = WGodotCppExpression;
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
	WGodotCppSource declarations;
	WGodotCppSource body;
	String source_path;
	HashSet<String> preparation_paths;
	Dictionary contribution;
	int temporary = 0;
	bool emitting_scene = false;
	bool resolving_nodes = false;
	bool remapping_resources = true;
	String scene_data;
	StringBuilder scene_fields;
	HashMap<const Resource *, String> scene_resources;
	Vector<String> scene_resource_fields;

	void emit(const String &p_line) {
		if (emitting_scene) {
			declarations += p_line;
		} else {
			body += p_line;
		}
	}

	class CodecWriter : public WResourceCodec::NativeWriter {
		WGodotCppResources &owner;

	public:
		explicit CodecWriter(WGodotCppResources &p_owner) : owner(p_owner) {}
		String value(const Variant &p_value) override { return owner.value(p_value).code; }
		String symbol() override { return "codec_" + itos(owner.temporary++); }
		void include(const String &p_header) override { owner.emitter->class_native_headers.insert(p_header); }
		void line(const String &p_line) override { owner.emit("\t" + p_line + "\n"); }
		void animation_property(const NodePath &p_path, WAnimationValueKind p_kind) override {
			if (p_path.get_subname_count() > 0) {
				owner.emitter->animation_property_paths[NodePath(Vector<StringName>(), p_path.get_subnames(), false)] |= uint64_t(1) << int(p_kind);
			}
		}
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

	String resource_type(const Ref<Resource> &p_resource) {
		if (const auto *game = resource_class(p_resource)) {
			return game->cpp_name;
		}
		return emitter->native_headers.has(p_resource->get_class()) ? native_class(p_resource->get_class()) : "Resource";
	}

	const WGodotCppProject::Class *node_class(const Ref<SceneState> &p_state, const NodePath &p_path, StringName *r_native_type = nullptr) {
		const WGodotSceneTypes::Type type = WGodotSceneTypes::resolve(p_state, p_path);
		if (r_native_type) {
			*r_native_type = type.native;
		}
		return script_class(type.script);
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

	String rewritten_string(const String &p_value) {
		auto path = [&](const String &p_original) { return "res://" + itos(emitter->resource_id(p_original)); };
		String rewritten = WGodotResourceRewrite::value(p_value, PropertyInfo(), path);
		if (rewritten.contains("#include")) {
			rewritten = WGodotResourceRewrite::text(rewritten, source_path, path);
		}
		return rewritten;
	}

	template <class T>
	void packed_values(const Vector<T> &p_values, const String &p_name, const String &p_type) {
		declarations += "static constexpr " + p_type + " " + p_name + "[] = {\n";
		for (int i = 0; i < p_values.size(); i++) {
			declarations += "\t" + WGodotCppLiterals::packed_element(p_values[i]) + ",\n";
		}
		declarations += "};\n";
	}

	Value scene_resource(const Ref<Resource> &p_resource) {
		if (const String *field_name = scene_resources.getptr(p_resource.ptr())) {
			return Value(*field_name, "Ref<" + resource_type(p_resource) + ">");
		}
		const String type = "Ref<" + resource_type(p_resource) + ">";
		const String field_name = "resource_" + itos(scene_resources.size());
		scene_resources.insert(p_resource.ptr(), field_name);
		scene_resource_fields.push_back(field_name);
		scene_fields += "\t" + type + " " + field_name + ";\n";
		emitting_scene = false;
		const Value source = value(p_resource);
		body += "\t" + scene_data + "->" + field_name + " = " + emitter->convert_value(source, type) + ";\n";
		emitting_scene = true;
		return Value(field_name, type);
	}

	Value value(const Variant &p_value) {
		if (p_value.get_type() == Variant::STRING || p_value.get_type() == Variant::STRING_NAME) {
			const String rewritten = rewritten_string(p_value);
			const String literal = rewritten.is_empty() ? "String()" : "String::utf8(" + quoted(rewritten) + ")";
			return p_value.get_type() == Variant::STRING_NAME ? Value("StringName(" + literal + ")", "StringName") : Value(literal, "String");
		}
		if (p_value.get_type() >= Variant::PACKED_BYTE_ARRAY && p_value.get_type() <= Variant::PACKED_VECTOR4_ARRAY) {
			const String type = Variant::get_type_name(p_value.get_type());
			const uint64_t count = p_value.get_indexed_size();
			if (count == 0) {
				return Value(type + "()", type);
			}
			const String name = "data_" + itos(temporary++);
			const String element_type = emitter->packed_element_type(p_value.get_type());
			switch (p_value.get_type()) {
				case Variant::PACKED_BYTE_ARRAY: packed_values(PackedByteArray(p_value), name, element_type); break;
				case Variant::PACKED_INT32_ARRAY: packed_values(PackedInt32Array(p_value), name, element_type); break;
				case Variant::PACKED_INT64_ARRAY: packed_values(PackedInt64Array(p_value), name, element_type); break;
				case Variant::PACKED_FLOAT32_ARRAY: packed_values(PackedFloat32Array(p_value), name, element_type); break;
				case Variant::PACKED_FLOAT64_ARRAY: packed_values(PackedFloat64Array(p_value), name, element_type); break;
				case Variant::PACKED_VECTOR2_ARRAY: packed_values(PackedVector2Array(p_value), name, element_type); break;
				case Variant::PACKED_VECTOR3_ARRAY: packed_values(PackedVector3Array(p_value), name, element_type); break;
				case Variant::PACKED_VECTOR4_ARRAY: packed_values(PackedVector4Array(p_value), name, element_type); break;
				case Variant::PACKED_COLOR_ARRAY: packed_values(PackedColorArray(p_value), name, element_type); break;
				case Variant::PACKED_STRING_ARRAY: {
					declarations += "static constexpr const char *" + name + "[] = {\n";
					for (const String &item : PackedStringArray(p_value)) {
						declarations += "\t" + quoted(rewritten_string(item)) + ",\n";
					}
					declarations += "};\n";
					return Value("WGodotNative::packed_strings(" + name + ")", type);
				}
				default: ERR_FAIL_V(Value());
			}
			return Value("WGodotNative::packed_data<" + type + ">(" + name + ")", type);
		}
		if (p_value.get_type() == Variant::OBJECT) {
			const Ref<Resource> resource = p_value;
			if (emitting_scene && resource.is_valid()) {
				const Value reference = scene_resource(resource);
				return Value(remapping_resources ? "p_context.resource(" + reference.code + ")" : reference.code, reference.cpp_type);
			}
			if (resource.is_null()) {
				return Value("Variant()", "Variant");
			}
			if (Object::cast_to<Script>(resource.ptr())) {
				error("A stored Script resource cannot be used in a native game: " + resource->get_path());
				return Value("Variant()", "Variant");
			}
			if (const int *index = embedded.getptr(resource.ptr())) {
				return Value("resource_" + itos(*index), "Ref<" + resource_type(resource) + ">");
			}
			const String path = resource->get_path();
			if (const String *existing = dependencies.getptr(path)) {
				return Value(*existing, "Ref<Resource>");
			}
			const String name = "dependency_" + itos(dependencies.size());
			dependencies.insert(path, name);
			emit("\tRef<Resource> " + name + " = ResourceLoader::load(WGodotResourcePaths::to_path(" + itos(emitter->resource_id(path)) + "), String(), dependency_cache);\n\tERR_FAIL_COND_V(" + name + ".is_null(), Ref<Resource>());\n");
			return Value(name, "Ref<Resource>");
		}
		if (p_value.get_type() == Variant::ARRAY) {
			const Array array = p_value;
			const String name = "array_" + itos(temporary++);
			String array_type = "Array";
			if (array.is_typed()) {
				String element_type;
				const Ref<Script> script = array.get_typed_script();
				if (const auto *game = script_class(script)) {
					element_type = game->cpp_name;
					emitter->class_dependencies.insert(game->cpp_name);
				} else if (array.get_typed_builtin() == Variant::OBJECT) {
					element_type = native_class(array.get_typed_class_name().is_empty() ? StringName("Object") : array.get_typed_class_name());
				} else {
					element_type = emitter->native_argument_type(PropertyInfo(Variant::Type(array.get_typed_builtin()), String()), &origin);
				}
				array_type = "TypedArray<" + element_type + ">";
			}
			if (array.is_empty()) {
				return Value(array_type + "()", array_type);
			}
			emit("\t" + array_type + " " + name + ";\n");
			emit("\t" + name + ".resize(" + itos(array.size()) + ");\n");
			int index = 0;
			for (const Variant &element : array) {
				const String item = value(element).code;
				emit("\t" + name + "[" + itos(index++) + "] = " + item + ";\n");
			}
			return Value(name, array_type);
		}
		if (p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary dictionary = p_value;
			const String name = "dictionary_" + itos(temporary++);
			emit("\tDictionary " + name + ";\n");
			if (dictionary.is_typed()) {
				StringName key = dictionary.get_typed_key_class_name();
				StringName item = dictionary.get_typed_value_class_name();
				if (const auto *game = script_class(dictionary.get_typed_key_script())) {
					key = game->cpp_name;
				}
				if (const auto *game = script_class(dictionary.get_typed_value_script())) {
					item = game->cpp_name;
				}
				emit("\t" + name + ".set_typed(Variant::Type(" + itos(dictionary.get_typed_key_builtin()) + "), StringName(" + quoted(key) + "), Variant(), Variant::Type(" + itos(dictionary.get_typed_value_builtin()) + "), StringName(" + quoted(item) + "), Variant());\n");
			}
			for (const KeyValue<Variant, Variant> &entry : dictionary) {
				const String key = value(entry.key).code;
				const String item = value(entry.value).code;
				emit("\t" + name + ".set(" + key + ", " + item + ");\n");
			}
			return Value(name, "Dictionary");
		}
		return emitter->lower_literal(p_value, &origin);
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

	String typed_value(const Variant &p_value, const Parser::DataType &p_type, const String &p_existing = String()) {
		const String type = emitter->type(p_type, &origin);
		if (type == "Variant") {
			error("Native construction requires a concrete type, not Variant.");
			return "{}";
		}
		if (p_value.get_type() == Variant::NIL || (p_value.get_type() == Variant::OBJECT && Ref<Resource>(p_value).is_null())) {
			return type + "()";
		}
		if (emitting_scene && p_value.get_type() == Variant::OBJECT && type.begins_with("Ref<")) {
			const Value reference = scene_resource(Ref<Resource>(p_value));
			String expression = reference.code;
			if (remapping_resources) {
				expression = "p_context.resource(" + expression;
				if (!p_existing.is_empty()) {
					expression += ", [&]() { return " + p_existing + "; }";
				}
				expression += ")";
			}
			return emitter->convert_value(Value(expression, reference.cpp_type), type);
		}
		if (resolving_nodes && p_type.builtin_type == Variant::OBJECT && p_value.get_type() == Variant::NODE_PATH) {
			const String path = value(p_value).code;
			return emitter->convert_value(Value("p_node->get_node_or_null(" + path + ")", "Node *"), type);
		}
		if (emitter->is_warray(p_type)) {
			const Array array = p_value;
			if (array.is_empty()) {
				return type + "()";
			}
			const String name = "array_" + itos(temporary++);
			emit("\t" + type + " " + name + ";\n\t" + name + ".resize(" + itos(array.size()) + ");\n");
			for (int i = 0; i < array.size(); i++) {
				const Parser::DataType element_type = p_type.get_container_element_type(0);
				const String existing = p_existing.is_empty() ? String() : "(" + p_existing + ").size() > " + itos(i) + " ? (" + p_existing + ").get(" + itos(i) + ") : " + emitter->type(element_type, &origin) + "()";
				const String item = typed_value(array[i], element_type, existing);
				emit("\t" + name + ".set(" + itos(i) + ", " + item + ");\n");
			}
			return name;
		}
		if (emitter->is_wdictionary(p_type)) {
			const Dictionary dictionary = p_value;
			if (dictionary.is_empty()) {
				return type + "()";
			}
			const String name = "dictionary_" + itos(temporary++);
			emit("\t" + type + " " + name + ";\n");
			for (const KeyValue<Variant, Variant> &entry : dictionary) {
				const String key = typed_value(entry.key, p_type.get_container_element_type(0));
				const String existing = p_existing.is_empty() ? String() : "(" + p_existing + ").get(" + key + ")";
				const String item = typed_value(entry.value, p_type.get_container_element_type(1), existing);
				emit("\t" + name + ".set(" + key + ", " + item + ");\n");
			}
			return name;
		}
		return emitter->convert_value(value(encode(p_value, p_type)), type);
	}

	void assign(const String &p_target, const StringName &p_native_type, const WGodotCppProject::Class *p_class, const StringName &p_name, const Variant &p_value, bool p_node = false) {
		if (const auto *variable = field(p_class, p_name)) {
			const Parser::DataType type = emitter->variable_type(variable);
			const String argument = typed_value(p_value, type, emitting_scene ? p_target + "->read_" + symbol(p_name) + "()" : String());
			emit("\t" + p_target + "->write_" + symbol(p_name) + "(" + argument + ");\n");
			return;
		}
		const ClassDB::NativeProperty property = ClassDB::get_native_property(p_native_type, p_name, p_value.get_type());
		if (!property.collection_prefix.is_empty()) {
			const MethodBind *clear = property.setter;
			const String key = String(clear->get_instance_class()) + "::" + String(clear->get_name());
			if (p_value.get_type() != Variant::DICTIONARY || !emitter->native_methods.has(key) || clear->get_argument_count() != 0) {
				error("Invalid native property collection: " + String(p_native_type) + ":" + String(p_name));
				return;
			}
			emit("\t" + emitter->native_access(clear, &origin) + "(" + p_target + (p_node ? String() : ".ptr()") + ");\n");
			for (const KeyValue<Variant, Variant> &entry : Dictionary(p_value)) {
				if (entry.key.get_type() != Variant::STRING && entry.key.get_type() != Variant::STRING_NAME) {
					error("Native property collection keys must be names: " + String(p_name));
					return;
				}
				assign(p_target, p_native_type, p_class, property.collection_prefix + String(entry.key), entry.value, p_node);
			}
			return;
		}
		const MethodBind *method = property.setter;
		const String key = method ? String(method->get_instance_class()) + "::" + String(method->get_name()) : String();
		const int index_args = property.indexed ? 1 : 0;
		if (method && !method->is_vararg() && method->get_argument_count() == index_args + 1 && emitter->native_methods.has(key)) {
			Vector<String> arguments;
			const String pointer = p_node ? p_target : p_target + ".ptr()";
			// Concrete handles already convert to the accessor's base pointer.
			// Only the generic Ref<Resource> fallback needs a downcast.
			arguments.push_back(p_class || emitter->native_headers.has(p_native_type) ? pointer : "static_cast<" + native_class(method->get_instance_class()) + " *>(" + pointer + ")");
			if (property.indexed) {
				arguments.push_back(emitter->convert_value(value(property.index), emitter->native_argument_type(method->get_argument_info(0), &origin)));
			}
			const PropertyInfo parameter = method->get_argument_info(index_args);
			const String argument_type = emitter->native_argument_type(parameter, &origin);
			if (emitting_scene && argument_type == "Variant") {
				error("Scene property requires a typed native setter: " + String(p_native_type) + ":" + String(p_name));
				return;
			}
			Value argument;
			if (p_value.get_type() == Variant::NIL || (p_value.get_type() == Variant::OBJECT && Ref<Resource>(p_value).is_null())) {
				argument = Value(argument_type + "()", argument_type);
			} else if (resolving_nodes && parameter.type == Variant::OBJECT && p_value.get_type() == Variant::NODE_PATH) {
				argument = Value("p_node->get_node_or_null(" + value(p_value).code + ")", "Node *");
			} else if (emitting_scene && p_value.get_type() == Variant::OBJECT) {
				const Value reference = scene_resource(Ref<Resource>(p_value));
				const MethodBind *getter = property.getter;
				if (!getter || !emitter->native_methods.has(String(getter->get_instance_class()) + "::" + String(getter->get_name()))) {
					error("Local resource binding requires a typed getter: " + String(p_native_type) + ":" + String(p_name));
					return;
				}
				String getter_args = pointer;
				if (property.indexed) {
					getter_args += ", " + emitter->convert_value(value(property.index), emitter->native_argument_type(getter->get_argument_info(0), &origin));
				}
				for (int argument = index_args; argument < getter->get_argument_count(); argument++) {
					if (!getter->has_default_argument(argument)) {
						error("Native property getter requires an explicit argument: " + String(p_native_type) + ":" + String(p_name));
						return;
					}
					getter_args += ", " + emitter->convert_value(value(getter->get_default_argument(argument)), emitter->native_argument_type(getter->get_argument_info(argument), &origin));
				}
				const String existing = emitter->native_access(getter, &origin) + "(" + getter_args + ")";
				argument = Value("p_context.resource(" + reference.code + ", [&]() { return " + existing + "; })", reference.cpp_type);
			} else {
				argument = value(p_value);
			}
			// Preserve a concrete TypedArray at its native boundary; erasing it to
			// Array here would force the engine's typed setter to validate it again.
			arguments.push_back(parameter.type == Variant::ARRAY ? argument.code : emitter->convert_value(argument, emitter->native_argument_type(parameter, &origin)));
			emit("\t" + emitter->native_access(method, &origin) + "(" + String(", ").join(arguments) + ");\n");
		} else if (emitting_scene) {
			error("No typed native setter for " + String(p_native_type) + ":" + String(p_name));
		} else {
			const String argument = value(p_value).code;
			emit("\t" + p_target + "->set(StringName(" + quoted(p_name) + "), " + argument + ");\n");
		}
	}

	bool connection(const Ref<SceneState> &p_state, int p_index) {
		const auto *source = node_class(p_state, p_state->get_connection_source(p_index));
		StringName native_target;
		const auto *target = node_class(p_state, p_state->get_connection_target(p_index), &native_target);
		if (target) {
			native_target = emitter->native_base(target->node->self_type);
		}
		const StringName method_name = p_state->get_connection_method(p_index);
		const StringName signal_name = p_state->get_connection_signal(p_index);
		const auto *method_owner = target ? emitter->member_owner(target->node, method_name) : nullptr;
		const auto *signal_owner = source ? emitter->member_owner(source->node, signal_name) : nullptr;
		const Parser::FunctionNode *method = method_owner && method_owner->node->get_member(method_name).type == Parser::ClassNode::Member::FUNCTION ? method_owner->node->get_member(method_name).function : nullptr;
		const Parser::SignalNode *signal = signal_owner && signal_owner->node->get_member(signal_name).type == Parser::ClassNode::Member::SIGNAL ? signal_owner->node->get_member(signal_name).signal : nullptr;
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
				arguments.push_back(typed_value(binds[i], parameter->type_constraint));
			}
			if (!arguments.is_empty()) {
				callback += ".bind(" + String(", ").join(arguments) + ")";
			}
		} else {
			const MethodBind *native_method = ClassDB::get_method(native_target, method_name);
			const String key = native_method ? String(native_method->get_instance_class()) + "::" + String(method_name) : String();
			if (!native_method || native_method->is_vararg() || native_method->is_static() || !emitter->native_methods.has(key)) {
				error("Scene connection requires a mapped typed method: " + String(native_target) + ":" + String(method_name));
				return false;
			}
			const String owner = native_class(native_method->get_instance_class());
			Vector<String> parameters;
			Vector<String> arguments;
			arguments.push_back("target");
			for (int i = 0; i < native_method->get_argument_count(); i++) {
				const String type = emitter->native_argument_type(native_method->get_argument_info(i), &origin);
				if (type == "Variant") {
					error("Scene connection has a dynamic native argument: " + key);
					return false;
				}
				parameters.push_back(type + " argument_" + itos(i));
				arguments.push_back("argument_" + itos(i));
			}
			const String tail = parameters.is_empty() ? String() : ", " + String(", ").join(parameters);
			const String slot = key.sha256_text().substr(0, 16);
			callback = "WGodotNative::native_method_callable(static_cast<" + owner + " *>(p_target), +[](" + owner + " *target" + tail + ") { " + emitter->native_access(native_method, &origin) + "(" + String(", ").join(arguments) + "); }, UINT64_C(0x" + slot + "))";
			Vector<String> defaults;
			for (int i = native_method->get_argument_count() - native_method->get_default_argument_count(); i < native_method->get_argument_count(); i++) {
				defaults.push_back(emitter->convert_value(value(native_method->get_default_argument(i)), emitter->native_argument_type(native_method->get_argument_info(i), &origin)));
			}
			if (!defaults.is_empty()) {
				callback += ".with_defaults(std::make_tuple(" + String(", ").join(defaults) + "))";
			}
			const Array binds = p_state->get_connection_binds(p_index);
			if (binds.size() > native_method->get_argument_count()) {
				error("Scene connection has too many native bound arguments: " + key);
				return false;
			}
			Vector<String> bound;
			for (int i = 0; i < binds.size(); i++) {
				const int argument = native_method->get_argument_count() - binds.size() + i;
				bound.push_back(emitter->convert_value(value(binds[i]), emitter->native_argument_type(native_method->get_argument_info(argument), &origin)));
			}
			if (!bound.is_empty()) {
				callback += ".bind(" + String(", ").join(bound) + ")";
			}
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
			if (unbinds > 0) {
				callback = signature + "::unbind<" + itos(unbinds) + ">(" + callback + ")";
			} else {
				callback = signature + "::adapt(" + callback + ")";
			}
			connect = "auto signal = static_cast<" + signal_owner->cpp_name + " *>(p_source)->s_" + symbol(signal_name) + ".signal();\n\t\tauto callback = " + callback + ";\n\t\tif (!signal.is_connected(callback)) { signal.connect(callback, p_flags); }";
		} else {
			callback += ".to_callable()";
			if (unbinds > 0) {
				callback += ".unbind(" + itos(unbinds) + ")";
			}
			connect = "auto callback = " + callback + ";\n\t\tconst StringName signal(" + quoted(signal_name) + ");\n\t\tif (!p_source->is_connected(signal, callback)) { p_source->connect(signal, callback, p_flags); }";
		}
		emit("\t" + connect + "\n");
		return true;
	}

	void scene(const Ref<PackedScene> &p_scene, const String &p_target) {
		const Ref<SceneState> state = prepared_scenes[p_scene.ptr()];
		state->compact_storage();
		const String suffix = itos(temporary++);
		const String data_type = "SceneConstruction_" + suffix;
		scene_data = "construction_" + suffix;
		const String state_name = "state_" + suffix;
		scene_resources.clear();
		scene_resource_fields.clear();
		scene_fields = StringBuilder();
		body += "\tRef<" + data_type + "> " + scene_data + ";\n\t" + scene_data + ".instantiate();\n";
		declarations += "struct " + data_type + " final : WGodotSceneConstruction {\n";
		emitting_scene = true;
		for (int phase = 0; phase < 2; phase++) {
			resolving_nodes = phase == 1;
			const String method_name = resolving_nodes ? "resolve" : "apply";
			declarations += "\tvoid " + method_name + "(int p_index, Node *p_node, WGodotSceneContext &p_context) const override {\n\t\tswitch (p_index) {\n";
			for (int i = 0; i < state->get_node_count(); i++) {
				const Vector<String> deferred = state->get_node_deferred_nodepath_properties(i);
				StringName native_type;
				const auto *game = node_class(state, state->get_node_path(i), &native_type);
				if (game) {
					native_type = emitter->native_base(game->node->self_type);
					emitter->class_dependencies.insert(game->cpp_name);
				}
				bool opened = false;
				for (int j = 0; j < state->get_node_property_count(i); j++) {
					const StringName name = state->get_node_property_name(i, j);
					if (name == SNAME("script")) {
						if (state->get_node_type(i).is_empty() || state->get_node_instance(i).is_valid()) {
							error("Replacing a script on an inherited/instanced node is unsupported: " + path_text(state->get_node_path(i)));
						}
						continue;
					}
					if (deferred.has(name) != resolving_nodes) {
						continue;
					}
					if (native_type.is_empty() || state->is_node_instance_placeholder(i)) {
						error("Native scene properties require a resolved node type: " + path_text(state->get_node_path(i)));
						continue;
					}
					if (!opened) {
						const String type = game ? game->cpp_name : native_class(native_type);
						declarations += "\t\t\tcase " + itos(i) + ": {\n\t\t\t\tauto *node = static_cast<" + type + " *>(p_node);\n";
						opened = true;
					}
					assign("node", native_type, game, name, state->get_node_property_value(i, j), true);
				}
				if (opened) {
					declarations += "\t\t\t} break;\n";
				}
			}
			declarations += "\t\t\tdefault: break;\n\t\t}\n\t}\n";
		}
		resolving_nodes = false;
		remapping_resources = false;
		declarations += "\tvoid connect(int p_index, Node *p_source, Node *p_target, int p_flags) const override {\n\t\tswitch (p_index) {\n";
		for (int i = 0; i < state->get_connection_count(); i++) {
			declarations += "\t\t\tcase " + itos(i) + ": {\n";
			connection(state, i);
			declarations += "\t\t\t} break;\n";
		}
		declarations += "\t\t\tdefault: break;\n\t\t}\n\t}\n";
		declarations += "\tRef<WGodotSceneConstruction> duplicate(const Resource::NativeCopyContext &p_context) const override {\n\t\tRef<" + data_type + "> result;\n\t\tresult.instantiate();\n";
		for (const String &field : scene_resource_fields) {
			declarations += "\t\tresult->" + field + " = p_context.resource(" + field + ");\n";
		}
		declarations += "\t\treturn result;\n\t}\n" + scene_fields.as_string() + "};\n";
		remapping_resources = true;
		emitting_scene = false;
		body += "\tRef<SceneState> " + state_name + " = " + p_target + "->get_state();\n\t" + state_name + "->set_native_construction(" + scene_data + ");\n";

		// Serialized tables are read only by the exporter. Runtime metadata is
		// constructed through typed SceneState calls, without a Dictionary/Array.
		const Dictionary bundled = state->get_bundled_scene();
		const PackedInt32Array nodes = bundled["nodes"];
		const Array node_paths = bundled["node_paths"];
		const Array id_paths = bundled.get("id_paths", Array());
		for (int i = 0; i < node_paths.size(); i++) {
			const String path = value(node_paths[i]).code;
			const String ids = value(i < id_paths.size() ? id_paths[i] : Variant(PackedInt32Array())).code;
			body += "\t" + state_name + "->add_node_path(" + path + ", " + ids + ");\n";
		}
		HashMap<StringName, int> names;
		auto name_index = [&](const StringName &p_name) {
			if (const int *index = names.getptr(p_name)) {
				return *index;
			}
			const int index = names.size();
			names.insert(p_name, index);
			body += "\t" + state_name + "->add_name(StringName(" + quoted(p_name) + "));\n";
			return index;
		};
		int next_instance = 0;
		if (state->get_base_scene_state().is_valid()) {
			const String base = value(state->get_node_instance(0)).code;
			body += "\t" + state_name + "->add_native_instance(" + base + ");\n\t" + state_name + "->set_native_base_scene(" + itos(next_instance++) + ");\n";
		}
		int offset = 0;
		for (int i = 0; i < state->get_node_count(); i++) {
			const int parent = nodes[offset++];
			const int owner = nodes[offset++];
			const bool inherited = nodes[offset++] == SceneState::TYPE_INSTANTIATED;
			offset++; // Packed name/index; use the typed getters below.
			const int original_instance = nodes[offset++];
			const int property_count = nodes[offset++];
			offset += property_count * 2;
			const int group_count = nodes[offset++];
			offset += group_count;
			int instance = -1;
			if (original_instance >= 0) {
				instance = next_instance++;
				if (state->is_node_instance_placeholder(i)) {
					const String path = rewritten_string(state->get_node_instance_placeholder(i));
					body += "\t" + state_name + "->add_native_instance(Ref<PackedScene>(), String::utf8(" + quoted(path) + "));\n";
					instance |= SceneState::FLAG_INSTANCE_IS_PLACEHOLDER;
				} else {
					const String scene = value(state->get_node_instance(i)).code;
					body += "\t" + state_name + "->add_native_instance(" + scene + ");\n";
				}
			}
			const int type = inherited ? SceneState::TYPE_INSTANTIATED : name_index(state->get_node_type(i));
			const int name = name_index(state->get_node_name(i));
			body += "\t" + state_name + "->add_node(" + itos(parent) + ", " + itos(owner) + ", " + itos(type) + ", " + itos(name) + ", " + itos(instance) + ", " + itos(state->get_node_index(i)) + ", " + itos(state->get_node_unique_id(i)) + ");\n";
			if (!inherited) {
				const auto *game = node_class(state, state->get_node_path(i));
				const String constructor = game ? game->cpp_name : native_class(state->get_node_type(i));
				if (game) {
					emitter->class_dependencies.insert(game->cpp_name);
				}
				body += "\t" + state_name + "->set_native_node(" + itos(i) + ", &WGodotNative::scene_node<" + constructor + ">);\n";
			}
			for (const StringName &group : state->get_node_groups(i)) {
				const int group_index = name_index(group);
				body += "\t" + state_name + "->add_node_group(" + itos(i) + ", " + itos(group_index) + ");\n";
			}
		}
		for (const NodePath &path : state->get_editable_instances()) {
			body += "\t" + state_name + "->add_editable_instance(" + value(path).code + ");\n";
		}
		const PackedInt32Array connections = bundled["conns"];
		offset = 0;
		for (int i = 0; i < state->get_connection_count(); i++) {
			const int from = connections[offset];
			const int to = connections[offset + 1];
			const int signal = name_index(state->get_connection_signal(i));
			const int method = name_index(state->get_connection_method(i));
			body += "\t" + state_name + "->add_connection(" + itos(from) + ", " + itos(to) + ", " + itos(signal) + ", " + itos(method) + ", " + itos(state->get_connection_flags(i)) + ", " + itos(state->get_connection_unbinds(i)) + ", Vector<int>());\n";
			offset += 7 + connections[offset + 5];
		}
	}

	void prepare_factory(const String &p_path, const Ref<Resource> &p_resource) {
		source_path = p_path;
		embedded.clear();
		graph.clear();
		resource_dependencies.clear();
		dependencies.clear();
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
				const String type_error = WGodotSceneTypes::validate(state);
				if (!type_error.is_empty()) {
					error(type_error);
				}

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
				if (!(property.usage & PROPERTY_USAGE_STORAGE) || property.name == SNAME("script") || property.name == SNAME("resource_path") || (packed.is_valid() && property.name == SNAME("_bundled")) || (codec && codec->owns_native_property(property.name))) {
					continue;
				}
				const Variant value = graph[i]->get(property.name);
				const Variant default_value = PropertyUtils::get_property_default_value(graph[i].ptr(), property.name, nullptr, nullptr, false);
				if (default_value.get_type() != Variant::NIL && bool(Variant::evaluate(Variant::OP_EQUAL, value, default_value))) {
					continue;
				}
				if (property.type == Variant::OBJECT && value.is_zero() && !(property.usage & PROPERTY_USAGE_STORE_IF_NULL)) {
					continue;
				}
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
					if (codec->owns_native_property(property.name)) {
						collect(graph[i]->get(property.name));
					}
				}
			}
		}
	}

	void emit_factory(WGodotCppEmitter &p_worker) {
		emitter = &p_worker;
		emitter->clear_contribution();
		emitter->unit_resource_paths = preparation_paths;
		const String &p_path = source_path;
		const int64_t id = emitter->resource_id(p_path);
		const String name = "resource_" + itos(id);
		const String staging = emitter->staging_directory.path_join(name);
		Error section_error = declarations.open(staging + ".declarations.tmp");
		if (section_error == OK) {
			section_error = body.open(staging + ".body.tmp");
		}
		if (section_error != OK) {
			emitter->output_error = section_error;
			error(String("Cannot open generated source sections: ") + error_names[section_error]);
			return;
		}
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
			const String type = resource_type(graph[i]);
			if (game) {
				emitter->class_dependencies.insert(type);
			}
			String constructor;
			if (game) {
				if (game->node->is_abstract) {
					error("Cannot construct an abstract resource class: " + game->node->fqcn);
					return;
				}
				// Keep scene initialization for concrete generated resource classes
				// through memnew rather than their explicit GDScript new() entry point.
				constructor = "memnew(" + type + ")";
			} else if (mapped) {
				constructor = "WGodotNative::instantiate<" + type + ">()";
			} else {
				constructor = "Ref<Resource>(Object::cast_to<Resource>(ClassDB::instantiate(StringName(" + quoted(graph[i]->get_class()) + "))))";
			}
			emit("\tRef<" + type + "> resource_" + itos(i) + " = " + constructor + ";\n");
			if (!game) {
				emit("\tERR_FAIL_COND_V(resource_" + itos(i) + ".is_null(), Ref<Resource>());\n");
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
		emit("\treturn resource_0;\n");
		String source = emitter->source_header(p_path, resource->get_class()) + "#include \"core/io/resource_loader.h\"\n#include \"core/io/wgodot_resource_paths.h\"\n#include \"scene/resources/packed_scene.h\"\n#include \"modules/wgodot/native/wgodot_native_serialization.h\"\n#include \"modules/wgodot/native/wgodot_native_callback.h\"\n";
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
		source += "\nnamespace WGodotGame {\n";
		const String function = "Ref<Resource> create_resource_" + itos(id) + "(ResourceFormatLoader::CacheMode p_cache_mode) {\n\tconst auto dependency_cache = p_cache_mode == ResourceFormatLoader::CACHE_MODE_IGNORE_DEEP || p_cache_mode == ResourceFormatLoader::CACHE_MODE_REPLACE_DEEP ? p_cache_mode : ResourceFormatLoader::CACHE_MODE_REUSE;\n";
		emitter->store_source(name + ".cpp", source, declarations, function, body, "}\n}\n");
		const auto *game = resource_class(resource);
		emitter->compiled_resources[p_path] = game ? game->cpp_name : String(resource->get_class());
		emitter->current_class = nullptr;
		contribution = emitter->capture_contribution();
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
		uint64_t reused = 0;
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
			WGodotCppMemoryProfile::sample("batch-emitted", String(), batch.factories.size());
			generation_msec += OS::get_singleton()->get_ticks_msec() - started;
			for (const auto *worker : workers) {
				failed |= !worker->diagnostics.is_empty() || worker->output_error != OK;
			}
			for (WGodotCppResources *factory : batch.factories) {
				if (!failed && !factory->contribution.is_empty()) {
					emitter->restore_contribution(factory->contribution);
					if (emitter->cache) {
						PackedStringArray dependencies = factory->contribution["resource_paths"];
						dependencies.push_back(factory->source_path);
						emitter->cache->record("resource:" + factory->source_path, "", dependencies, factory->contribution);
					}
				}
				memdelete(factory); // Release resource graphs on the preparing thread.
			}
			completed += batch.factories.size();
			batch.factories.clear();
			WGodotCppMemoryProfile::sample("batch-released", String(), completed);
			const uint64_t now = OS::get_singleton()->get_ticks_msec();
			if (now - last_progress >= 5000) {
				print_line(vformat("Native factories: %d generated (%d workers)", completed, concurrency));
				last_progress = now;
			}
		};
		auto queue = [&](const String &p_path, const Ref<Resource> &p_resource) {
			const uint64_t started = OS::get_singleton()->get_ticks_msec();
			WGodotCppResources *factory = memnew(WGodotCppResources(*emitter));
			HashSet<String> previous_paths;
			SWAP(emitter->unit_resource_paths, previous_paths);
			factory->prepare_factory(p_path, p_resource);
			factory->preparation_paths = emitter->unit_resource_paths;
			SWAP(emitter->unit_resource_paths, previous_paths);
			WGodotCppMemoryProfile::sample("factory-prepared", p_path, factory->graph.size());
			preparation_msec += OS::get_singleton()->get_ticks_msec() - started;
			batch.factories.push_back(factory);
			if (batch.factories.size() == concurrency) {
				flush();
			}
		};
		HashSet<String> visited;
		auto remember_dependencies = [&](const String &p_path) {
			if (!emitter->cache) {
				return;
			}
			PackedStringArray dependencies;
			for (const String &dependency : emitter->unit_resource_paths) {
				dependencies.push_back(dependency);
			}
			dependencies.sort();
			Dictionary payload;
			payload["files"] = Dictionary();
			payload["paths"] = dependencies;
			dependencies.push_back(p_path);
			emitter->cache->record("dependencies:" + p_path, "", dependencies, payload);
		};
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
				if (emitter->cache) {
					const Dictionary cached = emitter->cache->reuse("resource:" + path, "");
					if (!cached.is_empty() && emitter->restore_contribution(cached)) {
						const Dictionary compiled = cached["compiled"];
						if (compiled.has(path)) {
							paths.push_back(path);
							emitter->resource_sources[path] = emitter->cache->digest(path);
						}
						++reused;
						continue;
					}
					const Dictionary cached_dependencies = emitter->cache->reuse("dependencies:" + path, "");
					if (!cached_dependencies.is_empty()) {
						for (const String &dependency : PackedStringArray(cached_dependencies["paths"])) {
							emitter->resource_id(dependency);
						}
						continue;
					}
				}
				emitter->unit_resource_paths.clear();
				const bool authored = WGodotNativeResourcePolicy::is_authored(path);
				if (authored || ResourceLoader::get_resource_type(path) == "PackedScene") {
					WGodotCppMemoryProfile::sample("resource-load-begin", path);
					const Ref<Resource> resource = ResourceLoader::load(path);
					WGodotCppMemoryProfile::sample("resource-loaded", path);
					if (resource.is_null()) {
						emitter->diagnostics.push_back("Cannot load required export resource: " + path);
						continue;
					}
					if (authored && WGodotNativeResourcePolicy::needs_factory(resource)) {
						paths.push_back(path);
						emitter->resource_sources[path] = emitter->cache ? emitter->cache->digest(path) : FileAccess::get_sha256(path);
						queue(path, resource);
						continue;
					}
					const Ref<PackedScene> scene = resource;
					if (scene.is_valid()) {
						HashSet<const Resource *> seen;
						scene_dependencies(scene, path, seen);
						remember_dependencies(path);
						continue;
					}
				}
				file_dependencies(path);
				remember_dependencies(path);
			}
			flush();
		}
		flush();
		for (auto *worker : workers) {
			emitter->diagnostics.append_array(worker->diagnostics);
			if (emitter->output_error == OK) {
				emitter->output_error = worker->output_error;
			}
			memdelete(worker);
		}
		print_line(vformat("Native factories: %d generated, %d reused (%d workers)", completed, reused, concurrency));
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
