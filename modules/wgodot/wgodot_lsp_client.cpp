// wgodot-changes::file
/**************************************************************************/
/*  wgodot_lsp_client.cpp                                                 */
/**************************************************************************/

#include "wgodot_lsp_client.h"

#include "core/io/json.h"
#include "core/io/stream_peer_tcp.h"
#include "core/os/os.h"
#include "core/templates/vector.h"

namespace WGodotLSPClient {

namespace {

constexpr uint64_t CONNECT_TIMEOUT_MSEC = 2000;
constexpr uint64_t RESPONSE_TIMEOUT_MSEC = 30000;
constexpr int MAX_HEADER_SIZE = 16 * 1024;
constexpr int MAX_MESSAGE_SIZE = 64 * 1024 * 1024;

Dictionary make_error(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["error"] = p_error;
	response["message"] = p_message;
	return response;
}

String make_file_uri(const String &p_absolute_path) {
	String path = p_absolute_path.replace_char('\\', '/').simplify_path().lstrip("/");
	Vector<String> encoded_parts;
	for (const String &part : path.split("/")) {
		encoded_parts.push_back(part.uri_encode());
	}
	return "file:///" + String("/").join(encoded_parts);
}

bool send_message(const Ref<StreamPeerTCP> &p_tcp, const Dictionary &p_message) {
	const CharString body = JSON::stringify(p_message, "", false).utf8();
	const CharString header = ("Content-Length: " + itos(body.length()) + "\r\n\r\n").utf8();
	return p_tcp->put_data(reinterpret_cast<const uint8_t *>(header.get_data()), header.length()) == OK &&
			p_tcp->put_data(reinterpret_cast<const uint8_t *>(body.get_data()), body.length()) == OK;
}

bool read_byte(const Ref<StreamPeerTCP> &p_tcp, uint8_t &r_byte, uint64_t p_deadline) {
	while (OS::get_singleton()->get_ticks_msec() < p_deadline) {
		p_tcp->poll();
		int received = 0;
		const Error error = p_tcp->get_partial_data(&r_byte, 1, received);
		if (error != OK && error != ERR_BUSY) {
			return false;
		}
		if (received == 1) {
			return true;
		}
		if (p_tcp->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
			return false;
		}
		OS::get_singleton()->delay_usec(100);
	}
	return false;
}

bool read_message(const Ref<StreamPeerTCP> &p_tcp, Dictionary &r_message, String &r_error) {
	const uint64_t deadline = OS::get_singleton()->get_ticks_msec() + RESPONSE_TIMEOUT_MSEC;
	PackedByteArray header_bytes;
	while (header_bytes.size() < MAX_HEADER_SIZE) {
		uint8_t byte = 0;
		if (!read_byte(p_tcp, byte, deadline)) {
			r_error = "Timed out while reading a response from the GDScript language server.";
			return false;
		}
		header_bytes.push_back(byte);
		const int size = header_bytes.size();
		if (size >= 4 && header_bytes[size - 4] == '\r' && header_bytes[size - 3] == '\n' && header_bytes[size - 2] == '\r' && header_bytes[size - 1] == '\n') {
			break;
		}
	}
	if (header_bytes.size() >= MAX_HEADER_SIZE) {
		r_error = "The GDScript language server returned an oversized response header.";
		return false;
	}
	const String header = String::utf8(reinterpret_cast<const char *>(header_bytes.ptr()), header_bytes.size());
	int content_length = -1;
	for (const String &line : header.split("\r\n")) {
		if (line.to_lower().begins_with("content-length:")) {
			const String value = line.substr(line.find(":") + 1).strip_edges();
			if (value.is_valid_int()) {
				content_length = value.to_int();
			}
		}
	}
	if (content_length < 0 || content_length > MAX_MESSAGE_SIZE) {
		r_error = "The GDScript language server returned an invalid response size.";
		return false;
	}

	PackedByteArray body;
	body.resize(content_length);
	int offset = 0;
	while (offset < content_length && OS::get_singleton()->get_ticks_msec() < deadline) {
		p_tcp->poll();
		int received = 0;
		const Error error = p_tcp->get_partial_data(body.ptrw() + offset, content_length - offset, received);
		if (error != OK && error != ERR_BUSY) {
			r_error = "The GDScript language server connection failed while reading a response.";
			return false;
		}
		offset += received;
		if (received == 0) {
			OS::get_singleton()->delay_usec(100);
		}
	}
	if (offset != content_length) {
		r_error = "Timed out while reading a response from the GDScript language server.";
		return false;
	}
	JSON json;
	if (json.parse(String::utf8(reinterpret_cast<const char *>(body.ptr()), body.size())) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		r_error = "The GDScript language server returned invalid JSON.";
		return false;
	}
	r_message = json.get_data();
	return true;
}

bool wait_for_response(const Ref<StreamPeerTCP> &p_tcp, int p_id, Dictionary &r_response, String &r_workspace_path, String &r_error) {
	while (true) {
		Dictionary message;
		if (!read_message(p_tcp, message, r_error)) {
			return false;
		}
		if (String(message.get("method", String())) == "gdscript_client/changeWorkspace") {
			const Dictionary params = message.get("params", Dictionary());
			r_workspace_path = params.get("path", String());
			continue;
		}
		if (message.has("id") && (int)message["id"] == p_id) {
			r_response = message;
			return true;
		}
	}
}

bool response_error(const Dictionary &p_response, String &r_message) {
	if (!p_response.has("error")) {
		return false;
	}
	const Dictionary error = p_response["error"];
	r_message = error.get("message", "The GDScript language server rejected the request.");
	return true;
}

} // namespace

Dictionary request_rename(const String &p_host, int p_port, const String &p_project_root, const String &p_source_path, int p_line, int p_character, const String &p_new_name) {
	Ref<StreamPeerTCP> tcp;
	tcp.instantiate();
	if (tcp->connect_to_host(IPAddress(p_host), p_port) != OK) {
		return make_error("lsp_connect_failed", "Could not connect to the GDScript language server.");
	}
	const uint64_t connect_deadline = OS::get_singleton()->get_ticks_msec() + CONNECT_TIMEOUT_MSEC;
	while (tcp->get_status() == StreamPeerTCP::STATUS_CONNECTING && OS::get_singleton()->get_ticks_msec() < connect_deadline) {
		tcp->poll();
		OS::get_singleton()->delay_usec(100);
	}
	if (tcp->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		return make_error("lsp_connect_failed", "Could not connect to the GDScript language server at " + p_host + ":" + itos(p_port) + ".");
	}

	const String normalized_root = p_project_root.replace_char('\\', '/').simplify_path();
	Dictionary initialize_params;
	initialize_params["processId"] = OS::get_singleton()->get_process_id();
	initialize_params["rootPath"] = normalized_root;
	initialize_params["rootUri"] = make_file_uri(normalized_root);
	initialize_params["capabilities"] = Dictionary();
	Dictionary client_info;
	client_info["name"] = "wgodot-cli";
	client_info["version"] = "1";
	initialize_params["clientInfo"] = client_info;
	Dictionary initialize;
	initialize["jsonrpc"] = "2.0";
	initialize["id"] = 1;
	initialize["method"] = "initialize";
	initialize["params"] = initialize_params;
	if (!send_message(tcp, initialize)) {
		return make_error("lsp_write_failed", "Could not initialize the GDScript language server connection.");
	}
	Dictionary response;
	String workspace_path;
	String error_message;
	if (!wait_for_response(tcp, 1, response, workspace_path, error_message)) {
		return make_error("lsp_initialize_failed", error_message);
	}
	if (response_error(response, error_message)) {
		return make_error("lsp_initialize_failed", error_message);
	}
	if (!workspace_path.is_empty() && workspace_path.replace_char('\\', '/').simplify_path().to_lower() != normalized_root.to_lower()) {
		return make_error("lsp_workspace_mismatch", "The discovered language server belongs to a different Godot project: " + workspace_path);
	}

	Dictionary initialized;
	initialized["jsonrpc"] = "2.0";
	initialized["method"] = "initialized";
	initialized["params"] = Dictionary();
	if (!send_message(tcp, initialized)) {
		return make_error("lsp_write_failed", "Could not finish initializing the GDScript language server connection.");
	}

	Dictionary text_document;
	text_document["uri"] = make_file_uri(p_source_path);
	Dictionary position;
	position["line"] = p_line;
	position["character"] = p_character;
	Dictionary rename_params;
	rename_params["textDocument"] = text_document;
	rename_params["position"] = position;

	Dictionary prepare;
	prepare["jsonrpc"] = "2.0";
	prepare["id"] = 2;
	prepare["method"] = "textDocument/prepareRename";
	prepare["params"] = rename_params;
	if (!send_message(tcp, prepare) || !wait_for_response(tcp, 2, response, workspace_path, error_message)) {
		return make_error("lsp_prepare_failed", error_message.is_empty() ? "Could not prepare the semantic rename." : error_message);
	}
	if (response_error(response, error_message)) {
		return make_error("lsp_prepare_failed", error_message);
	}
	if (!response.has("result") || response["result"].get_type() == Variant::NIL) {
		return make_error("not_renameable", "The symbol at the resolved source position cannot be renamed.");
	}

	rename_params["newName"] = p_new_name;
	Dictionary rename;
	rename["jsonrpc"] = "2.0";
	rename["id"] = 3;
	rename["method"] = "textDocument/rename";
	rename["params"] = rename_params;
	if (!send_message(tcp, rename) || !wait_for_response(tcp, 3, response, workspace_path, error_message)) {
		return make_error("lsp_rename_failed", error_message.is_empty() ? "Could not obtain semantic rename edits." : error_message);
	}
	if (response_error(response, error_message)) {
		return make_error("lsp_rename_failed", error_message);
	}
	if (!response.has("result") || response["result"].get_type() != Variant::DICTIONARY) {
		return make_error("lsp_rename_failed", "The GDScript language server did not return a workspace edit.");
	}

	Dictionary result;
	result["ok"] = true;
	result["workspace_edit"] = response["result"];
	return result;
}

} // namespace WGodotLSPClient
