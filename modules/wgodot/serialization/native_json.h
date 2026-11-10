// wgodot-changes::file
#pragma once

#include "native.h"

namespace WGodotSerialization {
template <bool ByteBlob = true, class T>
Variant json(const T &p_value) {
	if constexpr (ByteBlob && std::is_same_v<T, WGodotNative::WArray<uint8_t, Variant::PACKED_BYTE_ARRAY>>) {
		return encode_base64(p_value.native().ptr(), p_value.size());
	} else if constexpr (WGodotNative::IsWArray<T>::value || WGodotNative::IsPacked<T>::value) {
		Array result;
		for (const auto &item : p_value.native()) {
			result.push_back(json(item));
		}
		return result;
	} else if constexpr (WGodotNative::IsWDictionary<T>::value) {
		Dictionary result;
		for (const auto &item : p_value.native()) {
			result[String(json(item.key))] = json(item.value);
		}
		return result;
	} else if constexpr (std::is_enum_v<T>) {
		return int64_t(p_value);
	} else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, String>) {
		return p_value;
	} else if constexpr (std::is_same_v<T, Vector3>) {
		Array result;
		for (int i = 0; i < 3; ++i) {
			result.push_back(p_value[i]);
		}
		return result;
	} else {
		return p_value.is_valid() ? Variant(p_value->serialize_json_dict()) : Variant();
	}
}

template <bool ByteBlob = true, class T>
bool parse_json(const Variant &p_value, T &r_value) {
	if constexpr (ByteBlob && std::is_same_v<T, WGodotNative::WArray<uint8_t, Variant::PACKED_BYTE_ARRAY>>) {
		PackedByteArray bytes;
		if (p_value.get_type() != Variant::STRING || !decode_base64(p_value, bytes)) {
			return false;
		}
		r_value = T(bytes);
		return true;
	} else if constexpr (WGodotNative::IsWArray<T>::value || WGodotNative::IsPacked<T>::value) {
		if (p_value.get_type() != Variant::ARRAY) {
			return false;
		}
		const Array input = p_value;
		T result;
		for (int64_t i = 0; i < input.size(); ++i) {
			typename T::Element item{};
			if (!parse_json(input[i], item)) {
				return false;
			}
			result.push_back(item);
		}
		r_value = result;
		return true;
	} else if constexpr (WGodotNative::IsWDictionary<T>::value) {
		if (p_value.get_type() != Variant::DICTIONARY) {
			return false;
		}
		const Dictionary input = p_value;
		T result;
		const Array keys = input.keys();
		for (int64_t i = 0; i < keys.size(); ++i) {
			typename T::Key key{};
			typename T::Value value{};
			if constexpr (std::is_same_v<typename T::Key, bool>) {
				const String text = keys[i];
				if (text != "true" && text != "false") {
					return false;
				}
				key = text == "true";
			} else if (!parse_json(keys[i], key)) {
				return false;
			}
			if (!parse_json(input[keys[i]], value)) {
				return false;
			}
			result.set(key, value);
		}
		r_value = result;
		return true;
	} else if constexpr (std::is_same_v<T, bool>) {
		if (p_value.get_type() != Variant::BOOL) {
			return false;
		}
		r_value = bool(p_value);
		return true;
	} else if constexpr (std::is_integral_v<T> || std::is_enum_v<T>) {
		int64_t integer = 0;
		if (!json_integer(p_value, integer)) {
			return false;
		}
		if constexpr (std::is_integral_v<T> && sizeof(T) < sizeof(int64_t)) {
			if (integer < std::numeric_limits<T>::min() || integer > std::numeric_limits<T>::max()) {
				return false;
			}
		}
		r_value = T(integer);
		return true;
	} else if constexpr (std::is_floating_point_v<T>) {
		if (!p_value.is_num() && (p_value.get_type() != Variant::STRING || !String(p_value).is_valid_float())) {
			return false;
		}
		r_value = T(double(p_value));
		return true;
	} else if constexpr (std::is_same_v<T, String>) {
		if (p_value.get_type() != Variant::STRING) {
			return false;
		}
		r_value = p_value;
		return true;
	} else if constexpr (std::is_same_v<T, Vector3>) {
		if (p_value.get_type() != Variant::ARRAY || Array(p_value).size() != 3) {
			return false;
		}
		const Array input = p_value;
		for (int i = 0; i < 3; ++i) {
			if (!parse_json(input[i], r_value[i])) {
				return false;
			}
		}
		return true;
	} else {
		if (p_value.get_type() == Variant::NIL) {
			r_value.unref();
			return true;
		}
		if (p_value.get_type() != Variant::DICTIONARY) {
			return false;
		}
		using Model = std::remove_pointer_t<decltype(r_value.ptr())>;
		auto result = Model::create();
		if (!result->_deserialize_json_dict(p_value)) {
			return false;
		}
		r_value = result;
		return true;
	}
}
} // namespace WGodotSerialization
