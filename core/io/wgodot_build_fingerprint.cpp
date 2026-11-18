// wgodot-changes::file
#include "wgodot_build_fingerprint.h"

#include "wgodot_file_stamp.h"

#include "core/os/os.h"

#ifdef ANDROID_ENABLED
#include <dlfcn.h>
#endif

const String &WGodotBuildFingerprint::get() {
	static const String fingerprint = []() {
		String path = OS::get_singleton()->get_executable_path();
#ifdef ANDROID_ENABLED
		// Android runs inside app_process. Fingerprint our library, or its APK when
		// Android maps the library directly from the package without extracting it.
		Dl_info library_info{};
		if (dladdr(reinterpret_cast<const void *>(&WGodotBuildFingerprint::get), &library_info) != 0 && library_info.dli_fname) {
			path = String::utf8(library_info.dli_fname).get_slice("!", 0);
		} else {
			return String();
		}
#endif
		const String stamp = WGodotFileStamp::get(path);
		return stamp == "<missing>" ? String() : stamp;
	}();
	return fingerprint;
}
