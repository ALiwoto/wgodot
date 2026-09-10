// wgodot-changes::file
/**************************************************************************/
/*  deenum_transform.cpp                                                  */
/**************************************************************************/

#include "deenum_transform.h"

#include "core/error/error_macros.h"
#include "core/variant/variant_parser.h"

namespace {

String variant_to_source(const Variant &p_value) {
	String text;
	if (VariantWriter::write_to_string(p_value, text) != OK) {
		return String();
	}

	return text;
}

bool is_script_enum_datatype(const GDScriptParser::DataType &p_datatype) {
	return p_datatype.kind == GDScriptParser::DataType::ENUM && p_datatype.class_type != nullptr;
}

bool enum_name_matches_datatype(const GDScriptParser::EnumNode *p_enum, const GDScriptParser::DataType &p_datatype) {
	return p_enum != nullptr &&
			p_enum->identifier != nullptr &&
			p_enum->identifier->name == p_datatype.enum_type;
}

const GDScriptParser::EnumNode *find_named_enum_for_datatype(const GDScriptParser::DataType &p_datatype) {
	if (!is_script_enum_datatype(p_datatype) || p_datatype.enum_type.is_empty()) {
		return nullptr;
	}

	const GDScriptParser::ClassNode *class_node = p_datatype.class_type;
	for (const GDScriptParser::ClassNode::Member &member : class_node->members) {
		if (member.type == GDScriptParser::ClassNode::Member::ENUM && enum_name_matches_datatype(member.m_enum, p_datatype)) {
			return member.m_enum;
		}
	}

	return nullptr;
}

const GDScriptParser::EnumNode *find_enum_value_owner(const GDScriptParser::DataType &p_datatype, const StringName &p_value_name) {
	if (!is_script_enum_datatype(p_datatype) || p_value_name.is_empty()) {
		return nullptr;
	}

	const GDScriptParser::ClassNode *class_node = p_datatype.class_type;
	if (class_node->has_member(p_value_name)) {
		const GDScriptParser::ClassNode::Member member = class_node->get_member(p_value_name);
		if (member.type == GDScriptParser::ClassNode::Member::ENUM_VALUE) {
			return member.enum_value.parent_enum;
		}
	}

	return find_named_enum_for_datatype(p_datatype);
}

bool datatype_has_enum_value(const GDScriptParser::DataType &p_datatype, const StringName &p_value_name) {
	return !p_value_name.is_empty() && p_datatype.enum_values.has(p_value_name);
}

String int_variant_to_source(const Variant &p_value) {
	if (p_value.get_type() != Variant::INT) {
		return String();
	}

	return itos((int64_t)p_value);
}

} // namespace

namespace WGodotGDScriptExportTransform {

const GDScriptParser::EnumNode *find_deenum_enum_for_datatype(const RewriteContext &p_context, const GDScriptParser::DataType &p_datatype) {
	const GDScriptParser::EnumNode *enum_node = find_named_enum_for_datatype(p_datatype);
	if (should_deenum_enum(p_context, enum_node)) {
		return enum_node;
	}

	return nullptr;
}

bool should_deenum_enum(const RewriteContext &p_context, const GDScriptParser::EnumNode *p_enum) {
	return p_context.options.deconst_exports && p_enum != nullptr && !p_enum->wgodot_no_mangle;
}

bool add_enum_type_replacement(RewriteContext &r_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return false;
	}

	if (find_deenum_enum_for_datatype(r_context, p_type->get_datatype()) == nullptr) {
		return false;
	}

	if (p_type->type_chain.is_empty() || p_type->type_chain[0] == nullptr) {
		return false;
	}

	const GDScriptParser::IdentifierNode *first_type_identifier = p_type->type_chain[0];
	const int start = get_offset(r_context, first_type_identifier->start_line, first_type_identifier->start_column);
	const int end = get_offset(r_context, p_type->end_line, p_type->end_column);
	if (start < 0 || end < start) {
		return false;
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = "int";
	r_context.replacements.push_back(replacement);
	return true;
}

bool add_enum_identifier_reference_replacement(RewriteContext &r_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return false;
	}

	const GDScriptParser::DataType datatype = p_identifier->get_datatype();
	if (!is_script_enum_datatype(datatype)) {
		return false;
	}

