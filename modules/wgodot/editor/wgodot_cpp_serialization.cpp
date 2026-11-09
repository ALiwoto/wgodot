// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

#include "modules/wgodot/serialization/schema.h"

using namespace WGodotCppNames;
using namespace WGodotSerialization;

namespace {
String field_name(const Field &p_field) {
	return "v_" + symbol(p_field.node->identifier->name);
}

String kind(Kind p_kind) {
	return "Kind::" + String(kind_name(p_kind));
}

String codec(const Field &p_field, bool p_protobuf, bool p_read) {
	const String operation = p_read ? "read" : "write";
	if (p_field.type.builtin_type == Variant::DICTIONARY) {
		return operation + "_map<" + kind(p_field.key) + ", " + kind(p_field.value) + ">";
	}
	return operation + String(p_field.type.builtin_type == Variant::ARRAY ? "_array<" : "<") + kind(p_field.kind) + ", " + (p_protobuf ? "true" : "false") + ">";
}
} // namespace

void WGodotCppEmitter::emit_serialization(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions) {
	for (const Codec selected : { Codec::JSON, Codec::BINARY, Codec::PROTOBUF }) {
		const Schema layout = schema(p_class.node, selected);
		if (!layout.enabled) {
			continue;
		}
		if (!layout.error.is_empty()) {
			unsupported(layout.error_node, layout.error);
			return;
		}
		const String &name = p_class.cpp_name;
		String encode;
		String decode;
		if (selected == Codec::JSON) {
			class_call_headers.insert("modules/wgodot/serialization/native_json.h");
			r_declaration += "\tDictionary write_json() const override;\n";
			r_declaration += "\tbool read_json(const Dictionary &p_data) override;\n";
			encode = "Dictionary " + name + "::write_json() const {\n\tDictionary result;\n";
			decode = "bool " + name + "::read_json(const Dictionary &p_data) {\n";
			for (const Field &field : layout.fields) {
				// Register dependencies even when a field is only used by the codec.
				(void)type(field.type, field.node);
				const String value = field_name(field);
				encode += "\tresult[" + quoted(field.json) + "] = WGodotSerialization::json(" + value + ");\n";
				decode += "\tif (const Variant *value = p_data.getptr(" + quoted(field.json) + ")) {\n\t\tif (!WGodotSerialization::parse_json(*value, " + value + ")) {\n\t\t\treturn false;\n\t\t}\n\t}\n";
			}
			r_definitions += encode + "\treturn result;\n}\n\n";
			r_definitions += decode + "\treturn true;\n}\n\n";
			continue;
		}
		class_call_headers.insert("modules/wgodot/serialization/native.h");
		if (layout.protobuf) {
			Vector<int> ids;
			for (const Field &field : layout.fields) {
				if (field.type.builtin_type != Variant::ARRAY && field.type.builtin_type != Variant::DICTIONARY) {
					ids.push_back(field.id);
				}
			}
			if (!ids.is_empty()) {
				r_declaration += "private:\n\tuint64_t protobuf_presence[" + itos((ids.size() + 63) / 64) + "]{};\npublic:\n";
			}
			r_declaration += "\tbool has_protobuf_field(uint32_t p_id) const override;\n";
			r_declaration += "\tvoid set_protobuf_field(uint32_t p_id, bool p_present) override;\n";
			r_declaration += "\tvoid clear_protobuf_fields() override;\n";
			String has = "bool " + name + "::has_protobuf_field(uint32_t p_id) const {\n\tswitch (p_id) {\n";
			String set = "void " + name + "::set_protobuf_field(uint32_t p_id, bool p_present) {\n\tswitch (p_id) {\n";
			for (int index = 0; index < ids.size(); index++) {
				const String slot = "protobuf_presence[" + itos(index / 64) + "]";
				const String mask = "(UINT64_C(1) << " + itos(index % 64) + ")";
				has += "\t\tcase " + itos(ids[index]) + ": return (" + slot + " & " + mask + ") != 0;\n";
				set += "\t\tcase " + itos(ids[index]) + ":\n\t\t\t" + slot + " = p_present ? " + slot + " | " + mask + " : " + slot + " & ~" + mask + ";\n\t\t\treturn;\n";
			}
			r_definitions += has + "\t\tdefault: return false;\n\t}\n}\n\n";
			r_definitions += set + "\t\tdefault: return;\n\t}\n}\n\n";
			r_definitions += "void " + name + "::clear_protobuf_fields() {\n";
			if (!ids.is_empty()) {
				r_definitions += "\tfor (uint64_t &bits : protobuf_presence) {\n\t\tbits = 0;\n\t}\n";
			}
			r_definitions += "}\n\n";
		}
		const String suffix = layout.protobuf ? "protobuf" : "binary";
		r_declaration += "\tvoid write_" + suffix + "(WGodotSerialization::Writer &writer) const override;\n\tbool read_" + suffix + "(WGodotSerialization::Reader &reader) override;\n";
		encode = "void " + name + "::write_" + suffix + "(WGodotSerialization::Writer &writer) const {\n\tusing namespace WGodotSerialization;\n";
		decode = "bool " + name + "::read_" + suffix + "(WGodotSerialization::Reader &reader) {\n\tusing namespace WGodotSerialization;\n";
		if (!layout.protobuf) {
			const String endian = layout.big_endian ? "true" : "false";
			encode += "\twriter.big_endian = " + endian + ";\n";
			decode += "\treader.big_endian = " + endian + ";\n";
		}
		HashMap<String, String> oneofs;
		for (const Field &field : layout.fields) {
			if (!field.oneof.is_empty() && !oneofs.has(field.oneof)) {
				String flag = "oneof_" + itos(oneofs.size());
				oneofs.insert(field.oneof, flag);
				encode += "\tbool " + flag + " = false;\n";
			}
		}
		if (layout.protobuf) {
			decode += "\twhile (reader.remaining()) {\n\t\tconst uint64_t tag = reader.varint();\n\t\tconst uint64_t id = tag >> 3;\n\t\tconst int wire = tag & 7;\n\t\tif (!reader.valid || !id || id > 536870911) {\n\t\t\treturn false;\n\t\t}\n\t\tswitch (id) {\n";
		}
		for (const Field &field : layout.fields) {
			const String value = field_name(field);
			const String id = itos(field.id);
			const bool array = field.type.builtin_type == Variant::ARRAY;
			const bool map = field.type.builtin_type == Variant::DICTIONARY;
			String indent = "\t";
			if (layout.protobuf && !array && !map) {
				encode += "\tif (present<" + kind(field.kind) + ">(" + value + ") || has_field(" + id + ")) {\n";
				indent += "\t";
				if (!field.oneof.is_empty()) {
					const String flag = oneofs[field.oneof];
					encode += indent + "if (" + flag + ") {\n" + indent + "\twriter.valid = false;\n" + indent + "\treturn;\n" + indent + "}\n" + indent + flag + " = true;\n";
				}
				encode += indent + "writer.tag(" + id + ", " + itos(wire_type(field.kind)) + ");\n";
			}
			String arguments = "writer, " + value;
			if (layout.protobuf && (array || map)) {
				arguments += ", " + id;
				if (array) {
					arguments += field.packed ? ", true" : ", false";
				}
			}
			encode += indent + codec(field, layout.protobuf, false) + "(" + arguments + ");\n";
			if (indent != "\t") {
				encode += "\t}\n";
			}
			indent = layout.protobuf ? "\t\t\t\t" : "\t";
			if (layout.protobuf) {
				decode += "\t\t\tcase " + id + ": {\n";
			} else if (!layout.strict) {
				decode += "\tif (!reader.remaining()) {\n\t\treturn reader.valid;\n\t}\n";
			}
			arguments = "reader, " + value;
			if (layout.protobuf && (array || map)) {
				arguments += ", wire";
			}
			String condition = "!" + codec(field, layout.protobuf, true) + "(" + arguments + ")";
			if (layout.protobuf && !array && !map) {
				condition = "wire != " + itos(wire_type(field.kind)) + " || " + condition;
			}
			decode += indent + "if (" + condition + ") {\n" + indent + "\treturn false;\n" + indent + "}\n";
			if (layout.protobuf && !array && !map) {
				if (!field.oneof.is_empty()) {
					for (const Field &other : layout.fields) {
						if (other.id != field.id && other.oneof == field.oneof) {
							decode += indent + field_name(other) + " = decltype(" + field_name(other) + "){};\n" + indent + "set_field_present(" + itos(other.id) + ", false);\n";
						}
					}
				}
				decode += indent + "set_field_present(" + id + ", true);\n";
			}
			if (layout.protobuf) {
				decode += "\t\t\t} break;\n";
			}
		}
		if (layout.protobuf) {
			decode += "\t\t\tdefault:\n\t\t\t\tif (!reader.skip(wire, id)) {\n\t\t\t\t\treturn false;\n\t\t\t\t}\n\t\t\t\tbreak;\n\t\t}\n\t}\n";
		}
		r_definitions += encode + "}\n\n" + decode + "\treturn reader.valid;\n}\n\n";
	}
}
