// wgodot-changes::file
#pragma once

#include "wgodot_check_budget.h"

#include "core/error/wgodot_error_capture.h"
#include "core/variant/array.h"
#include "core/variant/dictionary.h"
#include "core/variant/variant.h"

// One collector per analysis task; only its budget is shared between workers.
struct WGodotCheckDiagnostics {
	WGodotCheckBudget *budget = nullptr;
	PackedStringArray output;
	Array diagnostics;
	int errors = 0;
	int warnings = 0;

	static void capture(void *p_userdata, const char *p_file, int p_line, const char *p_message, ErrorHandlerType p_type) {
		auto &self = *static_cast<WGodotCheckDiagnostics *>(p_userdata);
		const bool warning = p_type == ERR_HANDLER_WARNING;
		if (!warning && !self.budget->claim_error()) {
			return;
		}
		warning ? self.warnings++ : self.errors++;
		const String message = String::utf8(p_message);
		self.output.push_back(vformat("wgodot-check: %s: %s (%s:%d)", warning ? "warning" : "error", message, p_file, p_line));
		Dictionary entry;
		entry["path"] = String::utf8(p_file);
		entry["line"] = p_line;
		entry["message"] = message;
		entry["warning"] = warning;
		self.diagnostics.push_back(entry);
	}
};
