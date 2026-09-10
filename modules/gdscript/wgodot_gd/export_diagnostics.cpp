// wgodot-changes::file

#include "export_diagnostics.h"

#include "core/io/file_access.h"
#include "core/io/json.h"

namespace WGodotGDScriptExportTransform {

DiagnosticExport::DiagnosticExport(const DiagnosticExport &p_other) {
	*this = p_other;
}

DiagnosticExport &DiagnosticExport::operator=(const DiagnosticExport &p_other) {
	messages = p_other.messages.duplicate(true);
	next_id = p_other.next_id;
	return *this;
}

String DiagnosticExport::add_message(const String &p_path, int p_line, const String &p_function, const String &p_message) {
	const String id = "ERZ_" + itos(next_id++);
	Dictionary entry;
	entry["source"] = p_path;
	entry["line"] = p_line;
	entry["function"] = p_function;
	entry["message"] = p_message;
	messages[id] = entry;
	return id;
}

void DiagnosticExport::reset() {
	messages.clear();
	next_id = 1;
}

Error DiagnosticExport::save_map(const String &p_path) const {
	Error err;
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::WRITE, &err);
	if (err != OK) {
		return err;
	}
	if (!file->store_string(JSON::stringify(messages, "\t", false) + "\n")) {
		return ERR_FILE_CANT_WRITE;
	}
	file->flush();
	return file->get_error();
}

} // namespace WGodotGDScriptExportTransform
