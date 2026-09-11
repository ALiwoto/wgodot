// wgodot-changes::file

#include "stream_peer.h"

Vector<uint8_t> StreamPeer::get_data_bytes(int p_bytes) {
	Vector<uint8_t> data;
	Error err = data.resize(p_bytes);
	if (err != OK || data.size() != p_bytes) {
		return Vector<uint8_t>();
	}

	if (p_bytes == 0) {
		return data;
	}

	uint8_t *w = data.ptrw();
	get_data(&w[0], p_bytes);
	return data;
}
