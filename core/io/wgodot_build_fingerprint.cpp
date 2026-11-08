// wgodot-changes::file
#include "wgodot_build_fingerprint.h"

#include "wgodot_file_stamp.h"

#include "core/os/os.h"

const String &WGodotBuildFingerprint::get() {
	static const String fingerprint = WGodotFileStamp::get(OS::get_singleton()->get_executable_path());
	return fingerprint;
}
