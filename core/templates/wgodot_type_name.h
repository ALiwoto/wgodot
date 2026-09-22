// wgodot-changes::file
#pragma once

#include "core/typedefs.h"

#ifndef WGODOT_NO_RTTI
#include <typeinfo>
#endif

template <typename T>
const char *wgodot_type_name() {
#ifdef WGODOT_NO_RTTI
	return "unknown";
#else
	return typeid(T).name();
#endif
}
