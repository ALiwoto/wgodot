// wgodot-changes::file
/**************************************************************************/
/*  deconst_transform.cpp                                                 */
/**************************************************************************/

#include "deconst_transform.h"

#include "export_transform_internal.h"

#include "core/error/error_macros.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant_parser.h"

namespace {

String variant_to_source(const Variant &p_value) {
	String text;
	if (VariantWriter::write_to_string(p_value, text) != OK) {
		return String();
	}

	return text;
}

bool get_string_literal_value(const Variant &p_value, String *r_value) {
	ERR_FAIL_NULL_V(r_value, false);

	switch (p_value.get_type()) {
		case Variant::STRING:
			*r_value = p_value;
			return true;
		case Variant::STRING_NAME:
			*r_value = String(StringName(p_value));
			return true;
		case Variant::NODE_PATH:
			*r_value = String(NodePath(p_value));
			return true;
		default:
			return false;
	}
}

String variant_to_export_source(WGodotGDScriptExportTransform::RewriteContext *p_context, const Variant &p_value) {
	if (p_context != nullptr) {
		String value;
		if (get_string_literal_value(p_value, &value)) {
			const String replacement = WGodotGDScriptExportTransform::get_export_string_literal_replacement(*p_context, p_value.get_type(), value);
			if (!replacement.is_empty()) {
				return replacement;
			}
		}
	}

	return variant_to_source(p_value);
}

bool is_array_source_type(Variant::Type p_type) {
	switch (p_type) {
		case Variant::ARRAY:
		case Variant::PACKED_BYTE_ARRAY:
		case Variant::PACKED_INT32_ARRAY:
		case Variant::PACKED_INT64_ARRAY:
		case Variant::PACKED_FLOAT32_ARRAY:
		case Variant::PACKED_FLOAT64_ARRAY:
		case Variant::PACKED_STRING_ARRAY:
		case Variant::PACKED_VECTOR2_ARRAY:
		case Variant::PACKED_VECTOR3_ARRAY:
		case Variant::PACKED_COLOR_ARRAY:
		case Variant::PACKED_VECTOR4_ARRAY:
			return true;
		default:
			return false;
	}
}

bool variant_to_untyped_container_source(WGodotGDScriptExportTransform::RewriteContext *p_context, const Variant &p_value, String &r_text);

bool array_to_untyped_source(WGodotGDScriptExportTransform::RewriteContext *p_context, const Variant &p_value, String &r_text) {
	if (p_value.get_type() != Variant::ARRAY) {
		return false;
	}

	const Array array = p_value;
	String text = "[";
	for (int i = 0; i < array.size(); i++) {
		String element_text;
		if (!variant_to_untyped_container_source(p_context, array[i], element_text)) {
			return false;
		}

		if (i > 0) {
			text += ", ";
		}
		text += element_text;
	}
	text += "]";
	r_text = text;
	return true;
}

bool indexed_array_to_untyped_source(WGodotGDScriptExportTransform::RewriteContext *p_context, const Variant &p_value, String &r_text) {
	if (!is_array_source_type(p_value.get_type()) || p_value.get_type() == Variant::ARRAY) {
		return false;
	}

	String text = "[";
	const uint64_t size = p_value.get_indexed_size();
	for (uint64_t i = 0; i < size; i++) {
		bool valid = false;
		bool oob = false;
		const Variant element = p_value.get_indexed(i, valid, oob);
		if (!valid || oob) {
			return false;
		}

		String element_text;
		if (!variant_to_untyped_container_source(p_context, element, element_text)) {
			return false;
		}

		if (i > 0) {
			text += ", ";
		}
		text += element_text;
	}
	text += "]";
	r_text = text;
	return true;
}

bool dictionary_to_untyped_source(WGodotGDScriptExportTransform::RewriteContext *p_context, const Variant &p_value, String &r_text) {
	if (p_value.get_type() != Variant::DICTIONARY) {
		return false;
	}

	const Dictionary dictionary = p_value;
	String text = "{";
	bool first = true;
	for (const KeyValue<Variant, Variant> &entry : dictionary) {
		String key_text;
		String value_text;
		if (!variant_to_untyped_container_source(p_context, entry.key, key_text) ||
				!variant_to_untyped_container_source(p_context, entry.value, value_text)) {
			return false;
		}

		if (!first) {
			text += ", ";
		}
		first = false;
		text += key_text + ": " + value_text;
	}
	text += "}";
	r_text = text;
	return true;
}

bool variant_to_untyped_container_source(WGodotGDScriptExportTransform::RewriteContext *p_context, const Variant &p_value, String &r_text) {
	if (p_value.get_type() == Variant::ARRAY) {
		return array_to_untyped_source(p_context, p_value, r_text);
	}

	if (is_array_source_type(p_value.get_type())) {
		return indexed_array_to_untyped_source(p_context, p_value, r_text);
	}

	if (p_value.get_type() == Variant::DICTIONARY) {
		return dictionary_to_untyped_source(p_context, p_value, r_text);
	}

	r_text = variant_to_export_source(p_context, p_value);
	return !r_text.is_empty();
}

bool constant_to_indexable_source(WGodotGDScriptExportTransform::RewriteContext *p_context, const GDScriptParser::ConstantNode *p_constant, String &r_text) {
	ERR_FAIL_NULL_V(p_constant, false);
	ERR_FAIL_NULL_V(p_constant->initializer, false);

	const Variant value = p_constant->initializer->reduced_value;
	if (!is_array_source_type(value.get_type()) && value.get_type() != Variant::DICTIONARY) {
		return false;
	}

	String text;
	if (!variant_to_untyped_container_source(p_context, value, text) || text.is_empty()) {
		return false;
	}

	r_text = "(" + text + ")";
	return true;
}

bool should_mangle_constant(const GDScriptParser::ConstantNode *p_constant) {
	return p_constant != nullptr && !p_constant->wgodot_no_mangle;
}

} // namespace

