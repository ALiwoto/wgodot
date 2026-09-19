// wgodot-changes::file
#include "inline_constants.h"

namespace {
bool is_inline_constant(const GDScriptParser::ExpressionNode *p_value) {
	if (!p_value->is_constant || !p_value->reduced || p_value->type_constraint.is_meta_type) {
		return false;
	}
	// These values are emitted as literals at each use. Containers and resources
	// need shared storage; callable and signal values need signature analysis.
	switch (p_value->reduced_value.get_type()) {
		case Variant::NIL:
		case Variant::BOOL:
		case Variant::INT:
		case Variant::FLOAT:
		case Variant::STRING:
		case Variant::STRING_NAME:
		case Variant::NODE_PATH:
		case Variant::VECTOR2:
		case Variant::VECTOR2I:
		case Variant::VECTOR3:
		case Variant::VECTOR3I:
		case Variant::VECTOR4:
		case Variant::VECTOR4I:
		case Variant::RECT2:
		case Variant::RECT2I:
		case Variant::TRANSFORM2D:
		case Variant::TRANSFORM3D:
		case Variant::BASIS:
		case Variant::COLOR:
			return true;
		default:
			return false;
	}
}
} // namespace

bool WGodotGDScriptInlineConstants::can_omit_class(const GDScriptParser::ClassNode *p_class) {
	if (p_class->wgodot_is_interface || p_class->is_abstract || p_class->extends_used || !p_class->wgodot_implements.is_empty()) {
		return false;
	}
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		switch (member.type) {
			case GDScriptParser::ClassNode::Member::CONSTANT:
				if (!is_inline_constant(member.constant->initializer)) {
					return false;
				}
				break;
			case GDScriptParser::ClassNode::Member::ENUM:
			case GDScriptParser::ClassNode::Member::ENUM_VALUE:
			case GDScriptParser::ClassNode::Member::GROUP:
				break;
			default:
				return false;
		}
	}
	return true;
}
