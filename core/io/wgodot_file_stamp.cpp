// wgodot-changes::file
#include "wgodot_file_stamp.h"

#include "core/config/project_settings.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#else
#include <sys/stat.h>
#endif

String WGodotFileStamp::get(const String &p_path) {
	String path = ProjectSettings::get_singleton()->globalize_path(p_path);
#ifdef WINDOWS_ENABLED
	path = path.replace("/", "\\");
	if (!path.begins_with("\\\\?\\")) {
		path = path.begins_with("\\\\") ? "\\\\?\\UNC\\" + path.substr(2) : "\\\\?\\" + path;
	}
	WIN32_FILE_ATTRIBUTE_DATA data;
	if (!GetFileAttributesExW((LPCWSTR)path.utf16().get_data(), GetFileExInfoStandard, &data)) {
		return "<missing>";
	}
	if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
		HANDLE handle = CreateFileW((LPCWSTR)path.utf16().get_data(), FILE_READ_ATTRIBUTES,
				FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (handle == INVALID_HANDLE_VALUE) {
			return "<missing>";
		}
		BY_HANDLE_FILE_INFORMATION target;
		const bool valid = GetFileInformationByHandle(handle, &target);
		CloseHandle(handle);
		if (!valid) {
			return "<missing>";
		}
		data.ftLastWriteTime = target.ftLastWriteTime;
		data.ftCreationTime = target.ftCreationTime;
		data.nFileSizeHigh = target.nFileSizeHigh;
		data.nFileSizeLow = target.nFileSizeLow;
	}
	const uint64_t size = (uint64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
	const uint64_t modified = (uint64_t(data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
	const uint64_t created = (uint64_t(data.ftCreationTime.dwHighDateTime) << 32) | data.ftCreationTime.dwLowDateTime;
	return uitos(size) + "-" + uitos(modified) + "-" + uitos(created);
#else
	struct stat data;
	if (stat(path.utf8().get_data(), &data) != 0) {
		return "<missing>";
	}
#ifdef MACOS_ENABLED
	const auto modified = data.st_mtimespec;
	const auto changed = data.st_ctimespec;
#else
	const auto modified = data.st_mtim;
	const auto changed = data.st_ctim;
#endif
	return itos(data.st_size) + "-" + itos(modified.tv_sec) + "-" + itos(modified.tv_nsec) + "-" + itos(changed.tv_sec) + "-" + itos(changed.tv_nsec);
#endif
}
