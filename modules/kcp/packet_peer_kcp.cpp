// wgodot-changes::file
#include "packet_peer_kcp.h"

#include "thirdparty/kcp/ikcp.h"

#include "core/io/marshalls.h"
#include "core/io/packet_peer_dtls.h"
#include "core/object/class_db.h"
#include "core/os/os.h"

namespace {
constexpr int HEADER_SIZE = 24;
// Upstream v1 accepts fewer than 128 fragments in ikcp_send().
constexpr int MAX_SEND_FRAGMENTS = 127;
constexpr uint8_t COMMAND_PUSH = 81;
constexpr uint8_t COMMAND_ACK = 82;
constexpr uint8_t COMMAND_WINDOW_REQUEST = 83;
constexpr uint8_t COMMAND_WINDOW_RESPONSE = 84;
} // namespace

Error PacketPeerKCP::start(const Ref<PacketPeer> &p_transport, int64_t p_conversation, int p_mtu,
		int p_send_window, int p_receive_window, int p_max_pending_segments) {
	if (kcp) {
		return ERR_ALREADY_IN_USE;
	}
	if (p_transport.is_valid() && p_mtu == 0) {
		p_mtu = MIN(1200, p_transport->get_max_packet_size());
	}
	if (p_transport.is_null() || p_transport.ptr() == this || p_conversation < 0 || p_conversation > UINT32_MAX ||
			p_mtu < 50 || p_mtu > 65507 || p_mtu > p_transport->get_max_packet_size() ||
			p_send_window < 1 || p_send_window > 65535 || p_receive_window < 128 || p_receive_window > 65535 ||
			p_max_pending_segments < 1) {
		return ERR_INVALID_PARAMETER;
	}
	PacketPeerUDP *udp = Object::cast_to<PacketPeerUDP>(p_transport.ptr());
	PacketPeerDTLS *dtls = Object::cast_to<PacketPeerDTLS>(p_transport.ptr());
	if ((udp && !udp->is_socket_connected()) || (dtls && dtls->get_status() != PacketPeerDTLS::STATUS_CONNECTED)) {
		return ERR_UNCONFIGURED;
	}
	kcp = ikcp_create(uint32_t(p_conversation), this);
	if (!kcp) {
		return ERR_OUT_OF_MEMORY;
	}
	if (ikcp_setmtu(kcp, p_mtu) != 0) {
		close();
		return ERR_OUT_OF_MEMORY;
	}
	ikcp_wndsize(kcp, p_send_window, p_receive_window);
	ikcp_nodelay(kcp, 0, 20, 2, 0);
	ikcp_setoutput(kcp, output);
	transport = p_transport;
	max_pending_segments = p_max_pending_segments;
	last_error = OK;
	discarded_packets = 0;
	if (udp) {
		udp->set_blocking_mode(false);
	}
	return OK;
}

Error PacketPeerKCP::set_timing(bool p_no_delay, int p_interval, int p_fast_resend, bool p_congestion_control) {
	if (!kcp) {
		return ERR_UNCONFIGURED;
	}
	if (p_interval < 10 || p_interval > 5000 || p_fast_resend < 0) {
		return ERR_INVALID_PARAMETER;
	}
	ikcp_nodelay(kcp, p_no_delay, p_interval, p_fast_resend, !p_congestion_control);
	return OK;
}

int PacketPeerKCP::output(const char *p_data, int p_size, IKCPCB *p_kcp, void *p_user) {
	PacketPeerKCP *peer = static_cast<PacketPeerKCP *>(p_user);
	if (peer->last_error != OK) {
		return -1;
	}
	Error error = peer->transport->put_packet(reinterpret_cast<const uint8_t *>(p_data), p_size);
	// A full nonblocking socket is packet loss, which KCP already retransmits.
	// Do not create a second unbounded outgoing queue or fail the session.
	if (error != OK && error != ERR_BUSY) {
		peer->last_error = error;
	}
	return error == OK ? 0 : -1;
}

bool PacketPeerKCP::validate_datagram(const uint8_t *p_data, int p_size) const {
	if (p_size < HEADER_SIZE || p_size > 65507) {
		return false;
	}
	// Validate the entire datagram before upstream mutates its queues. Incoming
	// MTUs may differ from our outgoing MTU; the UDP wire size bounds payloads.
	while (p_size >= HEADER_SIZE) {
		const uint32_t conversation = decode_uint32(p_data);
		const uint8_t command = p_data[4];
		const uint8_t fragment = p_data[5];
		const uint32_t length = decode_uint32(p_data + 20);
		if (conversation != kcp->conv || length > uint32_t(p_size - HEADER_SIZE)) {
			return false;
		}
		if (command == COMMAND_PUSH) {
			if (fragment >= kcp->rcv_wnd) {
				return false;
			}
		} else if ((command != COMMAND_ACK && command != COMMAND_WINDOW_REQUEST && command != COMMAND_WINDOW_RESPONSE) ||
				length != 0 || fragment != 0) {
			return false;
		}
		p_data += HEADER_SIZE + length;
		p_size -= HEADER_SIZE + length;
	}
	return p_size == 0;
}

