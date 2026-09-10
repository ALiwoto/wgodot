// wgodot-changes::file

#pragma once

#include "core/error/error_list.h"
#include "core/string/ustring.h"
#include "core/variant/dictionary.h"

namespace WGodotGDScriptExportTransform {

class DiagnosticExport {
	Dictionary messages;
	int64_t next_id = 1;

public:
	DiagnosticExport() = default;
	DiagnosticExport(const DiagnosticExport &p_other);
	DiagnosticExport &operator=(const DiagnosticExport &p_other);
	void reset();
	String add_message(const String &p_path, int p_line, const String &p_function, const String &p_message);
	Error save_map(const String &p_path) const;
};

} // namespace WGodotGDScriptExportTransform
