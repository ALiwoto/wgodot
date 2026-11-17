// wgodot-changes::file
#pragma once

#include "error_macros.h"

// Diagnostic tools can collect errors on their own thread without disabling
// logging for the editor, other jobs, or the running game.
class WGodotErrorCapture {
public:
	using Callback = void (*)(void *, const char *, int, const char *, ErrorHandlerType);

private:
	static inline thread_local WGodotErrorCapture *active = nullptr;
	WGodotErrorCapture *previous;
	Callback callback;
	void *userdata;
	bool capturing = false;

public:
	WGodotErrorCapture(Callback p_callback, void *p_userdata) :
			previous(active), callback(p_callback), userdata(p_userdata) {
		active = this;
	}
	~WGodotErrorCapture() { active = previous; }
	WGodotErrorCapture(const WGodotErrorCapture &) = delete;
	WGodotErrorCapture &operator=(const WGodotErrorCapture &) = delete;

	static bool consume(const char *p_file, int p_line, const char *p_message, ErrorHandlerType p_type) {
		WGodotErrorCapture *capture = active;
		if (!capture || capture->capturing) {
			return false;
		}
		capture->capturing = true;
		capture->callback(capture->userdata, p_file, p_line, p_message, p_type);
		capture->capturing = false;
		return true;
	}
};