bool PacketPeerKCP::validate_receive_queue() const {
	int remaining = 0;
	for (const IQUEUEHEAD *entry = kcp->rcv_queue.next; entry != &kcp->rcv_queue; entry = entry->next) {
		const IKCPSEG *segment = iqueue_entry(entry, IKCPSEG, node);
		if (remaining == 0) {
			remaining = int(segment->frg) + 1;
		}
		if (int(segment->frg) != --remaining) {
			return false;
		}
	}
	// An incomplete final message is normal until the remaining fragments arrive.
	return true;
}

Error PacketPeerKCP::input(const uint8_t *p_data, int p_size) {
	if (!kcp) {
		return ERR_UNCONFIGURED;
	}
	if (last_error != OK) {
		return last_error;
	}
	if (!validate_datagram(p_data, p_size)) {
		discarded_packets++;
		return ERR_INVALID_DATA;
	}
	const uint32_t now = uint32_t(OS::get_singleton()->get_ticks_msec());
	kcp->current = now;
	if (ikcp_input(kcp, reinterpret_cast<const char *>(p_data), p_size) != 0) {
		last_error = ERR_INVALID_DATA;
		return last_error;
	}
	// Duplicate packets also enqueue ACKs. Bound that queue even when callers
	// feed datagrams repeatedly before the next scheduled poll/flush.
	if (kcp->ackcount >= kcp->rcv_wnd) {
		ikcp_update(kcp, now);
		ikcp_flush(kcp);
	}
	return last_error;
}

Error PacketPeerKCP::input_packet(const PackedByteArray &p_packet) {
	return input(p_packet.ptr(), p_packet.size());
}

Error PacketPeerKCP::poll(int p_max_datagrams) {
	if (!kcp) {
		return ERR_UNCONFIGURED;
	}
	if (last_error != OK) {
		return last_error;
	}
	if (p_max_datagrams < 0) {
		return ERR_INVALID_PARAMETER;
	}
	PacketPeerDTLS *dtls = Object::cast_to<PacketPeerDTLS>(transport.ptr());
	for (int received = 0; received < p_max_datagrams; received++) {
		if (dtls) {
			dtls->poll();
			if (dtls->get_status() != PacketPeerDTLS::STATUS_CONNECTED) {
				last_error = ERR_CONNECTION_ERROR;
				break;
			}
		}
		const int available = transport->get_available_packet_count();
		if (available < 0) {
			last_error = ERR_CONNECTION_ERROR;
			break;
		}
		if (available == 0) {
			break;
		}
		const uint8_t *data = nullptr;
		int size = 0;
		Error error = transport->get_packet(&data, size);
		if (error == ERR_BUSY || error == ERR_UNAVAILABLE) {
			break;
		}
		if (error != OK) {
			last_error = error;
			break;
		}
		// DTLS can consume a control record without yielding application data.
		if (size == 0) {
			continue;
		}
		input(data, size);
		if (last_error != OK) {
			break;
		}
	}
	if (last_error == OK && !validate_receive_queue()) {
		last_error = ERR_INVALID_DATA;
	}
	if (last_error == OK) {
		// KCP's 32-bit monotonic clock deliberately wraps; its comparisons handle it.
		ikcp_update(kcp, uint32_t(OS::get_singleton()->get_ticks_msec()));
		if (kcp->state == UINT32_MAX) {
			last_error = ERR_TIMEOUT;
		}
	}
	return last_error;
}

Error PacketPeerKCP::put_packet(const uint8_t *p_buffer, int p_buffer_size) {
	if (!kcp) {
		return ERR_UNCONFIGURED;
	}
	if (last_error != OK) {
		return last_error;
	}
	if (p_buffer_size < 0 || p_buffer_size > get_max_packet_size()) {
		return ERR_INVALID_PARAMETER;
	}
	const int segments = MAX(1, (p_buffer_size + int(kcp->mss) - 1) / int(kcp->mss));
	if (segments > max_pending_segments - ikcp_waitsnd(kcp)) {
		return ERR_BUSY;
	}
	if (ikcp_send(kcp, reinterpret_cast<const char *>(p_buffer), p_buffer_size) != p_buffer_size) {
		// Upstream may have queued part of the message on allocation failure.
		last_error = ERR_OUT_OF_MEMORY;
		return last_error;
	}
	return OK;
}

Error PacketPeerKCP::put_packet_buffer(const Vector<uint8_t> &p_buffer) {
	// PacketPeer's default helper drops empty messages before calling put_packet.
	return put_packet(p_buffer.ptr(), p_buffer.size());
}

