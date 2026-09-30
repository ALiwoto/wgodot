// wgodot-changes::file
#pragma once

#if defined(DEBUG_ENABLED) && defined(WGODOT_NATIVE_TRACE_ENABLED)
#define WGODOT_NATIVE_TRACE_ACTIVE
#include "core/object/object_id.h"

namespace WGodotNative {

struct DebugSource {
	const char *file = nullptr;
	int line = 0;
	const char *function = nullptr;
};

// Stack links live on the C++ stack; tracing a call allocates nothing.
class DebugFrame {
	static thread_local const DebugFrame *current;
	const DebugFrame *previous;
	DebugSource source;
	const char *operation;

public:
	DebugFrame(DebugSource p_source, const char *p_operation);
	~DebugFrame();
	DebugFrame(const DebugFrame &) = delete;
	DebugFrame &operator=(const DebugFrame &) = delete;
	static DebugSource current_source();
	static void report_callback_failure(const char *p_reason, DebugSource p_target, ObjectID p_owner, int p_provided, int p_minimum, int p_maximum);
};

class NativeDebug {
public:
	static void initialize();
	static void register_class(const char *p_cpp_name, DebugSource p_source);
};

} // namespace WGodotNative

#define WGODOT_NATIVE_TRACE(m_file, m_line, m_function, ...) \
	([&]() -> decltype(auto) { \
		WGodotNative::DebugFrame native_debug_frame({ m_file, m_line, m_function }, "at"); \
		return (__VA_ARGS__); \
	}())

#define WGODOT_NATIVE_CALLBACK_SOURCE(m_file, m_line, m_function) .with_debug_source({ m_file, m_line, m_function })
#else
#define WGODOT_NATIVE_TRACE(m_file, m_line, m_function, ...) (__VA_ARGS__)
#define WGODOT_NATIVE_CALLBACK_SOURCE(m_file, m_line, m_function)
#endif
