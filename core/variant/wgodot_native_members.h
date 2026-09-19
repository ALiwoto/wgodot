// wgodot-changes::file
#pragma once

#include "core/variant/variant.h"

#ifdef TOOLS_ENABLED
namespace WGodotNativeMembers {
const char *get_accessor(Variant::Type p_type, const StringName &p_member);
}

#define WGODOT_NATIVE_MEMBER_METADATA(m_base_type, m_member) \
	static constexpr const char *wgodot_cpp_accessor() { \
		return "VariantSetGet_" #m_base_type "_" #m_member; \
	}
#else
#define WGODOT_NATIVE_MEMBER_METADATA(m_base_type, m_member)
#endif

#define WGODOT_NATIVE_MEMBER_GETTER(m_base_type, m_member_type, m_member, m_read) \
	WGODOT_NATIVE_MEMBER_METADATA(m_base_type, m_member) \
	static _FORCE_INLINE_ m_member_type wgodot_get(const m_base_type &p_base) { \
		return m_read; \
	}

#define WGODOT_NATIVE_MEMBER(m_base_type, m_member_type, m_member, m_read, m_write) \
	WGODOT_NATIVE_MEMBER_GETTER(m_base_type, m_member_type, m_member, m_read) \
	static _FORCE_INLINE_ void wgodot_set(m_base_type &r_base, const m_member_type &p_value) { \
		m_write; \
	}

// Preserve the source numeric type, matching Variant's separate int/float setters.
#define WGODOT_NATIVE_NUMBER_MEMBER(m_base_type, m_member_type, m_member, m_read, m_write) \
	WGODOT_NATIVE_MEMBER_GETTER(m_base_type, m_member_type, m_member, m_read) \
	template <typename T> \
	static _FORCE_INLINE_ void wgodot_set(m_base_type &r_base, T p_value) { \
		m_write; \
	}