Error PacketPeerKCP::get_packet(const uint8_t **r_buffer, int &r_buffer_size) {
	*r_buffer = nullptr;
	r_buffer_size = 0;
	if (!kcp) {
		return ERR_UNCONFIGURED;
	}
	if (last_error != OK) {
		return last_error;
	}
	if (!validate_receive_queue()) {
		last_error = ERR_INVALID_DATA;
		return last_error;
	}
	const int size = ikcp_peeksize(kcp);
	if (size < 0) {
		return ERR_UNAVAILABLE;
	}
	if (receive_buffer.size() < MAX(1, size)) {
		Error error = receive_buffer.resize(MAX(1, size));
		if (error != OK) {
			return error;
		}
	}
	if (ikcp_recv(kcp, reinterpret_cast<char *>(receive_buffer.ptrw()), size) != size) {
		last_error = ERR_INVALID_DATA;
		return last_error;
	}
	*r_buffer = receive_buffer.ptr();
	r_buffer_size = size;
	return OK;
}

int PacketPeerKCP::get_available_packet_count() const {
	if (!is_active()) {
		return 0;
	}
	int count = 0;
	for (const IQUEUEHEAD *entry = kcp->rcv_queue.next; entry != &kcp->rcv_queue; entry = entry->next) {
		const IKCPSEG *segment = iqueue_entry(entry, IKCPSEG, node);
		if (segment->frg == 0) {
			count++;
		}
	}
	return count;
}

int PacketPeerKCP::get_max_packet_size() const {
	return kcp ? MIN(MAX_SEND_FRAGMENTS, max_pending_segments) * int(kcp->mss) : 0;
}

bool PacketPeerKCP::is_active() const {
	return kcp && last_error == OK;
}

Error PacketPeerKCP::get_last_error() const {
	return last_error;
}

int PacketPeerKCP::get_next_poll_delay() const {
	if (!is_active()) {
		return -1;
	}
	const uint32_t now = uint32_t(OS::get_singleton()->get_ticks_msec());
	return MAX(0, int32_t(ikcp_check(kcp, now) - now));
}

int PacketPeerKCP::get_pending_segment_count() const {
	return kcp ? ikcp_waitsnd(kcp) : 0;
}

int PacketPeerKCP::get_buffered_segment_count() const {
	return kcp ? int(kcp->nrcv_buf + kcp->nrcv_que) : 0;
}

int PacketPeerKCP::get_round_trip_time() const {
	return kcp ? kcp->rx_srtt : 0;
}

uint64_t PacketPeerKCP::get_discarded_packet_count() const {
	return discarded_packets;
}

void PacketPeerKCP::close() {
	if (kcp) {
		ikcp_release(kcp);
		kcp = nullptr;
	}
	transport.unref();
	receive_buffer.clear();
	last_error = OK;
	discarded_packets = 0;
}

PacketPeerKCP::~PacketPeerKCP() {
	close();
}

void PacketPeerKCP::_bind_methods() {
	ClassDB::bind_method(D_METHOD("start", "transport", "conversation", "mtu", "send_window", "receive_window", "max_pending_segments"), &PacketPeerKCP::start, DEFVAL(0), DEFVAL(128), DEFVAL(128), DEFVAL(1024));
	ClassDB::bind_method(D_METHOD("set_timing", "no_delay", "interval", "fast_resend", "congestion_control"), &PacketPeerKCP::set_timing, DEFVAL(20), DEFVAL(2), DEFVAL(true));
	ClassDB::bind_method(D_METHOD("poll", "max_datagrams"), &PacketPeerKCP::poll, DEFVAL(64));
	ClassDB::bind_method(D_METHOD("input_packet", "packet"), &PacketPeerKCP::input_packet);
	ClassDB::bind_method(D_METHOD("close"), &PacketPeerKCP::close);
	ClassDB::bind_method(D_METHOD("is_active"), &PacketPeerKCP::is_active);
	ClassDB::bind_method(D_METHOD("get_last_error"), &PacketPeerKCP::get_last_error);
	ClassDB::bind_method(D_METHOD("get_next_poll_delay"), &PacketPeerKCP::get_next_poll_delay);
	ClassDB::bind_method(D_METHOD("get_pending_segment_count"), &PacketPeerKCP::get_pending_segment_count);
	ClassDB::bind_method(D_METHOD("get_max_packet_size"), &PacketPeerKCP::get_max_packet_size);
	ClassDB::bind_method(D_METHOD("get_buffered_segment_count"), &PacketPeerKCP::get_buffered_segment_count);
	ClassDB::bind_method(D_METHOD("get_round_trip_time"), &PacketPeerKCP::get_round_trip_time);
	ClassDB::bind_method(D_METHOD("get_discarded_packet_count"), &PacketPeerKCP::get_discarded_packet_count);
}
