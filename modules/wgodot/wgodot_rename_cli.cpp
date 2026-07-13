// wgodot-changes::file
/**************************************************************************/
/*  wgodot_rename_cli.cpp                                                 */
/**************************************************************************/

#include "wgodot_rename_cli.h"

#include "wgodot_cli.h"
#include "wgodot_lsp_client.h"
#include "wgodot_workspace_edit.h"

#include "core/io/json.h"
#include "core/string/print_string.h"
#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/gdscript.h"
#endif

namespace WGodotRenameCLI {

namespace {

void print_error(const Dictionary &p_response, bool p_json) {
	if (p_json) {
		print_line(JSON::stringify(p_response, "", true));
	} else {
		print_line("wgodot: " + String(p_response.get("message", "The rename failed.")));
	}
}

int request_editor(const String &p_command, const Dictionary &p_options, Dictionary &r_response) {
	Dictionary request;
	request["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	request["command"] = p_command;
	request["options"] = p_options;
	return WGodotCLI::request_editor_command(request, r_response);
}

bool parse_source_position(const String &p_value, String &r_path, int &r_line, int &r_character) {
	const int character_separator = p_value.rfind(":");
	if (character_separator < 0) {
		return false;
	}
	const int line_separator = p_value.rfind(":", character_separator - 1);
	if (line_separator < 0) {
		return false;
	}
	const String line_text = p_value.substr(line_separator + 1, character_separator - line_separator - 1);
	const String character_text = p_value.substr(character_separator + 1);
	if (!line_text.is_valid_int() || !character_text.is_valid_int() || line_text.to_int() <= 0 || character_text.to_int() <= 0) {
		return false;
	}
	r_path = p_value.left(line_separator);
	r_line = line_text.to_int() - 1;
	r_character = character_text.to_int() - 1;
	return !r_path.is_empty();
}

bool is_valid_new_name(const String &p_name) {
	if (!p_name.is_valid_identifier()) {
		return false;
	}
#ifdef MODULE_GDSCRIPT_ENABLED
	if (GDScriptLanguage::get_singleton() != nullptr) {
		for (const String &reserved : GDScriptLanguage::get_singleton()->get_reserved_words()) {
			if (reserved == p_name) {
				return false;
			}
		}
	}
#endif
	return true;
}

Dictionary make_argument_error(const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["error"] = "invalid_arguments";
	response["message"] = p_message;
	return response;
}

} // namespace

int run_source_info(const Vector<String> &p_arguments) {
	bool json_output = false;
	String target;
	for (const String &argument : p_arguments) {
		if (argument == "--json") {
			json_output = true;
		} else if (target.is_empty()) {
			target = argument;
		} else {
			const Dictionary error = make_argument_error("source_info accepts one target and optional --json.");
			print_error(error, json_output);
			return 2;
		}
	}
	if (target.is_empty()) {
		const Dictionary error = make_argument_error("source_info requires a named class, qualified member, or script path.");
		print_error(error, json_output);
		return 2;
	}
	Dictionary options;
	options["target"] = target;
	Dictionary response;
	const int discovery_result = request_editor("source_info", options, response);
	if (discovery_result != 0 || !(bool)response.get("ok", false)) {
		print_error(response, json_output);
		return discovery_result != 0 ? discovery_result : 4;
	}
	if (json_output) {
		print_line(JSON::stringify(response, "", true));
	} else {
		print_line(vformat("%s [%s] -> %s:%d:%d", String(response.get("name", String())), String(response.get("kind", String())), String(response.get("path", String())), (int)response.get("display_line", 0), (int)response.get("display_column", 0)));
	}
	return 0;
}

int run_rename(const Vector<String> &p_arguments) {
	bool dry_run = false;
	bool json_output = false;
	String source_position;
	Vector<String> operands;
	for (int i = 0; i < p_arguments.size(); i++) {
		const String &argument = p_arguments[i];
		if (argument == "--dry-run") {
			dry_run = true;
		} else if (argument == "--json") {
			json_output = true;
		} else if (argument == "--at") {
			if (i + 1 >= p_arguments.size()) {
				const Dictionary error = make_argument_error("--at requires <file>:<line>:<column>.");
				print_error(error, json_output);
				return 2;
			}
			source_position = p_arguments[++i];
		} else if (argument.begins_with("--")) {
			const Dictionary error = make_argument_error("Unknown rename argument: " + argument);
			print_error(error, json_output);
			return 2;
		} else {
			operands.push_back(argument);
		}
	}

	Dictionary preflight_options;
	String new_name;
	if (source_position.is_empty()) {
		if (operands.size() != 2) {
			const Dictionary error = make_argument_error("rename requires <target> <new-name>, or --at <file>:<line>:<column> <new-name>.");
			print_error(error, json_output);
			return 2;
		}
		preflight_options["target"] = operands[0];
		new_name = operands[1];
	} else {
		if (operands.size() != 1) {
			const Dictionary error = make_argument_error("rename --at requires exactly one <new-name> operand.");
			print_error(error, json_output);
			return 2;
		}
		String path;
		int line = -1;
		int character = -1;
		if (!parse_source_position(source_position, path, line, character)) {
			const Dictionary error = make_argument_error("--at must use <file>:<line>:<column> with one-based positive numbers.");
			print_error(error, json_output);
			return 2;
		}
		preflight_options["path"] = path;
		preflight_options["line"] = line;
		preflight_options["character"] = character;
		new_name = operands[0];
	}
	if (!is_valid_new_name(new_name)) {
		const Dictionary error = make_argument_error("The new name is not a valid non-keyword GDScript identifier: " + new_name);
		print_error(error, json_output);
		return 2;
	}
	preflight_options["new_name"] = new_name;

	Dictionary preflight;
	const int discovery_result = request_editor("rename_preflight", preflight_options, preflight);
	if (discovery_result != 0 || !(bool)preflight.get("ok", false)) {
		print_error(preflight, json_output);
		return discovery_result != 0 ? discovery_result : 4;
	}
	const String old_name = preflight.get("name", String());
	if (old_name == new_name) {
		const Dictionary error = make_argument_error("The old and new symbol names are identical.");
		print_error(error, json_output);
		return 2;
	}

	const Dictionary lsp_result = WGodotLSPClient::request_rename(
			preflight.get("lsp_host", String()),
			preflight.get("lsp_port", 0),
			preflight.get("project_root", String()),
			preflight.get("absolute_path", String()),
			preflight.get("line", -1),
			preflight.get("character", -1),
			new_name);
	if (!(bool)lsp_result.get("ok", false)) {
		print_error(lsp_result, json_output);
		return 4;
	}

	Dictionary result = WGodotWorkspaceEdit::apply(lsp_result.get("workspace_edit", Dictionary()), preflight.get("project_root", String()), old_name, new_name, dry_run);
	if (!(bool)result.get("ok", false)) {
		print_error(result, json_output);
		return 4;
	}
	result["target"] = preflight.get("target", String());
	result["source"] = String(preflight.get("path", String())) + ":" + itos((int)preflight.get("display_line", 0)) + ":" + itos((int)preflight.get("display_column", 0));

	if (!dry_run) {
		Dictionary completion_response;
		request_editor("rename_complete", Dictionary(), completion_response);
		result["editor_refreshed"] = (bool)completion_response.get("ok", false);
	}
	if (json_output) {
		print_line(JSON::stringify(result, "", true));
	} else {
		print_line(vformat("%sRenamed %s to %s: %d semantic edit(s) in %d file(s).", dry_run ? "Dry run: " : "", old_name, new_name, (int)result.get("edit_count", 0), (int)result.get("file_count", 0)));
		const PackedStringArray files = result.get("files", PackedStringArray());
		for (const String &file : files) {
			print_line("- " + file);
		}
	}
	return 0;
}

} // namespace WGodotRenameCLI