	if (datatype.is_meta_type) {
		const GDScriptParser::EnumNode *enum_node = find_deenum_enum_for_datatype(r_context, datatype);
		if (enum_node == nullptr) {
			return false;
		}

		const String text = variant_to_source(p_identifier->reduced_value);
		if (text.is_empty()) {
			return false;
		}

		add_replacement(r_context, p_identifier, text);
		return true;
	}

	const GDScriptParser::EnumNode *enum_node = find_enum_value_owner(datatype, p_identifier->name);
	if (!should_deenum_enum(r_context, enum_node)) {
		return false;
	}

	const String text = int_variant_to_source(p_identifier->reduced_value);
	if (text.is_empty()) {
		return false;
	}

	add_replacement(r_context, p_identifier, text);
	return true;
}

bool add_enum_attribute_reference_replacement(RewriteContext &r_context, const GDScriptParser::SubscriptNode *p_subscript) {
	if (p_subscript == nullptr || !p_subscript->is_attribute || p_subscript->base == nullptr || p_subscript->attribute == nullptr) {
		return false;
	}

	const GDScriptParser::DataType attribute_type = p_subscript->attribute->get_datatype();
	if (is_script_enum_datatype(attribute_type)) {
		if (attribute_type.is_meta_type) {
			const GDScriptParser::EnumNode *enum_node = find_deenum_enum_for_datatype(r_context, attribute_type);
			if (enum_node == nullptr) {
				return false;
			}

			const String text = variant_to_source(p_subscript->reduced_value);
			if (text.is_empty()) {
				return false;
			}

			add_replacement(r_context, p_subscript, text);
			return true;
		}

		const GDScriptParser::EnumNode *enum_node = find_enum_value_owner(attribute_type, p_subscript->attribute->name);
		if (!should_deenum_enum(r_context, enum_node)) {
			return false;
		}

		const String text = int_variant_to_source(p_subscript->reduced_value);
		if (text.is_empty()) {
			return false;
		}

		add_replacement(r_context, p_subscript, text);
		return true;
	}

	const GDScriptParser::DataType base_type = p_subscript->base->get_datatype();
	if (!is_script_enum_datatype(base_type) || !base_type.is_meta_type || !datatype_has_enum_value(base_type, p_subscript->attribute->name)) {
		return false;
	}

	const GDScriptParser::EnumNode *enum_node = find_deenum_enum_for_datatype(r_context, base_type);
	if (enum_node == nullptr) {
		return false;
	}

	const String text = int_variant_to_source(p_subscript->reduced_value);
	if (text.is_empty()) {
		return false;
	}

	add_replacement(r_context, p_subscript, text);
	return true;
}

bool add_enum_declaration_replacement(RewriteContext &r_context, const GDScriptParser::EnumNode *p_enum, bool p_leave_pass) {
	ERR_FAIL_NULL_V(p_enum, false);

	if (r_context.removed_enum_declarations.has(p_enum)) {
		return false;
	}
	r_context.removed_enum_declarations.insert(p_enum);

	int start_line = p_enum->start_line;
	int start_column = p_enum->start_column;
	for (const GDScriptParser::AnnotationNode *annotation : p_enum->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		if (annotation->start_line < start_line || (annotation->start_line == start_line && annotation->start_column < start_column)) {
			start_line = annotation->start_line;
			start_column = annotation->start_column;
		}
	}

	int start = get_offset(r_context, start_line, start_column);
	int end = get_offset(r_context, p_enum->end_line, p_enum->end_column);
	if (start < 0 || end < start) {
		return false;
	}

	const int line_end = get_line_end_offset(r_context, p_enum->end_line);
	bool remove_full_line = false;
	if (line_end >= end) {
		const String trailing = r_context.source.substr(end, line_end - end).strip_edges();
		if (trailing.begins_with(";")) {
			while (end < line_end && r_context.source[end] != ';') {
				end++;
			}
			if (end < line_end) {
				end++;
			}
			while (end < line_end && (r_context.source[end] == ' ' || r_context.source[end] == '\t')) {
				end++;
			}
		} else {
			end = line_end;
			remove_full_line = true;
		}
	}

	if (!p_leave_pass && remove_full_line) {
		const int line_start = get_line_start_offset(r_context, start_line);
		if (line_start >= 0) {
			start = line_start;
		}
		if (p_enum->end_line < r_context.line_offsets.size()) {
			end = r_context.line_offsets[p_enum->end_line];
		}
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = p_leave_pass ? "pass" : "";
	r_context.replacements.push_back(replacement);
	return true;
}

} // namespace WGodotGDScriptExportTransform
