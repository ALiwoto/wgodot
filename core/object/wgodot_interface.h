// wgodot-changes::file
#pragma once

// Interface identity does not participate in Object's single inheritance tree.
// Objects expose the adjusted interface pointer through their native query hook.
#define WGD_INTERFACE(m_class) \
public: \
	inline static char wgodot_interface_tag = 0; \
\
protected: \
	~m_class() = default; \
\
public:
