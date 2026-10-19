// wgodot-changes::file
#pragma once

#include "core/crypto/crypto_core.h"
#include "core/io/file_access.h"

#include <string>

// A resource factory discovers declarations while emitting its body. Spool the
// two sections separately, then assemble them without full UTF-32 text copies.
class WGodotCppSource {
	static constexpr size_t BUFFER_SIZE = 65536;
	String path;
	Ref<FileAccess> stream;
	std::string buffer;
	Error error = OK;
	void flush();

public:
	WGodotCppSource() = default;
	WGodotCppSource(const WGodotCppSource &) = delete;
	WGodotCppSource &operator=(const WGodotCppSource &) = delete;
	~WGodotCppSource();
	Error open(const String &p_path);
	void operator+=(const String &p_text);
	Error copy_to(FileAccess &p_output, CryptoCore::SHA256Context &p_digest);
	static Error write_text(FileAccess &p_output, CryptoCore::SHA256Context &p_digest, const String &p_text);
};
