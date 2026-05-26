// wgodot-changes::file
/**************************************************************************/
/*  wgodot_annotations.cpp                                                */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "gdscript_parser.h"

void GDScriptParser::register_wgodot_annotations() {
	register_annotation(MethodInfo("@private"), AnnotationInfo::CLASS_LEVEL, &GDScriptParser::wgodot_private_annotation);
	register_annotation(MethodInfo("@readonly"), AnnotationInfo::VARIABLE | AnnotationInfo::STATEMENT, &GDScriptParser::wgodot_readonly_annotation);
	register_annotation(MethodInfo("@partial", PropertyInfo(Variant::STRING, "path")), AnnotationInfo::SCRIPT | AnnotationInfo::CLASS, &GDScriptParser::wgodot_noop_annotation, Vector<Variant>(), true);
}

bool GDScriptParser::wgodot_noop_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	(void)p_annotation;
	(void)p_target;
	(void)p_class;
	return true;
}

bool GDScriptParser::wgodot_private_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	(void)p_class;

	if (p_target == nullptr) {
		push_error(R"("@private" annotation can only be applied to class-level members.)", p_annotation);
		return false;
	}

	switch (p_target->type) {
		case Node::CLASS: {
			ClassNode *class_node = static_cast<ClassNode *>(p_target);
			if (class_node->wgodot_private) {
				push_error(R"("@private" annotation can only be used once per class.)", p_annotation);
				return false;
			}
			class_node->wgodot_private = true;
			return true;
		}
		case Node::CONSTANT: {
			ConstantNode *constant = static_cast<ConstantNode *>(p_target);
			if (constant->wgodot_private) {
				push_error(R"("@private" annotation can only be used once per constant.)", p_annotation);
				return false;
			}
			constant->wgodot_private = true;
			return true;
		}
		case Node::FUNCTION: {
			FunctionNode *function = static_cast<FunctionNode *>(p_target);
			if (function->wgodot_private) {
				push_error(R"("@private" annotation can only be used once per function.)", p_annotation);
				return false;
			}
			function->wgodot_private = true;
			return true;
		}
		case Node::SIGNAL: {
			SignalNode *signal = static_cast<SignalNode *>(p_target);
			if (signal->wgodot_private) {
				push_error(R"("@private" annotation can only be used once per signal.)", p_annotation);
				return false;
			}
			signal->wgodot_private = true;
			return true;
		}
		case Node::VARIABLE: {
			VariableNode *variable = static_cast<VariableNode *>(p_target);
			if (variable->wgodot_private) {
				push_error(R"("@private" annotation can only be used once per variable.)", p_annotation);
				return false;
			}
			variable->wgodot_private = true;
			return true;
		}
		default:
			push_error(R"("@private" annotation can only be applied to class-level members.)", p_annotation);
			return false;
	}
}

bool GDScriptParser::wgodot_readonly_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	(void)p_class;

	if (p_target == nullptr || p_target->type != Node::VARIABLE) {
		push_error(R"("@readonly" annotation can only be applied to variables.)", p_annotation);
		return false;
	}

	VariableNode *variable = static_cast<VariableNode *>(p_target);
	if (variable->wgodot_readonly) {
		push_error(R"("@readonly" annotation can only be used once per variable.)", p_annotation);
		return false;
	}

	variable->wgodot_readonly = true;
	return true;
}
