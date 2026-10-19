// wgodot-changes::file
#include "wgodot_cpp_source.h"

#include "core/io/dir_access.h"

#include <algorithm>

WGodotCppSource::~WGodotCppSource() {
	if (stream.is_valid()) {
		stream.unref();
		DirAccess::remove_absolute(path);
	}
}

Error WGodotCppSource::open(const String &p_path) {
	path = p_path;
	stream = FileAccess::open(path, FileAccess::WRITE_READ, &error);
	buffer.reserve(BUFFER_SIZE);
	return error;
}

void WGodotCppSource::flush() {
	if (error != OK || buffer.empty()) {
		return;
	}
	if (!stream->store_buffer(reinterpret_cast<const uint8_t *>(buffer.data()), buffer.size())) {
		error = ERR_FILE_CANT_WRITE;
	}
	buffer.clear();
}

void WGodotCppSource::operator+=(const String &p_text) {
	if (error != OK) {
		return;
	}
	const CharString text = p_text.utf8();
	const char *data = text.get_data();
	size_t remaining = text.length();
	while (remaining && error == OK) {
		const size_t count = std::min(remaining, BUFFER_SIZE - buffer.size());
		buffer.append(data, count);
		data += count;
		remaining -= count;
		if (buffer.size() == BUFFER_SIZE) {
			flush();
		}
	}
}

Error WGodotCppSource::copy_to(FileAccess &p_output, CryptoCore::SHA256Context &p_digest) {
	flush();
	RETURN_IF_ERROR(error);
	stream->flush();
	RETURN_IF_ERROR(stream->get_error());
	uint64_t remaining = stream->get_length();
	stream->seek(0);
	uint8_t chunk[BUFFER_SIZE];
	while (remaining) {
		const uint64_t count = std::min(remaining, uint64_t(BUFFER_SIZE));
		if (stream->get_buffer(chunk, count) != count) {
			return ERR_FILE_CANT_READ;
		}
		if (!p_output.store_buffer(chunk, count)) {
			return ERR_FILE_CANT_WRITE;
		}
		RETURN_IF_ERROR(p_digest.update(chunk, count));
		remaining -= count;
	}
	return OK;
}

Error WGodotCppSource::write_text(FileAccess &p_output, CryptoCore::SHA256Context &p_digest, const String &p_text) {
	const CharString text = p_text.utf8();
	const auto *data = reinterpret_cast<const uint8_t *>(text.get_data());
	if (!p_output.store_buffer(data, text.length())) {
		return ERR_FILE_CANT_WRITE;
	}
	return p_digest.update(data, text.length());
}
