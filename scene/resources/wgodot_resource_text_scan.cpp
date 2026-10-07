// wgodot-changes::file

#include "wgodot_resource_text_scan.h"

#include "core/io/file_access.h"

int WGodotResourceTextScan::count_resources(const String &p_path) {
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null()) {
		return 0;
	}

	// Progress needs only the tag at the start of a line. Animation key arrays
	// can occupy megabytes on one line; don't decode or allocate that whole line.
	uint8_t buffer[65536];
	char prefix[14];
	uint32_t prefix_size = 0;
	int count = 0;
	bool has_main_resource = false;
	const auto finish_line = [&]() {
		if (prefix_size >= 14 && memcmp(prefix, "[sub_resource ", 14) == 0) {
			count++;
		} else if (!has_main_resource && ((prefix_size >= 10 && memcmp(prefix, "[resource]", 10) == 0) || (prefix_size >= 6 && memcmp(prefix, "[node ", 6) == 0))) {
			count++;
			has_main_resource = true;
		}
		prefix_size = 0;
	};

	while (true) {
		const uint64_t length = file->get_buffer(buffer, sizeof(buffer));
		if (length == 0) {
			break;
		}
		uint64_t cursor = 0;
		while (cursor < length) {
			while (cursor < length && prefix_size < sizeof(prefix) && buffer[cursor] != '\n') {
				const uint8_t character = buffer[cursor++];
				if (prefix_size != 0 || character > ' ') {
					prefix[prefix_size++] = character;
				}
			}
			const uint8_t *newline = static_cast<const uint8_t *>(memchr(buffer + cursor, '\n', length - cursor));
			if (!newline) {
				break;
			}
			finish_line();
			cursor = newline - buffer + 1;
		}
	}
	finish_line();
	return count;
}
