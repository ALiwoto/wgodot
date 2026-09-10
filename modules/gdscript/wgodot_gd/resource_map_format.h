// wgodot-changes::file

#pragma once

#include "core/crypto/crypto_core.h"
#include "core/string/ustring.h"

namespace WGodotGDScriptMapFormat {
inline constexpr uint8_t MAP_MAGIC[4] = { 'W', 'G', 'M', '1' };
inline constexpr int MAP_HEADER_SIZE = 12;

inline Error prepare_cipher(const String &p_path, CryptoCore::AESContext &r_aes, uint8_t r_iv[16]) {
	const Vector<uint8_t> key = p_path.sha256_buffer();
	const Vector<uint8_t> iv_hash = (p_path + "::wgodot-resource-map-iv").sha256_buffer();
	if (key.size() != 32 || iv_hash.size() != 32) {
		return ERR_INVALID_DATA;
	}
	memcpy(r_iv, iv_hash.ptr(), 16);
	// CFB uses the encryption key schedule for both directions.
	return r_aes.set_encode_key(key.ptr(), 256);
}
} // namespace WGodotGDScriptMapFormat
