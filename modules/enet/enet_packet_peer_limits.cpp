// wgodot-changes::file
#include "enet_packet_peer.h"

Error ENetPacketPeer::send_bounded(int p_channel, const PackedByteArray &p_packet, bool p_reliable, int p_max_pending_packets, int p_max_pending_bytes) {
	if (!peer || peer->state != ENET_PEER_STATE_CONNECTED) {
		return ERR_UNCONFIGURED;
	}
	if (p_channel < 0 || p_channel >= (int)peer->channelCount || p_max_pending_packets < 1 || p_max_pending_bytes < 1) {
		return ERR_INVALID_PARAMETER;
	}
	const int overhead = sizeof(ENetProtocolHeader) + sizeof(ENetProtocolSendFragment) + (peer->host->checksum ? sizeof(enet_uint32) : 0);
	if (p_packet.is_empty() || (size_t)p_packet.size() > peer->host->maximumPacketSize || p_packet.size() > (int)peer->mtu - overhead) {
		return ERR_INVALID_PARAMETER;
	}
	// Count commands too: tiny messages must not create an unbounded queue.
	// The caller's small fixed limit bounds this traversal even under backpressure.
	int pending_packets = 1;
	int64_t pending_bytes = p_packet.size();
	ENetList *queues[] = { &peer->outgoingCommands, &peer->outgoingSendReliableCommands, &peer->sentReliableCommands };
	for (ENetList *queue : queues) {
		for (ENetListIterator entry = enet_list_begin(queue); entry != enet_list_end(queue); entry = enet_list_next(entry)) {
			const ENetOutgoingCommand *command = (const ENetOutgoingCommand *)entry;
			pending_packets++;
			pending_bytes += command->fragmentLength;
			if (pending_packets > p_max_pending_packets || pending_bytes > p_max_pending_bytes) {
				return ERR_BUSY;
			}
		}
	}
	if (pending_bytes > p_max_pending_bytes) {
		return ERR_BUSY;
	}
	ENetPacket *packet = enet_packet_create(p_packet.ptr(), p_packet.size(), p_reliable ? ENET_PACKET_FLAG_RELIABLE : 0);
	if (!packet) {
		return ERR_OUT_OF_MEMORY;
	}
	if (enet_peer_send(peer, p_channel, packet) < 0) {
		enet_packet_destroy(packet);
		return FAILED;
	}
	return OK;
}
