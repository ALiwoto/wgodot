// wgodot-changes::file
#pragma once

// Preserve Godot's VM ABI/metadata. The additional editor registration also
// verifies that the implementation provides the typed text overload.
#ifdef TOOLS_ENABLED
#define WGODOT_REGISTER_TEXT_UTILITY(m_name, m_cpp_name) \
	WGodotText::register_utility(#m_name, "VariantUtilityFunctions::" #m_cpp_name, &VariantUtilityFunctions::m_cpp_name)
#else
#define WGODOT_REGISTER_TEXT_UTILITY(m_name, m_cpp_name)
#endif

#define FUNCBIND_TEXT_VARARG_CNAME(m_func, m_cpp_name, m_args, m_category) \
	FUNCBINDVARARGV_CNAME(m_func, m_cpp_name, m_args, m_category); \
	WGODOT_REGISTER_TEXT_UTILITY(m_func, m_cpp_name)

#define FUNCBIND_TEXT_VARARG(m_func, m_args, m_category) \
	FUNCBIND_TEXT_VARARG_CNAME(m_func, m_func, m_args, m_category)

#define FUNCBIND_TEXT_VARARGS(m_func, m_args, m_category) \
	FUNCBINDVARARGS(m_func, m_args, m_category); \
	WGODOT_REGISTER_TEXT_UTILITY(m_func, m_func)
