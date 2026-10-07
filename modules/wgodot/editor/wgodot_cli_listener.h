// wgodot-changes::file

#pragma once

#include "core/io/packet_peer.h"
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "core/os/mutex.h"
#include "core/os/thread.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/dictionary.h"

class WGodotCLIListener {
public:
	struct Request {
		Ref<StreamPeerTCP> tcp;
		Ref<PacketPeerStream> packet;
		Dictionary data;
		uint64_t accepted_at_msec = 0;
	};

private:
	Ref<TCPServer> server;
	String token;
	String project_key;
	Thread thread;
	SafeFlag stopping;
	Mutex ready_mutex;
	Vector<Request> ready;

	static void thread_main(void *p_self);
	void listen();
	bool authenticate(const Dictionary &p_request) const;
	static void reply(Request &p_request, const Dictionary &p_response);

public:
	Error start(const Ref<TCPServer> &p_server, const String &p_token, const String &p_project_key);
	void stop();
	Vector<Request> take_requests();
	static Dictionary busy_response();
	~WGodotCLIListener();
};
