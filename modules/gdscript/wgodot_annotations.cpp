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
	register_annotation(MethodInfo("@private"), AnnotationInfo::CLASS_LEVEL, &GDScriptParser::wgodot_noop_annotation);
	register_annotation(MethodInfo("@readonly"), AnnotationInfo::VARIABLE | AnnotationInfo::STATEMENT, &GDScriptParser::wgodot_readonly_annotation);
	register_annotation(MethodInfo("@partial", PropertyInfo(Variant::STRING, "path")), AnnotationInfo::SCRIPT | AnnotationInfo::CLASS, &GDScriptParser::wgodot_noop_annotation, Vector<Variant>(), true);
}

bool GDScriptParser::wgodot_noop_annotation(AnnotationNode *p_annotation, Node *p_target, ClassNode *p_class) {
	(void)p_annotation;
	(void)p_target;
	(void)p_class;
	return true;
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
