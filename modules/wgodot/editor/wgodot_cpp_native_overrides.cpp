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

void WGodotCppEmitter::configure_native_call(const MethodBind *p_method, const Parser::Node *p_origin, Vector<Value> &r_arguments, NativeCall &r_call) {
	const StringName owner = p_method->get_instance_class();
	const StringName method = p_method->get_name();
	// These APIs return independent snapshots. Stored results need WArray
	// ownership; iteration can retain the engine array without copying it.
	r_call.snapshot_result = (owner == SNAME("Node") && method == SNAME("get_children")) ||
			(owner == SNAME("SmoothScrollElement") && method == SNAME("get_virtual_items")) ||
			(owner == SNAME("SceneTree") && method == SNAME("get_nodes_in_group"));
	if (owner == SNAME("ResourceLoader") && method == SNAME("load_threaded_get_status")) {
		if (r_arguments.size() != 1) {
			unsupported(p_origin, "ResourceLoader.load_threaded_get_status with a progress Array. Native export currently supports only load_threaded_get_status(path)");
			return;
		}
		class_call_headers.insert("core/io/resource_loader.h");
		r_call.owner = "::ResourceLoader";
		r_call.method = "load_threaded_get_status";
		r_call.argument_types = { "String", "float *" };
		Value progress("nullptr", "float *");
		progress.effects = false;
		progress.invariant = true;
		r_arguments.push_back(progress);
		r_call.is_static = true;
		r_call.adapted = true;
	} else if (owner == SNAME("StreamPeer") && method == SNAME("put_data")) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_engine.h");
		r_call.owner = "WGodotNative";
		r_call.method = "stream_put_data";
		r_call.receiver_argument = true;
		r_call.is_static = true;
	} else if (owner == SNAME("FileAccess") && (method == SNAME("get_buffer") || method == SNAME("store_buffer"))) {
		// The argument types select the public vector overloads directly.
		r_call.method = method;
	}
}
