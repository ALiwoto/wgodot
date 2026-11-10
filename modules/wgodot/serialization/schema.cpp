// wgodot-changes::file
#include "schema.h"

#ifdef TOOLS_ENABLED
#include "modules/gdscript/wgodot_gd/interface_helpers.h"

namespace WGodotSerialization {
namespace {
using P = GDScriptParser;

Variant argument(const P::Node *p_node, const StringName &p_name, const Variant &p_default, int p_index = 0) {
	for (const auto *annotation : p_node->annotations) {
		if (annotation->name == p_name && annotation->resolved_arguments.size() > uint32_t(p_index)) {
			return annotation->resolved_arguments[p_index];
		}
	}
	return p_default;
}

Kind inferred_kind(const P::DataType &p_type) {
	switch (p_type.builtin_type) {
		case Variant::BOOL:
			return Kind::BOOL;
		case Variant::INT:
			return Kind::INT64;
		case Variant::FLOAT:
			return Kind::DOUBLE;
		case Variant::STRING:
			return Kind::STRING;
		case Variant::PACKED_BYTE_ARRAY:
			return Kind::BYTES;
		case Variant::PACKED_INT32_ARRAY:
			return Kind::INT32;
		case Variant::PACKED_INT64_ARRAY:
			return Kind::INT64;
		case Variant::PACKED_FLOAT32_ARRAY:
			return Kind::FLOAT;
		case Variant::PACKED_FLOAT64_ARRAY:
			return Kind::DOUBLE;
		case Variant::PACKED_STRING_ARRAY:
			return Kind::STRING;
		case Variant::VECTOR3:
		case Variant::PACKED_VECTOR3_ARRAY:
			return Kind::VECTOR3;
		case Variant::OBJECT:
			return Kind::MESSAGE;
		default:
			return Kind::INVALID;
	}
}

bool matches(Kind p_kind, const P::DataType &p_type, bool p_binary, bool p_protobuf) {
	if (p_kind == Kind::INVALID || is_sequence(p_type.builtin_type, p_kind)) {
		return false;
	}
	const Kind inferred = inferred_kind(p_type);
	if (p_kind == Kind::MESSAGE) {
		const StringName base = p_protobuf ? SNAME("ProtobufSerializable") : p_binary ? SNAME("BinarySerializable")
																					  : SNAME("JsonSerializable");
		return p_type.kind == P::DataType::CLASS && WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_type.class_type, base);
	}
	if (inferred == Kind::INT64) {
		return p_kind >= Kind::INT8 && p_kind <= Kind::SFIXED64;
	}
	if (inferred == Kind::DOUBLE) {
		return p_kind == Kind::FLOAT || p_kind == Kind::DOUBLE;
	}
	return inferred == p_kind && (!p_protobuf || p_kind != Kind::VECTOR3);
}

void fields(const P::ClassNode *p_class, Schema &r_schema) {
	if (p_class->base_type.kind == P::DataType::CLASS) {
		fields(p_class->base_type.class_type, r_schema);
	}
	for (const auto &member : p_class->members) {
		if (member.type != P::ClassNode::Member::VARIABLE || member.variable->is_static) {
			continue;
		}
		Field field;
		field.node = member.variable;
		field.type = member.variable->type_constraint;
		field.id = argument(field.node, r_schema.protobuf ? SNAME("@PbFieldId") : SNAME("@BinaryFieldOrder"), -1);
		field.json = argument(field.node, SNAME("@JsonPropertyName"), String(member.get_name()));
		field.oneof = r_schema.protobuf ? String(argument(field.node, SNAME("@PbOneof"), "")) : String();
		field.packed = argument(field.node, SNAME("@PbPacked"), true);
		const String encoding = argument(field.node, r_schema.json ? SNAME("@JsonEncoding") : r_schema.protobuf ? SNAME("@PbEncoding")
																												: SNAME("@BinaryFieldType"),
				"");
		field.kind = kind_from_name(encoding);
		if (encoding.is_empty()) {
			field.kind = inferred_kind(field.type.builtin_type == Variant::ARRAY ? field.type.get_container_element_type(0) : field.type);
		}
		field.repeated = is_sequence(field.type.builtin_type, field.kind);
		const auto element = field.repeated ? sequence_element(field.type) : field.type;
		bool valid = matches(field.kind, element, r_schema.binary, r_schema.protobuf);
		if (field.type.builtin_type == Variant::DICTIONARY) {
			const auto key = field.type.get_container_element_type(0);
			const auto value = field.type.get_container_element_type(1);
			field.key = kind_from_name(argument(field.node, SNAME("@PbKeyEncoding"), ""));
			field.value = kind_from_name(argument(field.node, SNAME("@PbValueEncoding"), ""));
			if (field.key == Kind::INVALID && String(argument(field.node, SNAME("@PbKeyEncoding"), "")).is_empty()) {
				field.key = inferred_kind(key);
			}
			if (field.value == Kind::INVALID && String(argument(field.node, SNAME("@PbValueEncoding"), "")).is_empty()) {
				field.value = inferred_kind(value);
			}
			valid = (!r_schema.binary || r_schema.protobuf) && matches(field.key, key, r_schema.binary, r_schema.protobuf) && matches(field.value, value, r_schema.binary, r_schema.protobuf);
			valid &= field.key == Kind::STRING || field.key == Kind::BOOL || (field.key >= Kind::INT8 && field.key <= Kind::SFIXED64);
		}
		if (!valid || (r_schema.protobuf && (field.kind == Kind::INT8 || field.kind == Kind::UINT8 || field.kind == Kind::INT16 || field.kind == Kind::UINT16))) {
			r_schema.error = "Unsupported serializable field type or encoding: " + String(member.get_name());
		}
		if (r_schema.protobuf && (field.id <= 0 || field.id > 536870911 || (field.id >= 19000 && field.id <= 19999))) {
			r_schema.error = "PbFieldId must be 1..536870911, excluding reserved IDs 19000..19999.";
		} else if (r_schema.binary && !r_schema.protobuf && field.id < 0) {
			r_schema.error = "Binary fields require a non-negative BinaryFieldOrder.";
		}
		if (!field.oneof.is_empty() && (field.repeated || field.type.builtin_type == Variant::DICTIONARY)) {
			r_schema.error = "Protobuf oneof members cannot be arrays or maps.";
		}
		for (const Field &other : r_schema.fields) {
			if ((r_schema.binary && field.id == other.id) || field.json == other.json) {
				r_schema.error = "Duplicate serialization field ID/order or JSON property: " + String(member.get_name());
			}
		}
		if (!r_schema.error.is_empty()) {
			r_schema.error_node = field.node;
			return;
		}
		r_schema.fields.push_back(field);
	}
}
} // namespace

