// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "core/object/method_bind.h"

using Parser = GDScriptParser;

bool WGodotCppEmitter::validate_native_arguments(const MethodBind *p_method, const Parser::Node *p_origin) {
	for (int i = 0; i < p_method->get_argument_count(); i++) {
		const Variant::Type argument_type = p_method->get_argument_type(i);
		if (argument_type == Variant::ARRAY || argument_type == Variant::DICTIONARY) {
			if (argument_type == Variant::ARRAY && p_origin->type == Parser::Node::CALL) {
				const auto *call = static_cast<const Parser::CallNode *>(p_origin);
				if (uint32_t(i) < call->arguments.size() && is_array_duplicate(call->arguments[i])) {
					continue; // Explicit independent copy, never shared WArray storage.
				}
			}
			const PropertyInfo argument = p_method->get_argument_info(i);
			unsupported(p_origin, vformat("native call %s.%s: argument %d (%s) uses %s. This API needs an explicit native handler for its container semantics", p_method->get_instance_class(), p_method->get_name(), i + 1, argument.name, Variant::get_type_name(argument_type)));
			return false;
		}
	}
	return true;
}

bool WGodotCppEmitter::validate_builtin_arguments(Variant::Type p_type, const StringName &p_method, const Parser::Node *p_origin) {
	const int count = Variant::get_builtin_method_argument_count(p_type, p_method);
	for (int i = 0; i < count; i++) {
		const Variant::Type argument_type = Variant::get_builtin_method_argument_type(p_type, p_method, i);
		if (argument_type == Variant::ARRAY || argument_type == Variant::DICTIONARY) {
			if (argument_type == Variant::ARRAY && p_origin->type == Parser::Node::CALL) {
				const auto *call = static_cast<const Parser::CallNode *>(p_origin);
				if (uint32_t(i) < call->arguments.size() && is_array_duplicate(call->arguments[i])) {
					continue;
				}
			}
			unsupported(p_origin, vformat("builtin call %s.%s: argument %d (%s) uses %s. This API needs an explicit native handler for its container semantics", Variant::get_type_name(p_type), p_method, i + 1, Variant::get_builtin_method_argument_name(p_type, p_method, i), Variant::get_type_name(argument_type)));
			return false;
		}
	}
	return true;
}

bool WGodotCppEmitter::native_override(const MethodBind *p_method, const String &p_receiver, const Vector<String> &p_arguments, const String &p_result, const Parser::Node *p_origin, String &r_code) {
	const StringName owner = p_method->get_instance_class();
	const StringName method = p_method->get_name();
	if (owner == SNAME("ResourceLoader") && method == SNAME("load_threaded_get_status")) {
		// The script wrapper writes progress through a shared Array. Only the
		// status-only form has a native translation until out arguments are defined.
		if (p_arguments.size() != 1) {
			unsupported(p_origin, "ResourceLoader.load_threaded_get_status with a progress Array. Native export currently supports only load_threaded_get_status(path)");
			return true;
		}
		class_call_headers.insert("core/io/resource_loader.h");
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		r_code = "static_cast<" + p_result + ">(::ResourceLoader::load_threaded_get_status(WGodotNative::convert<String>(" + p_arguments[0] + "), nullptr))";
		return true;
	}
	if (owner == SNAME("Node") && (method == SNAME("get_children") || method == SNAME("get_child_count") || method == SNAME("get_child"))) {
		Vector<String> arguments(p_arguments);
		// In particular, an omitted include_internal is false in GDScript, even
		// though these C++ declarations default to true.
		for (int i = arguments.size(); i < p_method->get_argument_count(); i++) {
			if (!p_method->has_default_argument(i)) {
				unsupported(p_origin, "missing native argument " + String(method));
				return true;
			}
			arguments.push_back(literal(p_method->get_default_argument(i), p_origin));
		}
		class_call_headers.insert("scene/main/node.h");
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		const bool children = method == SNAME("get_children");
		r_code = (children ? "WGodotNative::copy_array<" + p_result + ">(" : "") + "WGodotNative::invoke_member<" + (children ? "Array" : p_result) + ">(&Node::" + String(method) + ", " + p_receiver + ", " + String(", ").join(arguments) + ")" + (children ? ")" : "");
		return true;
	}
	if (owner == SNAME("SmoothScrollElement") && method == SNAME("get_virtual_items") && p_arguments.is_empty()) {
		class_call_headers.insert("modules/wgodot_ui/smooth_scroll_element.h");
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		r_code = "WGodotNative::copy_array<" + p_result + ">(WGodotNative::invoke_member<Array>(&SmoothScrollElement::get_virtual_items, " + p_receiver + "))";
		return true;
	}
	if (owner == SNAME("SceneTree") && method == SNAME("get_nodes_in_group") && p_arguments.size() == 1) {
		class_call_headers.insert("scene/main/scene_tree.h");
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		r_code = "WGodotNative::copy_vector<" + p_result + ">(WGodotNative::invoke_member<Vector<Node *>>(&SceneTree::get_nodes_in_group, " + p_receiver + ", " + p_arguments[0] + "))";
		return true;
	}
	if (owner == SNAME("StreamPeer") && method == SNAME("put_data")) {
		if (p_arguments.size() != 1) {
			unsupported(p_origin, "StreamPeer.put_data without its byte vector");
			return true;
		}
		// StreamPeer::_put_data is a protected script wrapper. Use the public
		// pointer/length API and retain its empty-buffer behavior.
		class_call_headers.insert("modules/wgodot/native/wgodot_native_engine.h");
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		r_code = "static_cast<" + p_result + ">(WGodotNative::stream_put_data(" + p_receiver + ", WGodotNative::convert<PackedByteArray>(" + p_arguments[0] + ")))";
		return true;
	}
	if (owner == SNAME("FileAccess") && (method == SNAME("get_buffer") || method == SNAME("store_buffer")) && p_arguments.size() == 1) {
		// Select the public vector overload, rather than the pointer/length API
		// or an older compatibility binding with a different return type.
		const String signature = method == SNAME("get_buffer") ? "Vector<uint8_t> (FileAccess::*)(int64_t) const" : "bool (FileAccess::*)(const Vector<uint8_t> &)";
		class_call_headers.insert("core/io/file_access.h");
		class_call_headers.insert("modules/wgodot/native/wgodot_native_calls.h");
		r_code = "WGodotNative::invoke_member<" + p_result + ">(static_cast<" + signature + ">(&FileAccess::" + String(method) + "), " + p_receiver + ", " + p_arguments[0] + ")";
		return true;
	}
	return false;
}
