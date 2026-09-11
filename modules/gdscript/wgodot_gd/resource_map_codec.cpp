// wgodot-changes::file
/**************************************************************************/
/*  resource_map_codec.cpp                                                */
/**************************************************************************/

#include "resource_map_codec.h"

#include "resource_map_format.h"

#include "core/crypto/crypto_core.h"
#include "core/error/error_macros.h"
#include "core/io/compression.h"
#include "core/io/marshalls.h"

#include <climits>

namespace {

using namespace WGodotGDScriptMapFormat;

bool has_map_magic(const Vector<uint8_t> &p_data) {
	if (p_data.size() < MAP_HEADER_SIZE) {
		return false;
	}
	for (int i = 0; i < 4; i++) {
		if (p_data[i] != MAP_MAGIC[i]) {
			return false;
		}
	}
	return true;
}

} // namespace

namespace WGodotGDScriptResourceMapCodec {

Vector<uint8_t> decode_resource_map(const String &p_path, const Vector<uint8_t> &p_encoded) {
	if (!has_map_magic(p_encoded)) {
		return Vector<uint8_t>();
	}

	const uint64_t raw_size = decode_uint64(&p_encoded[4]);
	if (raw_size > static_cast<uint64_t>(INT32_MAX)) {
		return Vector<uint8_t>();
	}

	const int payload_size = p_encoded.size() - MAP_HEADER_SIZE;
	CryptoCore::AESContext aes;
	if (prepare_cipher(p_path, aes, CryptoCore::AESContext::Mode::DECRYPT) != OK || payload_size == 0) {
		return Vector<uint8_t>();
	}
	Vector<uint8_t> compressed;
	compressed.resize(payload_size);
	if (aes.update(p_encoded.ptr() + MAP_HEADER_SIZE, payload_size, compressed.ptrw(), payload_size) != OK || aes.finish(nullptr, 0) != OK) {
		return Vector<uint8_t>();
	}

	Vector<uint8_t> raw;
	raw.resize(static_cast<int>(raw_size));
	const int64_t decompressed_size = Compression::decompress(raw.ptrw(), raw.size(), compressed.ptr(), compressed.size(), Compression::MODE_ZSTD);
	if (decompressed_size != static_cast<int64_t>(raw_size)) {
		return Vector<uint8_t>();
	}

	return raw;
}

} // namespace WGodotGDScriptResourceMapCodec
