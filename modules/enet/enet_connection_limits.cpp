// wgodot-changes::file
#include "enet_connection.h"

#include <cstring>

ENetConnection::EventType ENetConnection::poll_event(const Ref<ENetConnectionEvent> &p_event) {
	ERR_FAIL_COND_V(p_event.is_null(), EVENT_ERROR);
	p_event->peer.unref();
	p_event->packet.clear();
	p_event->channel = 0;
	p_event->data = 0;
	p_event->flags = 0;
	Event event;
	EventType result = service(0, event);
	p_event->peer = event.peer;
	p_event->channel = event.channel_id;
	p_event->data = event.data;
	if (event.packet) {
		p_event->flags = event.packet->flags;
		p_event->packet.resize(event.packet->dataLength);
		if (event.packet->dataLength) {
			memcpy(p_event->packet.ptrw(), event.packet->data, event.packet->dataLength);
		}
		enet_packet_destroy(event.packet);
	}
	return result;
}

Error ENetConnection::set_packet_limits(int p_mtu, int p_max_packet_size, int p_max_waiting_data) {
	ERR_FAIL_NULL_V(host, ERR_UNCONFIGURED);
	// Set limits after create_host, before accepting or connecting any peers.
	for (size_t i = 0; i < host->peerCount; i++) {
		if (host->peers[i].state != ENET_PEER_STATE_DISCONNECTED) {
			return ERR_ALREADY_IN_USE;
		}
	}
	const int overhead = sizeof(ENetProtocolHeader) + sizeof(ENetProtocolSendFragment);
	if (p_mtu < ENET_PROTOCOL_MINIMUM_MTU || p_mtu > ENET_PROTOCOL_MAXIMUM_MTU ||
			p_max_packet_size < 1 || p_max_packet_size > p_mtu - overhead || p_max_waiting_data < p_max_packet_size) {
		return ERR_INVALID_PARAMETER;
	}
	host->mtu = p_mtu;
	host->maximumPacketSize = p_max_packet_size;
	host->maximumWaitingData = p_max_waiting_data;
	return OK;
}