namespace WGodotGDScriptExportTransform {

bool should_deconst_constant(const RewriteContext &p_context, const GDScriptParser::ConstantNode *p_constant) {
	return p_context.options.deconst_exports && should_mangle_constant(p_constant);
}

bool is_declared_constant_identifier(const RewriteContext &p_context, const GDScriptParser::IdentifierNode *p_identifier) {
	if (p_identifier == nullptr) {
		return false;
	}

	if (p_identifier->source != GDScriptParser::IdentifierNode::LOCAL_CONSTANT &&
			p_identifier->source != GDScriptParser::IdentifierNode::MEMBER_CONSTANT) {
		return false;
	}

	return should_deconst_constant(p_context, p_identifier->constant_source);
}

const GDScriptParser::ConstantNode *get_declared_constant_reference(const RewriteContext &p_context, const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return nullptr;
	}

	if (p_expression->type == GDScriptParser::Node::IDENTIFIER) {
		const GDScriptParser::IdentifierNode *identifier = static_cast<const GDScriptParser::IdentifierNode *>(p_expression);
		return is_declared_constant_identifier(p_context, identifier) ? identifier->constant_source : nullptr;
	}

	if (p_expression->type == GDScriptParser::Node::SUBSCRIPT) {
		const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
		if (subscript->is_attribute && is_declared_constant_identifier(p_context, subscript->attribute)) {
			return subscript->attribute->constant_source;
		}
	}

	return nullptr;
}

void add_constant_reference_replacement(RewriteContext &r_context, const GDScriptParser::Node *p_node, const GDScriptParser::ConstantNode *p_constant) {
	ERR_FAIL_NULL(p_constant);
	ERR_FAIL_NULL(p_constant->initializer);

	const String text = variant_to_export_source(&r_context, p_constant->initializer->reduced_value);
	if (text.is_empty()) {
		return;
	}

	add_replacement(r_context, p_node, text);
}

bool add_constant_indexed_reference_replacement(RewriteContext &r_context, const GDScriptParser::SubscriptNode *p_subscript, bool &r_replaced_whole_expression) {
	ERR_FAIL_NULL_V(p_subscript, false);

	r_replaced_whole_expression = false;
	if (p_subscript->is_attribute || p_subscript->base == nullptr || p_subscript->index == nullptr) {
		return false;
	}

	const GDScriptParser::ConstantNode *constant = get_declared_constant_reference(r_context, p_subscript->base);
	if (constant == nullptr) {
		return false;
	}

	if (p_subscript->is_constant) {
		String value_text;
		if (!variant_to_untyped_container_source(&r_context, p_subscript->reduced_value, value_text) || value_text.is_empty()) {
			return false;
		}

		add_replacement(r_context, p_subscript, value_text);
		r_replaced_whole_expression = true;
		return true;
	}

	String base_text;
	if (!constant_to_indexable_source(&r_context, constant, base_text)) {
		return false;
	}

	add_replacement(r_context, p_subscript->base, base_text);
	return true;
}

void add_constant_declaration_replacement(RewriteContext &r_context, const GDScriptParser::ConstantNode *p_constant, bool p_leave_pass) {
	ERR_FAIL_NULL(p_constant);

	int start_line = p_constant->start_line;
	int start_column = p_constant->start_column;
	for (const GDScriptParser::AnnotationNode *annotation : p_constant->annotations) {
		if (annotation == nullptr) {
			continue;
		}
		if (annotation->start_line < start_line || (annotation->start_line == start_line && annotation->start_column < start_column)) {
			start_line = annotation->start_line;
			start_column = annotation->start_column;
		}
	}

	int start = get_offset(r_context, start_line, start_column);
	int end = get_offset(r_context, p_constant->end_line, p_constant->end_column);
	if (start < 0 || end < start) {
		return;
	}

	const int line_end = get_line_end_offset(r_context, p_constant->end_line);
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
		if (p_constant->end_line < r_context.line_offsets.size()) {
			end = r_context.line_offsets[p_constant->end_line];
		}
	}

	Replacement replacement;
	replacement.start = start;
	replacement.end = end;
	replacement.text = p_leave_pass ? "pass" : "";
	r_context.replacements.push_back(replacement);
}

} // namespace WGodotGDScriptExportTransform
