// wgodot-changes::file

#include "export_maps.h"

#include "core/io/compression.h"
#include "core/io/marshalls.h"

#include "modules/gdscript/wgodot_gd/resource_map_format.h"

namespace WGodotGDScriptResourceMapCodec {
using namespace WGodotGDScriptMapFormat;

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

	CryptoCore::AESContext aes;
	if (prepare_cipher(p_path, aes, CryptoCore::AESContext::Mode::ENCRYPT) != OK) {
		return Vector<uint8_t>();
	}
	Vector<uint8_t> output;
	output.resize(MAP_HEADER_SIZE + compressed.size());
	uint8_t *buffer = output.ptrw();
	memcpy(buffer, MAP_MAGIC, sizeof(MAP_MAGIC));
	encode_uint64(p_raw.size(), buffer + 4);
	if (aes.update(compressed.ptr(), compressed.size(), buffer + MAP_HEADER_SIZE, compressed.size()) != OK || aes.finish(nullptr, 0) != OK) {
		return Vector<uint8_t>();
	}
	return output;
}

} //namespace WGodotGDScriptResourceMapCodec