Schema schema(const GDScriptParser::ClassNode *p_class, Codec p_codec) {
	Schema result;
	const StringName contract = p_codec == Codec::PROTOBUF ? SNAME("ProtobufSerializable") : p_codec == Codec::BINARY ? SNAME("BinarySerializable")
																													  : SNAME("JsonSerializable");
	result.enabled = WGodotGDScriptInterfaceHelpers::class_implements_native_interface(p_class, contract);
	if (!result.enabled || p_class->wgodot_is_interface) {
		result.enabled = false;
		return result;
	}
	result.binary = p_codec != Codec::JSON;
	result.protobuf = p_codec == Codec::PROTOBUF;
	result.json = p_codec == Codec::JSON;
	const String endian = argument(p_class, SNAME("@BinaryOptions"), "little");
	result.big_endian = endian == "big";
	result.strict = argument(p_class, SNAME("@BinaryOptions"), true, 1);
	if (endian != "little" && endian != "big") {
		result.error = "BinaryOptions endian must be little or big.";
		result.error_node = p_class;
		return result;
	}
	fields(p_class, result);
	struct Order {
		bool operator()(const Field &p_a, const Field &p_b) const { return p_a.id < p_b.id; }
	};
	result.fields.sort_custom<Order>();
	return result;
}
} // namespace WGodotSerialization
#endif
