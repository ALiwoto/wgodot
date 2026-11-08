// wgodot-changes::file
#pragma once

#include "core/io/packet_peer.h"

struct IKCPCB;

// Reliable message transport over a connected, nonblocking datagram PacketPeer.
// Session negotiation and application framing belong to the caller.
class PacketPeerKCP : public PacketPeer {
	GDCLASS(PacketPeerKCP, PacketPeer);

	IKCPCB *kcp = nullptr;
	Ref<PacketPeer> transport;
	Vector<uint8_t> receive_buffer;
	int max_pending_segments = 1024;
	Error last_error = OK;
	uint64_t discarded_packets = 0;

	static int output(const char *p_data, int p_size, IKCPCB *p_kcp, void *p_user);
	Error input(const uint8_t *p_data, int p_size);
	bool validate_datagram(const uint8_t *p_data, int p_size) const;
	bool validate_receive_queue() const;

protected:
	static void _bind_methods();

public:
	Error start(const Ref<PacketPeer> &p_transport, int64_t p_conversation, int p_mtu = 0,
			int p_send_window = 128, int p_receive_window = 128, int p_max_pending_segments = 1024);
	Error set_timing(bool p_no_delay, int p_interval = 20, int p_fast_resend = 2, bool p_congestion_control = true);
	Error poll(int p_max_datagrams = 64);
	Error input_packet(const PackedByteArray &p_packet);
	void close();
	bool is_active() const;
	Error get_last_error() const;
	int get_next_poll_delay() const;
	int get_pending_segment_count() const;
	int get_buffered_segment_count() const;
	int get_round_trip_time() const;
	uint64_t get_discarded_packet_count() const;

	int get_available_packet_count() const override;
	int get_max_packet_size() const override;
	Error get_packet(const uint8_t **r_buffer, int &r_buffer_size) override;
	Error put_packet(const uint8_t *p_buffer, int p_buffer_size) override;
	Error put_packet_buffer(const Vector<uint8_t> &p_buffer) override;

	~PacketPeerKCP();
};
