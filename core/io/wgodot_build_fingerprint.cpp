// wgodot-changes::file
#include "wgodot_build_fingerprint.h"

#include "core/io/file_access.h"
#include "core/os/os.h"

const String &WGodotBuildFingerprint::get() {
	static const String fingerprint = FileAccess::get_sha256(OS::get_singleton()->get_executable_path());
	return fingerprint;
}
