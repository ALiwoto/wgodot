// wgodot-changes::file
#include "live_scene_probe.h"
#ifdef TOOLS_ENABLED
#include "core/io/marshalls.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"
#include "scene/main/scene_tree.h"
#include "scene/3d/skeleton_3d.h"

void WLiveSceneProbe::_bind_methods() {
	ClassDB::bind_method(D_METHOD("listen", "port"), &WLiveSceneProbe::listen);
	ClassDB::bind_method(D_METHOD("close"), &WLiveSceneProbe::close);
	ClassDB::bind_method(D_METHOD("reply", "id", "bytes"), &WLiveSceneProbe::reply);
	ClassDB::bind_method(D_METHOD("fail", "id", "message"), &WLiveSceneProbe::fail);
	ClassDB::bind_method(D_METHOD("snapshot", "root", "body"), &WLiveSceneProbe::snapshot, DEFVAL(Variant()));
	ADD_SIGNAL(MethodInfo("command_received", PropertyInfo(Variant::INT, "id"), PropertyInfo(Variant::INT, "command"), PropertyInfo(Variant::PACKED_BYTE_ARRAY, "payload")));
	ADD_SIGNAL(MethodInfo("controller_disconnected"));
}

WLiveSceneProbe::WLiveSceneProbe() {
	set_process_mode(PROCESS_MODE_ALWAYS);
	set_process_internal(true);
	server.instantiate();
}

WLiveSceneProbe::~WLiveSceneProbe() {
	close();
}

Error WLiveSceneProbe::listen(int p_port) {
	ERR_FAIL_COND_V(p_port < 1 || p_port > 65535, ERR_INVALID_PARAMETER);
	ERR_FAIL_COND_V(server->is_listening(), ERR_ALREADY_IN_USE);
	return server->listen(p_port, IPAddress("127.0.0.1"));
}

void WLiveSceneProbe::disconnect_peer() {
	if (peer.is_valid()) {
		peer->disconnect_from_host();
		peer.unref();
	}
	incoming.clear();
	outgoing.clear();
	sent = 0;
	busy = false;
}

void WLiveSceneProbe::close() {
	disconnect_peer();
	server->stop();
	release_skeletons();
}

void WLiveSceneProbe::reply(uint32_t p_id, const PackedByteArray &p_bytes) {
	queue_reply(p_id, 0, p_bytes);
}

void WLiveSceneProbe::queue_reply(uint32_t p_id, uint32_t p_status, const PackedByteArray &p_bytes) {
	ERR_FAIL_COND(!Thread::is_main_thread());
	if (peer.is_null()) {
		return; // The controller can disconnect during asynchronous preparation.
	}
	ERR_FAIL_COND(!busy || p_id != request_id || !outgoing.is_empty());
	if (p_bytes.size() > MAX_MESSAGE_BYTES - 8) {
		fail(p_id, "Live response exceeds the memory budget; narrow the inspection scope.");
		return;
	}
	outgoing.resize(12 + p_bytes.size());
	encode_uint32(8 + p_bytes.size(), outgoing.ptrw());
	encode_uint32(p_id, outgoing.ptrw() + 4);
	encode_uint32(p_status, outgoing.ptrw() + 8);
	if (!p_bytes.is_empty()) {
		memcpy(outgoing.ptrw() + 12, p_bytes.ptr(), p_bytes.size());
	}
	flush_output();
}

void WLiveSceneProbe::fail(uint32_t p_id, const String &p_message) {
	PackedByteArray bytes = p_message.to_utf8_buffer();
	queue_reply(p_id, 1, bytes);
}

bool WLiveSceneProbe::flush_output() {
	if (outgoing.is_empty()) {
		return true;
	}
	int written = 0;
	Error error = peer->put_partial_data(outgoing.ptr() + sent, outgoing.size() - sent, written);
	if (error != OK) {
		disconnect_peer();
		emit_signal(SNAME("controller_disconnected"));
		return false;
	}
	sent += written;
	if (sent == outgoing.size()) {
		outgoing.clear();
		sent = 0;
		busy = false;
	}
	return true;
}

void WLiveSceneProbe::poll() {
	if (peer.is_null()) {
		if (server->is_connection_available()) {
			peer = server->take_connection();
			peer->set_no_delay(true);
		}
		return;
	}
	peer->poll();
	if (peer->get_status() != StreamPeerTCP::STATUS_CONNECTED) {
		disconnect_peer();
		emit_signal(SNAME("controller_disconnected"));
		return;
	}
	if (!flush_output() || busy) {
		return;
	}
	// Read only one framed command. A slow or disconnected host never makes
	// the main thread wait in get_data(), send(), a condition variable or I/O.
	// Consume prefix and body in the same processing slice when available.
	// Splitting them across capped engine frames adds a full frame of latency.
	for (int part = 0; part < 2; part++) {
		int target = incoming.size() < 4 ? 4 : int(decode_uint32(incoming.ptr())) + 4;
		if (incoming.size() >= 4 && (target < 12 || target > MAX_MESSAGE_BYTES)) {
			disconnect_peer();
			emit_signal(SNAME("controller_disconnected"));
			return;
		}
		int available = MIN(peer->get_available_bytes(), target - incoming.size());
		if (available <= 0) {
			break;
		}
		int offset = incoming.size();
		incoming.resize(offset + available);
		int received = 0;
		Error error = peer->get_partial_data(incoming.ptrw() + offset, available, received);
		incoming.resize(offset + received);
		if (error != OK) {
			disconnect_peer();
			emit_signal(SNAME("controller_disconnected"));
			return;
		}
		if (incoming.size() < target) {
			return;
		}
	}
	if (incoming.size() < 12 || incoming.size() != int(decode_uint32(incoming.ptr())) + 4) {
		return;
	}
	request_id = decode_uint32(incoming.ptr() + 4);
	uint32_t command = decode_uint32(incoming.ptr() + 8);
	PackedByteArray payload = incoming.slice(12);
	incoming.clear();
	busy = true;
	emit_signal(SNAME("command_received"), request_id, command, payload);
	if (peer.is_valid()) {
		flush_output();
	}
}

void WLiveSceneProbe::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		get_tree()->connect(SNAME("node_added"), callable_mp(this, &WLiveSceneProbe::node_added));
	} else if (p_what == NOTIFICATION_INTERNAL_PROCESS) {
		poll();
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		get_tree()->disconnect(SNAME("node_added"), callable_mp(this, &WLiveSceneProbe::node_added));
		close();
	}
}
#endif
