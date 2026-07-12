// wgodot-changes::file
/**************************************************************************/
/*  wgodot_cli_editor_plugin.h                                            */
/**************************************************************************/

#pragma once

#include "core/io/packet_peer.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/templates/vector.h"
#include "editor/plugins/editor_plugin.h"

class WGodotCLIEditorPlugin : public EditorPlugin {
	GDCLASS(WGodotCLIEditorPlugin, EditorPlugin);

	struct PendingConnection {
		Ref<StreamPeerTCP> tcp;
		Ref<PacketPeerStream> packet;
		uint64_t accepted_at_msec = 0;
	};

	Ref<TCPServer> server;
	Vector<PendingConnection> connections;
	String token;
	String instance_id;
	String project_root;
	String project_key;
	String discovery_file;

	bool start_server();
	void stop_server();
	void accept_connections();
	void poll_connections();
	void process_request(PendingConnection &p_connection);
	Dictionary make_status_response(const Dictionary &p_request) const;

	static bool secure_token_matches(const String &p_expected, const String &p_received);
	static String generate_random_hex(int p_byte_count);

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	WGodotCLIEditorPlugin();
	~WGodotCLIEditorPlugin();
};
