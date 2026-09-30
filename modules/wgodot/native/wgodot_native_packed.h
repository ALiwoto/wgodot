// wgodot-changes::file
#pragma once

#include "wgodot_native_warray.h"

#include "core/variant/method_ptrcall.h"
#include "core/variant/type_info.h"
#include "core/variant/variant_internal.h"

template <class T, Variant::Type Kind>
struct GetTypeInfo<WGodotNative::WArray<T, Kind>> : GetTypeInfo<Vector<T>> {
	static_assert(Kind != Variant::ARRAY, "Ordinary WArray has no implicit engine ABI.");
};

template <class T, Variant::Type Kind>
struct PtrToArg<WGodotNative::WArray<T, Kind>> {
	static_assert(Kind != Variant::ARRAY, "Ordinary WArray has no implicit engine ABI.");
	using EncodeT = Vector<T>;
	static WGodotNative::WArray<T, Kind> convert(const void *p_pointer) { return *static_cast<const Vector<T> *>(p_pointer); }
	static void encode(const WGodotNative::WArray<T, Kind> &p_value, void *p_pointer) { *static_cast<Vector<T> *>(p_pointer) = p_value.native(); }
};

template <class T, Variant::Type Kind>
struct VariantInternalAccessor<WGodotNative::WArray<T, Kind>> {
	static_assert(Kind != Variant::ARRAY, "Ordinary WArray has no implicit engine ABI.");
	static WGodotNative::WArray<T, Kind> get(const Variant *p_value) { return *p_value; }
	static void set(Variant *p_target, const WGodotNative::WArray<T, Kind> &p_value) { *p_target = Variant(p_value); }
};
