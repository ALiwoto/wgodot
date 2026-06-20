// wgodot-changes::file
/**************************************************************************/
/*  resource_map_codec.cpp                                                */
/**************************************************************************/

#include "resource_map_codec.h"

#include "core/crypto/crypto_core.h"
#include "core/error/error_macros.h"
#include "core/io/compression.h"
#include "core/io/marshalls.h"

#include <climits>

namespace {

constexpr uint8_t MAP_MAGIC[4] = { 'W', 'G', 'M', '1' };
constexpr int MAP_HEADER_SIZE = 12;

void append_u64(Vector<uint8_t> &r_output, uint64_t p_value) {
	const int offset = r_output.size();
	r_output.resize(offset + 8);
	encode_uint64(p_value, &r_output.write[offset]);
}

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

void make_key_and_iv(const String &p_path, Vector<uint8_t> *r_key, uint8_t r_iv[16]) {
	ERR_FAIL_NULL(r_key);

	*r_key = p_path.sha256_buffer();
	const Vector<uint8_t> iv_hash = (p_path + "::wgodot-resource-map-iv").sha256_buffer();
	for (int i = 0; i < 16; i++) {
		r_iv[i] = i < iv_hash.size() ? iv_hash[i] : 0;
	}
}

Vector<uint8_t> crypt_payload(const String &p_path, const Vector<uint8_t> &p_payload, bool p_encrypt) {
	if (p_payload.is_empty()) {
		return p_payload;
	}

	Vector<uint8_t> key;
	uint8_t iv[16] = {};
	make_key_and_iv(p_path, &key, iv);
	if (key.size() != 32) {
		return Vector<uint8_t>();
	}

	CryptoCore::AESContext aes;
	// CFB uses the AES encryption key schedule for both encryption and decryption.
	const Error key_err = aes.set_encode_key(key.ptr(), 256);
	if (key_err != OK) {
		return Vector<uint8_t>();
	}

	Vector<uint8_t> output;
	output.resize(p_payload.size());
	const Error crypt_err = p_encrypt ?
			aes.encrypt_cfb(p_payload.size(), iv, p_payload.ptr(), output.ptrw()) :
			aes.decrypt_cfb(p_payload.size(), iv, p_payload.ptr(), output.ptrw());
	if (crypt_err != OK) {
		return Vector<uint8_t>();
	}

	return output;
}

} // namespace

namespace WGodotGDScriptResourceMapCodec {

Vector<uint8_t> encode_resource_map(const String &p_path, const Vector<uint8_t> &p_raw) {
	if (p_raw.is_empty()) {
		return p_raw;
	}

	const int64_t max_compressed_size = Compression::get_max_compressed_buffer_size(p_raw.size(), Compression::MODE_ZSTD);
	if (max_compressed_size <= 0) {
		return Vector<uint8_t>();
	}

	Vector<uint8_t> compressed;
	compressed.resize(max_compressed_size);
	const int64_t compressed_size = Compression::compress(compressed.ptrw(), p_raw.ptr(), p_raw.size(), Compression::MODE_ZSTD);
	if (compressed_size <= 0 || compressed_size > max_compressed_size) {
		return Vector<uint8_t>();
	}
	compressed.resize(compressed_size);

	const Vector<uint8_t> encrypted = crypt_payload(p_path, compressed, true);
	if (encrypted.is_empty()) {
		return Vector<uint8_t>();
	}

	Vector<uint8_t> output;
	for (int i = 0; i < 4; i++) {
		output.push_back(MAP_MAGIC[i]);
	}
	append_u64(output, p_raw.size());
	const int payload_offset = output.size();
	output.resize(payload_offset + encrypted.size());
	for (int i = 0; i < encrypted.size(); i++) {
		output.write[payload_offset + i] = encrypted[i];
	}
	return output;
}

Vector<uint8_t> decode_resource_map(const String &p_path, const Vector<uint8_t> &p_encoded) {
	if (!has_map_magic(p_encoded)) {
		return Vector<uint8_t>();
	}

	const uint64_t raw_size = decode_uint64(&p_encoded[4]);
	if (raw_size > static_cast<uint64_t>(INT32_MAX)) {
		return Vector<uint8_t>();
	}

	Vector<uint8_t> encrypted;
	encrypted.resize(p_encoded.size() - MAP_HEADER_SIZE);
	for (int i = 0; i < encrypted.size(); i++) {
		encrypted.write[i] = p_encoded[MAP_HEADER_SIZE + i];
	}

	const Vector<uint8_t> compressed = crypt_payload(p_path, encrypted, false);
	if (compressed.is_empty()) {
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
