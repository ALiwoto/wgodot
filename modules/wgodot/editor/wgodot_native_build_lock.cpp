// wgodot-changes::file
#include "wgodot_native_build_lock.h"

#include "core/config/project_settings.h"
#include "core/io/dir_access.h"

#ifdef WINDOWS_ENABLED
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

Error WGodotNativeBuildLock::acquire(const String &p_directory) {
	RETURN_IF_ERROR(DirAccess::make_dir_recursive_absolute(p_directory));
	const String path = ProjectSettings::get_singleton()->globalize_path(p_directory.path_join(".native-export.lock"));
#ifdef WINDOWS_ENABLED
	handle = reinterpret_cast<intptr_t>(CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16().get_data()), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
	return handle == -1 ? ERR_BUSY : OK;
#else
	handle = ::open(path.utf8().get_data(), O_RDWR | O_CREAT, 0600);
	if (handle < 0) {
		return ERR_CANT_OPEN;
	}
	if (flock(handle, LOCK_EX | LOCK_NB) != 0) {
		::close(handle);
		handle = -1;
		return ERR_BUSY;
	}
	return OK;
#endif
}

WGodotNativeBuildLock::~WGodotNativeBuildLock() {
	if (handle != -1) {
#ifdef WINDOWS_ENABLED
		CloseHandle(reinterpret_cast<HANDLE>(handle));
#else
		::close(handle);
#endif
	}
}
