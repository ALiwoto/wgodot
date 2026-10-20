// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "modules/gdscript/wgodot_gd/editor/property_path.h"

using Parser = GDScriptParser;

String WGodotCppEmitter::property_path_accessor(const WGodotGDScriptPropertyPath &p_path, const Parser::ExpressionNode *p_origin, bool p_write, int p_writeback_start, bool p_object_receiver, const String &p_value_type) {
	const auto &target_type = p_path.segments[0].base_type;
	const bool target_interface = is_interface_type(target_type);
	const String target = class_name(target_type, p_origin);
	const String field_type = type(p_path.segments[p_path.segments.size() - 1].datatype, p_origin);
	const String value = p_value_type.is_empty() ? field_type : p_value_type;
	auto access = [&](int p_index, const String &p_receiver, const String &p_value = String()) {
		const auto &segment = p_path.segments[p_index];
		if (segment.base_type.kind == Parser::DataType::BUILTIN) {
			return Value(builtin_member(segment.base_type.builtin_type, segment.name, p_origin, p_receiver, p_value), p_value.is_empty() ? type(segment.datatype, p_origin) : String("void"));
		}
		// A subscript origin preserves normal getter/setter semantics, including
		// when the tween was declared inside the property's own accessor.
		Parser::SubscriptNode origin;
		origin.start_line = p_origin->start_line;
		origin.start_column = p_origin->start_column;
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
	String parameter = target_interface ? "const " + target + " &target" : target + " *target";
	String return_type = p_write ? "void" : value;
	String value_parameter = p_write ? ", " + value + " value" : "";
	if (p_object_receiver) {
		parameter = "Object *object";
	}
	String body = "[](" + parameter + value_parameter + ") -> " + return_type + " {\n";
	if (p_object_receiver) {
		body += "\tauto *target = static_cast<" + target + " *>(object);\n";
	}
	Vector<String> receivers;
	for (int i = 0; i < p_path.segments.size(); i++) {
		String receiver = i == 0 ? "target" : "part_" + itos(i - 1);
		if (i > 0 && p_path.segments[i].base_type.kind != Parser::DataType::BUILTIN) {
			const String pointer = "object_" + itos(i);
			const bool contract = is_interface_type(p_path.segments[i].base_type);
			body += "\tauto *" + pointer + " = " + (contract ? receiver + ".operator->()" : "WGodotNative::object_pointer(" + receiver + ")") + ";\n";
			body += p_write ? "\tERR_FAIL_NULL(" + pointer + ");\n" : "\tERR_FAIL_NULL_V(" + pointer + ", (" + return_type + "()));\n";
			if (!contract) {
				receiver = pointer;
			}
		}
		receivers.push_back(receiver);
		if (p_write && i == p_path.segments.size() - 1) {
			break;
		}
		if (!p_write && i == p_path.segments.size() - 1) {
			Value result = access(i, receiver);
			result.code = convert_value(result, value);
			result.cpp_type = value;
			body += result.statement(1, true);
			break;
		}
		Value part = access(i, receiver);
		if (is_interface_type(p_path.segments[i].datatype)) {
			// A native getter may return an Object pointer. Keep the interface
			// handle so the next segment can access either base or contract members.
			const String part_type = type(p_path.segments[i].datatype, p_origin);
			part.code = convert_value(part, part_type);
			part.cpp_type = part_type;
			part.object_pointer = false;
		}
		body += "\tauto part_" + itos(i) + " = " + part.expression() + ";\n";
	}
	if (p_write) {
		// Write back value components in the same order as Object::set_indexed.
		// Animation resource traversal may exclude leading Resource references.
		for (int i = p_path.segments.size() - 1; i >= p_writeback_start; i--) {
			body += access(i, receivers[i], i == p_path.segments.size() - 1 ? convert_value(Value("value", value), field_type) : "part_" + itos(i)).statement(1);
		}
	}
	return body + "}";
}
