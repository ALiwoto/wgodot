// wgodot-changes::file
#pragma once

#include "enet_packet_peer.h"

// Reuse one event when polling. Packet ownership stays inside this object;
// Native and script callers do not need an Array or the peer packet queue.
class ENetConnectionEvent : public RefCounted {
	GDCLASS(ENetConnectionEvent, RefCounted);
	friend class ENetConnection;

	Ref<ENetPacketPeer> peer;
	PackedByteArray packet;
	int channel = 0;
	int data = 0;
	int flags = 0;

protected:
	static void _bind_methods();

public:
	Ref<ENetPacketPeer> get_peer() const { return peer; }
	PackedByteArray get_packet() const { return packet; }
	int get_channel() const { return channel; }
	int get_data() const { return data; }
	int get_flags() const { return flags; }
};
