// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/io/stream_peer_tcp.h"
#include "core/io/tcp_server.h"
#include "scene/main/node.h"
#include "core/math/transform_3d.h"

class Skeleton3D;

// One local controller, one outstanding command, one bounded response. This is
// an inspection transport, not an archive writer or a gameplay network server.
class WLiveSceneProbe : public Node {
	GDCLASS(WLiveSceneProbe, Node);
	Ref<TCPServer> server;
	Ref<StreamPeerTCP> peer;
	PackedByteArray incoming, outgoing;
	int sent = 0;
	uint32_t request_id = 0;
	bool busy = false;
	struct SkeletonPose {
		Vector<String> names;
		Vector<Transform3D> transforms;
		uint64_t version = 0;
	};
	HashMap<ObjectID, SkeletonPose> skeleton_poses;
	ObjectID snapshot_root;
	void node_added(Node *p_node);
	void skeleton_updated(ObjectID p_id);
	const SkeletonPose &observe_skeleton(Skeleton3D *p_skeleton);
	void release_skeletons();
	void poll();
	bool flush_output();
	void queue_reply(uint32_t p_id, uint32_t p_status, const PackedByteArray &p_bytes);
	void disconnect_peer();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	static constexpr int MAX_MESSAGE_BYTES = 64 * 1024 * 1024;
	Error listen(int p_port);
	void close();
	void reply(uint32_t p_id, const PackedByteArray &p_bytes);
	void fail(uint32_t p_id, const String &p_message);
	PackedByteArray snapshot(Node *p_root, Skeleton3D *p_body = nullptr);
	WLiveSceneProbe();
	~WLiveSceneProbe();
};
#endif
