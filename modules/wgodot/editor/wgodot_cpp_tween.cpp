// wgodot-changes::file
#include "wgodot_cpp_emitter.h"

#include "modules/gdscript/gdscript_analyzer.h"
#include "modules/gdscript/wgodot_gd/editor/property_path.h"

using Parser = GDScriptParser;

String WGodotCppEmitter::tween_builtin_property(Variant::Type p_type, const StringName &p_name, const Parser::Node *p_origin, const String &p_receiver, const String &p_value) {
	// These are the native members/accessors registered in variant_setget.h.
	// Do not fall back to get_named/set_named: a missing mapping is an export error.
	const String name = p_name;
	String member = name;
	String getter;
	String setter;
	String index;
	switch (p_type) {
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::QUATERNION:
		case Variant::TRANSFORM3D:
			break;
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::AABB:
			if (name == "end") {
				getter = "get_end";
				setter = "set_end";
			}
			break;
		case Variant::TRANSFORM2D:
			member = "columns[" + String(name == "x" ? "0" : name == "y" ? "1"
																		 : "2") +
					"]";
			break;
		case Variant::PROJECTION:
			member = "columns[" + itos(String("xyzw").find(name)) + "]";
			break;
		case Variant::PLANE:
			if (name == "x" || name == "y" || name == "z") {
				member = "normal." + name;
			}
			break;
		case Variant::BASIS:
			getter = "get_column";
			setter = "set_column";
			index = itos(String("xyz").find(name));
			break;
		case Variant::COLOR:
			if (name != "r" && name != "g" && name != "b" && name != "a") {
				getter = "get_" + name;
				setter = "set_" + name;
			}
			break;
		default:
			unsupported(p_origin, "native tween access to " + Variant::get_type_name(p_type) + "." + name);
			return String();
	}
	if (!getter.is_empty()) {
		return p_receiver + "." + (p_value.is_empty() ? getter + "(" + index : setter + "(" + (index.is_empty() ? "" : index + ", ") + p_value) + ")";
	}
	const String field = p_receiver + "." + member;
	return p_value.is_empty() ? field : field + " = WGodotNative::convert<std::decay_t<decltype(" + field + ")>>(" + p_value + ")";
}

WGodotCppEmitter::Value WGodotCppEmitter::tween_property_call(const Parser::CallNode *p_call, const Parser::ExpressionNode *p_base) {
	const GDScriptAnalyzer *analyzer = project.find_analyzer(current_class->script_path);
	const WGodotGDScriptPropertyPath *path = analyzer ? analyzer->wgodot_get_tween_property_path(p_call) : nullptr;
	if (!path || path->segments.is_empty() || path->segments.size() != path->path.get_as_property_path().get_subname_count()) {
		unsupported(p_call, "Tween.tween_property requires a fully analyzer-resolved constant property path");
		return String();
	}
	const auto target_type = expression_type(p_call->arguments[0]);
	const bool target_interface = is_interface_type(target_type);
	const auto &value_type = path->segments[path->segments.size() - 1].datatype;
	if (native_only(value_type)) {
		unsupported(p_call, "tweening a native container or callback value through Godot's Variant interpolation");
		return String();
	}
	const String target = class_name(target_type, p_call);
	const String value = type(value_type, p_call);
	class_call_headers.insert("modules/wgodot/native/wgodot_native_tween.h");

	auto access = [&](int p_index, const String &p_receiver, const String &p_value = String()) {
		const auto &segment = path->segments[p_index];
		if (segment.base_type.kind == Parser::DataType::BUILTIN) {
			return Value(tween_builtin_property(segment.base_type.builtin_type, segment.name, p_call, p_receiver, p_value), p_value.is_empty() ? type(segment.datatype, p_call) : String("void"));
		}
		// A subscript origin preserves normal getter/setter semantics, including
		// when the tween was declared inside the property's own accessor.
		Parser::SubscriptNode origin;
		origin.start_line = p_call->start_line;
		origin.start_column = p_call->start_column;
		origin.type_constraint = segment.datatype;
		const bool contract = is_interface_type(segment.base_type);
		Value receiver(p_receiver, contract ? type(segment.base_type, &origin) : class_name(segment.base_type, &origin) + " *");
		receiver.object_pointer = !contract;
		receiver.nonnull = true; // The generated accessor checks each object before access.
		receiver.effects = false;
		receiver.borrowed = true;
		Value assigned(p_value, type(segment.datatype, &origin));
		assigned.effects = false;
		assigned.borrowed = true;
		return property_access(segment.base_type, segment.name, &origin, receiver, assigned);
	};
	auto accessor = [&](bool p_write) {
		const String parameter = target_interface ? "const " + target + " &target" : target + " *target";
		String body = "[](" + parameter + (p_write ? ", " + value + " value" : "") + ") -> " + (p_write ? "void" : value) + " {\n";
		Vector<String> receivers;
		for (int i = 0; i < path->segments.size(); i++) {
			String receiver = i == 0 ? "target" : "part_" + itos(i - 1);
			if (i > 0 && path->segments[i].base_type.kind != Parser::DataType::BUILTIN) {
				const String pointer = "object_" + itos(i);
				const bool contract = is_interface_type(path->segments[i].base_type);
				body += "\tauto *" + pointer + " = " + (contract ? receiver + ".operator->()" : "WGodotNative::object_pointer(" + receiver + ")") + ";\n";
				body += p_write ? "\tERR_FAIL_NULL(" + pointer + ");\n" : "\tERR_FAIL_NULL_V(" + pointer + ", (" + value + "()));\n";
				if (!contract) {
					receiver = pointer;
				}
			}
			receivers.push_back(receiver);
			if (p_write && i == path->segments.size() - 1) {
				break;
			}
			if (!p_write && i == path->segments.size() - 1) {
				body += access(i, receiver).statement(1, true);
				break;
			}
			Value part = access(i, receiver);
			if (is_interface_type(path->segments[i].datatype)) {
				// A native getter may return an Object pointer. Keep the interface
				// handle so the next segment can access either base or contract members.
				const String part_type = type(path->segments[i].datatype, p_call);
				part.code = convert_value(part, part_type);
				part.cpp_type = part_type;
				part.object_pointer = false;
			}
			body += "\tauto part_" + itos(i) + " = " + part.expression() + ";\n";
		}
		if (p_write) {
			// Object::set_indexed writes EVERY parent back, even Object references.
			// Retain that order and each setter's side effects, using typed locals.
			for (int i = path->segments.size() - 1; i >= 0; i--) {
				body += access(i, receivers[i], i == path->segments.size() - 1 ? "value" : "part_" + itos(i)).statement(1);
			}
		}
		return body + "}";
	};

	// Match native_call's argument/receiver evaluation order, but never emit
	// the path literal: only its analyzer-resolved property accesses survive.
	Vector<Value> operands{ target_interface ? lower_converted(p_call->arguments[0], target_type) : lower(p_call->arguments[0]), lower(p_call->arguments[2]), lower(p_call->arguments[3]), lower_receiver(p_base) };
	Value result = sequence(operands);
	result.cpp_type = "Ref<PropertyTweener>";
	result.effects = true;
	const String receiver = target_interface ? operands[0].code : "static_cast<" + target + " *>(WGodotNative::object_pointer(" + operands[0].code + "))";
	result.code = "WGodotNative::tween_property(WGodotNative::object_pointer(" + operands[3].code + "), " + receiver + ",\n";
	result.code += accessor(false) + ",\n" + accessor(true) + ", " + convert_value(operands[1], "Variant") + ", " + operands[2].code + ")";
	return result;
}
