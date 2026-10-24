// wgodot-changes::file
#include "renderdoc_capture.h"
#include "gpu_profile.h"

#ifdef TOOLS_ENABLED
#include "core/io/config_file.h"
#include "core/io/dir_access.h"
#include "core/os/os.h"
#include "thirdparty/renderdoc/renderdoc_app.h"

namespace {
RENDERDOC_API_1_6_0 *api = nullptr;
void *library = nullptr;
Ref<ConfigFile> metadata;
String destination;
String prefix;
bool active = false;
uint32_t capture_count = 0;
Error failure = OK;
}

Error WRenderDocCapture::initialize(const String &p_library) {
	ERR_FAIL_COND_V(api, ERR_ALREADY_IN_USE);
	Error error = OS::get_singleton()->open_dynamic_library(p_library, library);
	ERR_FAIL_COND_V(error != OK, error);
	void *symbol = nullptr;
	error = OS::get_singleton()->get_dynamic_library_symbol_handle(library, "RENDERDOC_GetAPI", symbol);
	ERR_FAIL_COND_V(error != OK, error);
	auto get_api = reinterpret_cast<pRENDERDOC_GetAPI>(symbol);
	ERR_FAIL_COND_V(get_api(eRENDERDOC_API_Version_1_6_0, reinterpret_cast<void **>(&api)) != 1, ERR_UNAVAILABLE);
	api->MaskOverlayBits(0, 0);
	api->SetCaptureKeys(nullptr, 0);
	api->SetFocusToggleKeys(nullptr, 0);
	return OK;
}

bool WRenderDocCapture::is_available() {
	return api != nullptr;
}

Error WRenderDocCapture::request(const String &p_directory, int p_index, int64_t p_frame, double p_seconds) {
	ERR_FAIL_COND_V(!api, ERR_UNAVAILABLE);
	ERR_FAIL_COND_V(failure != OK, failure);
	ERR_FAIL_COND_V(metadata.is_valid(), ERR_ALREADY_IN_USE);
	Error error = DirAccess::make_dir_recursive_absolute(p_directory);
	ERR_FAIL_COND_V(error != OK, error);
	prefix = p_directory.path_join(vformat("frame_%08d", p_index));
	destination = prefix + "_renderdoc.ini";
	metadata.instantiate();
	metadata->set_value("capture", "state", "pending");
	metadata->set_value("capture", "process_frame", p_frame);
	metadata->set_value("capture", "seconds", p_seconds);
	failure = metadata->save(destination);
	return failure;
}

Error WRenderDocCapture::get_result() {
	if (failure != OK) {
		return failure;
	}
	return metadata.is_valid() ? ERR_BUSY : OK;
}

void WRenderDocCapture::begin_frame() {
	if (metadata.is_null() || active) {
		return;
	}
	api->SetCaptureFilePathTemplate(prefix.utf8().get_data());
	WGodotGPUProfile::Scope profile_scope("renderdoc.begin");
	capture_count = api->GetNumCaptures();
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	api->StartFrameCapture(nullptr, nullptr);
	metadata->set_value("capture", "begin_usec", int64_t(OS::get_singleton()->get_ticks_usec() - started));
	active = api->IsFrameCapturing() != 0;
	metadata->set_value("capture", "state", active ? "capturing" : "failed");
	failure = metadata->save(destination);
	if (!active) {
		if (failure == OK) {
			failure = ERR_UNAVAILABLE;
		}
		metadata.unref();
	}
}

void WRenderDocCapture::end_frame() {
	if (!active) {
		return;
	}
	WGodotGPUProfile::Scope profile_scope("renderdoc.end");
	const uint64_t started = OS::get_singleton()->get_ticks_usec();
	bool success = api->EndFrameCapture(nullptr, nullptr) != 0;
	metadata->set_value("capture", "end_usec", int64_t(OS::get_singleton()->get_ticks_usec() - started));
	active = false;
	success = success && api->GetNumCaptures() == capture_count + 1;
	uint32_t length = 0;
	if (success) {
		success = api->GetCapture(capture_count, nullptr, &length, nullptr) != 0 && length > 0;
	}
	if (success) {
		Vector<char> path;
		path.resize(length + 1);
		path.write[length] = 0;
		success = api->GetCapture(capture_count, path.ptrw(), &length, nullptr) != 0;
		if (success) {
			metadata->set_value("capture", "file", String::utf8(path.ptr()).get_file());
		}
	}
	metadata->set_value("capture", "state", success ? "finished" : "failed");
	if (!success && failure == OK) {
		failure = ERR_CANT_CREATE;
	}
	metadata->set_value("capture", "error", failure);
	Error save_error = metadata->save(destination);
	if (save_error != OK) {
		failure = save_error;
	}
	metadata.unref();
}
#endif
