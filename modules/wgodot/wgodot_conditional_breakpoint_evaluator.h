// wgodot-changes::file
/**************************************************************************/
/*  wgodot_conditional_breakpoint_evaluator.h                             */
/**************************************************************************/

#pragma once

#include "core/error/error_list.h"
#include "core/variant/array.h"

class GDScriptFunction;
class GDScriptInstance;
class Variant;

namespace WGodotConditionalBreakpointEvaluator {

enum BreakDecision {
	BREAK_NOT_MANAGED,
	BREAK_SKIP,
	BREAK_STOP,
};

Error sync_breakpoints(const Array &p_arguments);
BreakDecision evaluate_breakpoint(const String &p_path, int p_line, GDScriptFunction *p_function, GDScriptInstance *p_instance, Variant *p_stack);
bool consume_break_presentation_suppressed();
void reset();

} // namespace WGodotConditionalBreakpointEvaluator
