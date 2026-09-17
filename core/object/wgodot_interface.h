// wgodot-changes::file
#pragma once

namespace WGodotNativeInterfaces {
class Builder;
}

// Interface identity does not participate in Object's single inheritance tree.
// Objects expose the adjusted interface pointer through their native query hook.
#define WGD_INTERFACE(m_class) \
public: \
	inline static char wgodot_interface_tag = 0; \
	static const char *wgodot_interface_name() { \
		return #m_class; \
	} \
	static void _bind_interface(WGodotNativeInterfaces::Builder &p_builder); \
\
protected: \
	~m_class() = default; \
\
public:
