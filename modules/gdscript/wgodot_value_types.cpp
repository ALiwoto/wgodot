// wgodot-changes::file
#include "gdscript_analyzer.h"
#include "wgodot_record_declarations.h"

#include <functional>

bool GDScriptAnalyzer::wgodot_validate_value_type(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_source) {
	for (int index = 0; index < p_type.get_container_element_type_count(); ++index) {
		if (p_type.get_container_element_type(index).wgodot_value) {
			push_error("Nested @value element qualifications require a record with @value fields.", p_source);
			return false;
		}
	}
	if (!p_type.wgodot_value) {
		return true;
	}
	const Variant::Type type = p_type.builtin_type;
	if (p_type.kind != GDScriptParser::DataType::BUILTIN ||
			(type != Variant::ARRAY && type != Variant::DICTIONARY && (type < Variant::PACKED_BYTE_ARRAY || type > Variant::PACKED_VECTOR4_ARRAY))) {
		push_error("@value requires an Array, Dictionary or packed array type.", p_source);
		return false;
	}
	return true;
}

void GDScriptAnalyzer::wgodot_validate_record_layout(GDScriptParser::ClassNode *p_class) {
	using Parser = GDScriptParser;
	using Type = Parser::DataType;
	if (!p_class->wgodot_record) {
		return;
	}
	for (const auto &member : p_class->members) {
		if (member.type == Parser::ClassNode::Member::VARIABLE && member.variable->initializer) {
			Variant value;
			if (!WGodotRecordDeclarations::default_literal(member.variable->initializer, value)) {
				push_error("Record defaults must be data literals or record construction with literal fields.", member.variable);
			}
		}
	}
	HashSet<const Parser::ClassNode *> path;
	std::function<void(Parser::ClassNode *)> visit = [&](Parser::ClassNode *p_record) {
		path.insert(p_record);
		for (const auto &member : p_record->members) {
			if (member.type != Parser::ClassNode::Member::VARIABLE) {
				continue;
			}
			const auto &type = member.variable->type_constraint;
			if (!type.is_hard_type() || type.is_variant() || type.builtin_type == Variant::NIL || type.builtin_type == Variant::CALLABLE || type.builtin_type == Variant::SIGNAL) {
				push_error("Record fields require concrete data types.", member.variable);
				continue;
			}
			const bool container = type.builtin_type == Variant::ARRAY || type.builtin_type == Variant::DICTIONARY || Variant::is_packed_array_type(type.builtin_type);
			if (container && !type.wgodot_value) {
				push_error("Record container fields require @value so record copies own their changes.", member.variable);
			}
			if (type.kind == Type::CLASS && type.class_type->wgodot_record) {
				if (path.has(type.class_type)) {
					push_error("Records cannot contain a cycle of direct record fields; use a value container for recursive data.", member.variable);
					continue;
				}
				resolve_class_interface(type.class_type, member.variable);
				visit(type.class_type);
			}
		}
		path.erase(p_record);
	};
	visit(p_class);
}
