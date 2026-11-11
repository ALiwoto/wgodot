// wgodot-changes::file
#pragma once

#include "core/io/file_access.h"
#include "core/templates/local_vector.h"
#include "core/variant/type_info.h"
#include "core/variant/variant.h"

#include <algorithm>
#include <functional>
#include <type_traits>

// Native resource values only. Constructors, setters and scene wiring stay C++.
// Records are indexed independently of their byte length, so changing an array
// does not change offsets embedded in generated code.
namespace WGodotNativePayload {
constexpr uint32_t MAGIC = 0x31504757; // WGP1
constexpr uint32_t VERSION = 2;
String path(int64_t p_resource);

struct Input {
	Ref<FileAccess> file;
	uint64_t end = 0;
	bool valid = true;

	bool available(uint64_t p_bytes) {
		valid = valid && file->get_position() <= end && p_bytes <= end - file->get_position();
		return valid;
	}
	void bytes(void *p_destination, uint64_t p_size) {
		if (available(p_size)) {
			valid = file->get_buffer(static_cast<uint8_t *>(p_destination), p_size) == p_size;
		}
	}
};

template <class T>
struct Value;

// Numeric Godot math types consist of contiguous components, never pointers or
// owning objects. The assertion prevents accidentally serializing padding.
template <class T, class Component, size_t Count>
struct Numeric {
	static_assert(sizeof(T) == sizeof(Component) * Count);
	static constexpr size_t COMPONENT_SIZE = sizeof(Component);
	static void swap_components(void *p_data, size_t p_bytes) {
#ifdef BIG_ENDIAN_ENABLED
		auto *bytes = static_cast<uint8_t *>(p_data);
		for (size_t offset = 0; offset < p_bytes; offset += sizeof(Component)) {
			std::reverse(bytes + offset, bytes + offset + sizeof(Component));
		}
#endif
	}
	static T read(Input &p_input) {
		T result{};
		p_input.bytes(&result, sizeof(T));
		swap_components(&result, sizeof(T));
		return result;
	}
	static void write(FileAccess &p_file, const T &p_value) {
#ifdef BIG_ENDIAN_ENABLED
		T value = p_value;
		swap_components(&value, sizeof(T));
		p_file.store_buffer(reinterpret_cast<const uint8_t *>(&value), sizeof(T));
#else
		p_file.store_buffer(reinterpret_cast<const uint8_t *>(&p_value), sizeof(T));
#endif
	}
};

#define WG_PAYLOAD_NUMERIC(m_type, m_component, m_count) \
	template <> \
	struct Value<m_type> : Numeric<m_type, m_component, m_count> {};
WG_PAYLOAD_NUMERIC(uint8_t, uint8_t, 1)
WG_PAYLOAD_NUMERIC(int32_t, int32_t, 1)
WG_PAYLOAD_NUMERIC(int64_t, int64_t, 1)
WG_PAYLOAD_NUMERIC(float, float, 1)
WG_PAYLOAD_NUMERIC(double, double, 1)
WG_PAYLOAD_NUMERIC(Vector2, real_t, 2)
WG_PAYLOAD_NUMERIC(Vector2i, int32_t, 2)
WG_PAYLOAD_NUMERIC(Rect2, real_t, 4)
WG_PAYLOAD_NUMERIC(Rect2i, int32_t, 4)
WG_PAYLOAD_NUMERIC(Vector3, real_t, 3)
WG_PAYLOAD_NUMERIC(Vector3i, int32_t, 3)
WG_PAYLOAD_NUMERIC(Transform2D, real_t, 6)
WG_PAYLOAD_NUMERIC(Vector4, real_t, 4)
WG_PAYLOAD_NUMERIC(Vector4i, int32_t, 4)
WG_PAYLOAD_NUMERIC(Plane, real_t, 4)
WG_PAYLOAD_NUMERIC(Quaternion, real_t, 4)
WG_PAYLOAD_NUMERIC(AABB, real_t, 6)
WG_PAYLOAD_NUMERIC(Basis, real_t, 9)
WG_PAYLOAD_NUMERIC(Transform3D, real_t, 12)
WG_PAYLOAD_NUMERIC(Projection, real_t, 16)
WG_PAYLOAD_NUMERIC(Color, float, 4)
#undef WG_PAYLOAD_NUMERIC

template <>
struct Value<bool> {
	static bool read(Input &p_input) { return Value<uint8_t>::read(p_input) != 0; }
	static void write(FileAccess &p_file, bool p_value) { p_file.store_8(p_value); }
};

template <>
struct Value<String> {
	static String read(Input &p_input);
	static void write(FileAccess &p_file, const String &p_value) { p_file.store_pascal_string(p_value); }
};

template <>
struct Value<StringName> {
	static StringName read(Input &p_input) { return StringName(Value<String>::read(p_input)); }
	static void write(FileAccess &p_file, const StringName &p_value) { Value<String>::write(p_file, p_value); }
};

template <>
struct Value<NodePath> {
	static NodePath read(Input &p_input) { return NodePath(Value<String>::read(p_input)); }
	static void write(FileAccess &p_file, const NodePath &p_value) { Value<String>::write(p_file, String(p_value)); }
};

template <class T>
struct Value<Vector<T>> {
	static Vector<T> read(Input &p_input) {
		Vector<T> result;
		const int64_t count = Value<int64_t>::read(p_input);
		if (count < 0) {
			// A negative count stores one value followed by runtime expansion.
			if (count < -int64_t(INT32_MAX)) {
				p_input.valid = false;
				return result;
			}
			const T item = Value<T>::read(p_input);
			if (!p_input.valid || result.resize(-count) != OK) {
				p_input.valid = false;
				return result;
			}
			result.fill(item);
			return result;
		}
		if constexpr (std::is_same_v<T, String>) {
			p_input.valid = p_input.valid && count >= 0 && uint64_t(count) <= (p_input.end - p_input.file->get_position()) / 4;
		} else {
			p_input.valid = p_input.valid && count >= 0 && uint64_t(count) <= (p_input.end - p_input.file->get_position()) / sizeof(T);
		}
		if (!p_input.valid || result.resize(count) != OK) {
			p_input.valid = false;
			return result;
		}
		if constexpr (std::is_same_v<T, String>) {
			for (int64_t i = 0; i < count && p_input.valid; i++) {
				result.ptrw()[i] = Value<String>::read(p_input);
			}
		} else if (count) {
			p_input.bytes(result.ptrw(), count * sizeof(T));
			Value<T>::swap_components(result.ptrw(), count * sizeof(T));
		}
		return result;
	}
	static void write(FileAccess &p_file, const Vector<T> &p_value) {
		bool repeated = p_value.size() > 4;
		for (int64_t i = 1; repeated && i < p_value.size(); i++) {
			if constexpr (std::is_same_v<T, String>) {
				repeated = p_value[0] == p_value[i];
			} else {
				static_assert(sizeof(T) % Value<T>::COMPONENT_SIZE == 0);
				repeated = memcmp(p_value.ptr(), p_value.ptr() + i, sizeof(T)) == 0;
			}
		}
		if (repeated) {
			p_file.store_64(-p_value.size());
			Value<T>::write(p_file, p_value[0]);
			return;
		}
		p_file.store_64(p_value.size());
		if constexpr (std::is_same_v<T, String>) {
			for (const T &value : p_value) {
				Value<T>::write(p_file, value);
			}
		} else {
#ifdef BIG_ENDIAN_ENABLED
			for (const T &value : p_value) {
				Value<T>::write(p_file, value);
			}
#else
			p_file.store_buffer(reinterpret_cast<const uint8_t *>(p_value.ptr()), p_value.size() * sizeof(T));
#endif
		}
	}
};

template <class T, size_t MinimumSize>
struct LocalValues {
	static LocalVector<T> read(Input &p_input) {
		LocalVector<T> result;
		const int64_t count = Value<int64_t>::read(p_input);
		p_input.valid = p_input.valid && count >= 0 && uint64_t(count) <= UINT32_MAX && uint64_t(count) <= (p_input.end - p_input.file->get_position()) / MinimumSize;
		if (!p_input.valid) {
			return result;
		}
		result.resize(count);
		for (int64_t i = 0; i < count && p_input.valid; i++) {
			result[i] = Value<T>::read(p_input);
		}
		return result;
	}
	static void write(FileAccess &p_file, const LocalVector<T> &p_value) {
		p_file.store_64(p_value.size());
		for (const T &key : p_value) {
			Value<T>::write(p_file, key);
		}
	}
};
class Reader {
	Ref<FileAccess> file;
	Vector<uint64_t> offsets;
	bool valid = false;

public:
	explicit Reader(int64_t p_resource);
	bool is_valid() const { return valid; }
	template <class T>
	T read_custom(uint32_t p_slot, uint32_t p_schema) {
		if (!valid || uint64_t(p_slot) + 1 >= uint64_t(offsets.size())) {
			valid = false;
			ERR_FAIL_V_MSG(T(), "Native codec payload slot is missing.");
		}
		file->seek(offsets[p_slot]);
		Input input{ file, offsets[p_slot + 1], true };
		if (!input.available(8) || file->get_32() != Variant::VARIANT_MAX || file->get_32() != p_schema) {
			valid = false;
			ERR_FAIL_V_MSG(T(), "Native codec payload schema mismatch.");
		}
		T result = Value<T>::read(input);
		valid = input.valid && file->get_position() == input.end;
		ERR_FAIL_COND_V_MSG(!valid, T(), "Invalid native codec payload.");
		return result;
	}
	template <class T>
	T read(uint32_t p_slot) {
		if (!valid || uint64_t(p_slot) + 1 >= uint64_t(offsets.size())) {
			valid = false;
			ERR_FAIL_V_MSG(T(), "Native resource payload slot is missing.");
		}
		file->seek(offsets[p_slot]);
		Input input{ file, offsets[p_slot + 1], true };
		if (!input.available(4) || file->get_32() != GetTypeInfo<T>::VARIANT_TYPE) {
			valid = false;
			ERR_FAIL_V_MSG(T(), "Native resource payload type mismatch.");
		}
		T result = Value<T>::read(input);
		valid = input.valid && file->get_position() == input.end;
		ERR_FAIL_COND_V_MSG(!valid, T(), "Invalid native resource payload value.");
		return result;
	}
};

#ifdef TOOLS_ENABLED
class Writer {
	Ref<FileAccess> file;
	Vector<uint64_t> offsets;
	Error error = OK;

public:
	Error open(const String &p_path);
	uint32_t append(const Variant &p_value);
	uint32_t append_custom(uint32_t p_schema, const std::function<void(FileAccess &)> &p_write);
	Error finish();
};
#endif
} // namespace WGodotNativePayload
