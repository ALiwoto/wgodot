// wgodot-changes::file
#pragma once

#include "core/io/stream_peer.h"

namespace WGodotNative {

inline Error stream_put_data(StreamPeer *p_peer, const PackedByteArray &p_bytes) {
	ERR_FAIL_NULL_V(p_peer, ERR_INVALID_PARAMETER);
	return p_bytes.is_empty() ? OK : p_peer->put_data(p_bytes.ptr(), p_bytes.size());
}

} // namespace WGodotNative
