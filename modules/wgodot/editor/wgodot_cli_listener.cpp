// wgodot-changes::file

#include "wgodot_cli_listener.h"

#include "../wgodot_cli.h"

#include "core/io/json.h"
#include "core/os/os.h"
#include "editor/wgodot_editor_activity.h"

namespace {
constexpr int MAX_PACKET_SIZE = 4 * 1024 * 1024;
constexpr uint64_t CONNECTION_TIMEOUT_MSEC = 5000;

Dictionary failure(const String &p_error, const String &p_message) {
	Dictionary response;
	response["ok"] = false;
	response["error"] = p_error;
	response["message"] = p_message;
	response["protocol"] = WGodotCLI::PROTOCOL_VERSION;
	return response;
}
} // namespace

Dictionary WGodotCLIListener::busy_response() {
	const WGodotEditorActivity::Snapshot activity = WGodotEditorActivity::get_snapshot();
	if (!activity.busy) {
		return Dictionary();
	}
	String message = "The editor is busy: " + activity.stage;
	if (!activity.path.is_empty()) {
		message += " (" + activity.path + ")";
	}
	message += ". Wait for it to finish, then retry the command.";
	Dictionary response = failure("editor_busy", message);
	response["busy"] = true;
	response["stage"] = activity.stage;
	response["path"] = activity.path;
	response["elapsed_msec"] = activity.elapsed_msec;
	return response;
}

bool WGodotCLIListener::authenticate(const Dictionary &p_request) const {
	const CharString expected = token.utf8();
	const CharString received = String(p_request.get("token", String())).utf8();
	uint32_t difference = static_cast<uint32_t>(expected.length() ^ received.length());
	for (int i = 0; i < expected.length(); i++) {
		const uint8_t received_byte = i < received.length() ? static_cast<uint8_t>(received[i]) : 0;
		difference |= static_cast<uint8_t>(expected[i]) ^ received_byte;
	}
	return difference == 0 && String(p_request.get("project_key", String())) == project_key;
}

void WGodotCLIListener::reply(Request &p_request, const Dictionary &p_response) {
	const PackedByteArray bytes = JSON::stringify(p_response, "", true).to_utf8_buffer();
	p_request.packet->put_packet(bytes.ptr(), bytes.size());
	p_request.tcp->disconnect_from_host();
}

void WGodotCLIListener::thread_main(void *p_self) {
	static_cast<WGodotCLIListener *>(p_self)->listen();
}

void WGodotCLIListener::listen() {
	Vector<Request> incoming;
	while (!stopping.is_set()) {
		while (server->is_connection_available()) {
			Request request;
			request.tcp = server->take_connection();
			if (request.tcp.is_null()) {
				break;
			}
			request.packet.instantiate();
			request.packet->set_input_buffer_max_size(MAX_PACKET_SIZE);
			request.packet->set_output_buffer_max_size(MAX_PACKET_SIZE);
			request.packet->set_stream_peer(request.tcp);
			request.accepted_at_msec = OS::get_singleton()->get_ticks_msec();
			incoming.push_back(request);
		}

		const Dictionary busy = busy_response();
		{
			MutexLock lock(ready_mutex);
			// Work may have started after a request was queued but before the editor
			// claimed it. Reject it here as well; never leave it waiting on that work.
			if (!busy.is_empty()) {
				for (Request &request : ready) {
					reply(request, busy);
				}
				ready.clear();
			} else {
				for (int i = ready.size() - 1; i >= 0; i--) {
					Request &request = ready.write[i];
					request.tcp->poll();
					if (request.tcp->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
						ready.remove_at(i);
					} else if (OS::get_singleton()->get_ticks_msec() - request.accepted_at_msec > CONNECTION_TIMEOUT_MSEC) {
						reply(request, failure("editor_unresponsive", "The editor did not accept the command; it has not been executed."));
						ready.remove_at(i);
					}
				}
			}
		}

		for (int i = incoming.size() - 1; i >= 0; i--) {
			Request &request = incoming.write[i];
			request.tcp->poll();
			if (request.packet->get_available_packet_count() > 0) {
				const uint8_t *buffer = nullptr;
				int size = 0;
				JSON json;
				if (request.packet->get_packet(&buffer, size) != OK || size <= 0 || size > MAX_PACKET_SIZE || json.parse(String::utf8(reinterpret_cast<const char *>(buffer), size)) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
					request.tcp->disconnect_from_host();
					incoming.remove_at(i);
					continue;
				}
				request.data = json.get_data();
				if (!authenticate(request.data)) {
					reply(request, failure("authentication_failed", "Authentication failed."));
				} else if (int(request.data.get("protocol", 0)) != WGodotCLI::PROTOCOL_VERSION) {
					reply(request, failure("protocol_mismatch", "The CLI and editor protocol versions do not match."));
				} else if (!busy.is_empty()) {
					reply(request, busy);
				} else {
					MutexLock lock(ready_mutex);
					ready.push_back(request);
				}
				// Socket ownership transfers to the editor only in take_requests().
				incoming.remove_at(i);
			} else if (request.tcp->get_status() != StreamPeerTCP::STATUS_CONNECTED || OS::get_singleton()->get_ticks_msec() - request.accepted_at_msec > CONNECTION_TIMEOUT_MSEC) {
				request.tcp->disconnect_from_host();
				incoming.remove_at(i);
			}
		}
		OS::get_singleton()->delay_usec(5000);
	}
}

Error WGodotCLIListener::start(const Ref<TCPServer> &p_server, const String &p_token, const String &p_project_key) {
	server = p_server;
	token = p_token;
	project_key = p_project_key;
	stopping.clear();
	thread.start(thread_main, this);
	return thread.is_started() ? OK : ERR_CANT_CREATE;
}

void WGodotCLIListener::stop() {
	stopping.set();
	if (thread.is_started()) {
		thread.wait_to_finish();
	}
	ready.clear();
	server.unref();
}

Vector<WGodotCLIListener::Request> WGodotCLIListener::take_requests() {
	MutexLock lock(ready_mutex);
	Vector<Request> requests = ready;
	ready.clear();
	return requests;
}

WGodotCLIListener::~WGodotCLIListener() {
	stop();
}
