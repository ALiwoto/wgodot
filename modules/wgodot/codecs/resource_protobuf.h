// wgodot-changes::file
#pragma once

#include "native_payload.h"

#include "core/io/resource_loader.h"

#include "modules/wgodot/serialization/wire.h"

// Resource storage has its own schema. User-facing ProtobufSerializable
// annotations and network field numbers never participate in this format.
namespace WGodotResourceProtobuf {
using WGodotSerialization::Reader;
using WGodotSerialization::Writer;

struct Reference {
	uint64_t local = 0;
	int64_t external = 0;
};

class Context {
public:
	virtual Ref<Resource> resolve(const Reference &p_reference) = 0;
	virtual Reference identify(const Ref<Resource> &p_resource) = 0;
	virtual ~Context() = default;
};

template <class T, class = void>
struct Value;

template <class T>
struct Value<T, std::enable_if_t<std::is_integral_v<T> || std::is_enum_v<T>>> {
	static constexpr int WIRE = 0;
	static void write(Writer &r_writer, T p_value, Context &) {
		if constexpr (std::is_same_v<T, bool>) {
			r_writer.varint(p_value);
		} else {
			r_writer.varint(WGodotSerialization::zigzag(int64_t(p_value)));
		}
	}
	static bool read(Reader &r_reader, T &r_value, Context &) {
		const uint64_t encoded = r_reader.varint();
		if constexpr (std::is_same_v<T, bool>) {
			r_reader.valid &= encoded <= 1;
			r_value = encoded != 0;
		} else {
			const int64_t decoded = WGodotSerialization::unzigzag(encoded);
			r_value = T(decoded);
			r_reader.valid &= int64_t(r_value) == decoded;
		}
		return r_reader.valid;
	}
};

template <class T>
struct Value<T, std::enable_if_t<std::is_floating_point_v<T>>> {
	static constexpr int WIRE = sizeof(T) == 4 ? 5 : 1;
	static void write(Writer &r_writer, T p_value, Context &) {
		using Bits = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
		Bits bits;
		memcpy(&bits, &p_value, sizeof(T));
		r_writer.fixed(bits, sizeof(T));
	}
	static bool read(Reader &r_reader, T &r_value, Context &) {
		using Bits = std::conditional_t<sizeof(T) == 4, uint32_t, uint64_t>;
		const Bits bits = Bits(r_reader.fixed(sizeof(T)));
		memcpy(&r_value, &bits, sizeof(T));
		return r_reader.valid;
	}
};

inline void message(Writer &r_writer, const Writer &p_message) {
	r_writer.valid &= p_message.valid;
	r_writer.varint(p_message.data.size());
	r_writer.raw(p_message.data.ptr(), p_message.data.size());
}

template <class T>
void field(Writer &r_writer, uint32_t p_id, const T &p_value, Context &p_context) {
	r_writer.tag(p_id, Value<T>::WIRE);
	Value<T>::write(r_writer, p_value, p_context);
}

template <class T>
bool field(Reader &r_reader, int p_wire, T &r_value, Context &p_context) {
	return p_wire == Value<T>::WIRE && Value<T>::read(r_reader, r_value, p_context);
}

template <>
struct Value<String> {
	static constexpr int WIRE = 2;
	static void write(Writer &r_writer, const String &p_value, Context &) {
		const CharString text = p_value.utf8();
		WGodotSerialization::write_bytes(r_writer, reinterpret_cast<const uint8_t *>(text.get_data()), text.length(), true);
	}
	static bool read(Reader &r_reader, String &r_value, Context &) {
		return WGodotSerialization::read_string(r_reader, r_value, true);
	}
};

template <class T>
struct TextValue {
	static constexpr int WIRE = 2;
	static void write(Writer &r_writer, const T &p_value, Context &p_context) {
		Value<String>::write(r_writer, String(p_value), p_context);
	}
	static bool read(Reader &r_reader, T &r_value, Context &p_context) {
		String text;
		if (!Value<String>::read(r_reader, text, p_context)) {
			return false;
		}
		r_value = T(text);
		return true;
	}
};
template <>
struct Value<StringName> : TextValue<StringName> {};
template <>
struct Value<NodePath> : TextValue<NodePath> {};

// Math values are protobuf bytes containing fixed-width little-endian
// components. The existing payload trait verifies their padding-free layout.
template <class T>
struct Value<T, std::enable_if_t<!std::is_arithmetic_v<T>, std::void_t<decltype(WGodotNativePayload::Value<T>::COMPONENT_SIZE)>>> {
	static constexpr int WIRE = 2;
	static void write(Writer &r_writer, const T &p_value, Context &) {
		T value = p_value;
		WGodotNativePayload::Value<T>::swap_components(&value, sizeof(T));
		r_writer.varint(sizeof(T));
		r_writer.raw(reinterpret_cast<const uint8_t *>(&value), sizeof(T));
	}
	static bool read(Reader &r_reader, T &r_value, Context &) {
		const uint64_t size = r_reader.varint();
		const uint8_t *bytes;
		if (size != sizeof(T) || !r_reader.take(sizeof(T), bytes)) {
			return false;
		}
		memcpy(&r_value, bytes, sizeof(T));
		WGodotNativePayload::Value<T>::swap_components(&r_value, sizeof(T));
		return true;
	}
};

template <>
struct Value<Reference> {
	static constexpr int WIRE = 2;
	static void write(Writer &r_writer, const Reference &p_value, Context &);
	static bool read(Reader &r_reader, Reference &r_value, Context &);
};

template <class T>
struct Value<Ref<T>> {
	static constexpr int WIRE = 2;
	static void write(Writer &r_writer, const Ref<T> &p_value, Context &p_context) {
		Value<Reference>::write(r_writer, p_context.identify(p_value), p_context);
	}
	static bool read(Reader &r_reader, Ref<T> &r_value, Context &p_context) {
		Reference reference;
		if (!Value<Reference>::read(r_reader, reference, p_context)) {
			return false;
		}
		const Ref<Resource> resource = p_context.resolve(reference);
		r_value = resource;
		return (!reference.local && !reference.external) || (resource.is_valid() && r_value.is_valid());
	}
};

template <class T, class = void>
struct FixedSequence : std::false_type {};

template <class T>
struct FixedSequence<T, std::void_t<decltype(WGodotNativePayload::Value<T>::COMPONENT_SIZE)>> : std::bool_constant<!std::is_integral_v<T>> {};

template <class T>
bool repeated_equal(const Ref<T> &p_left, const Ref<T> &p_right, Context &) {
	return p_left == p_right;
}

template <class T>
bool repeated_equal(const T &p_left, const T &p_right, Context &p_context) {
	if constexpr (FixedSequence<T>::value) {
		// Preserve exact floating-point representations, including signed zero.
		return memcmp(&p_left, &p_right, sizeof(T)) == 0;
	} else if constexpr (std::is_arithmetic_v<T> || std::is_enum_v<T> || std::is_same_v<T, String> || std::is_same_v<T, StringName> || std::is_same_v<T, NodePath>) {
		return p_left == p_right;
	} else {
		// Nested containers and references must have identical encoded identities.
		Writer left;
		Writer right;
		Value<T>::write(left, p_left, p_context);
		Value<T>::write(right, p_right, p_context);
		return left.valid && right.valid && left.data == right.data;
	}
}

template <class T>
struct Sequence {
	static constexpr int WIRE = 2;
	template <class Container>
	static void write(Writer &r_writer, const Container &p_value, Context &p_context) {
		Writer contents;
		bool repeated = p_value.size() > 4;
		for (int64_t i = 1; repeated && i < p_value.size(); i++) {
			repeated = repeated_equal(p_value[0], p_value[i], p_context);
		}
		if (repeated) {
			// The sequence contains either ordinary field 1 elements or one field 2
			// element plus the unsigned field 3 count. This is independent of the
			// user's protobuf schema and expands directly into typed storage.
			field<T>(contents, 2, p_value[0], p_context);
			contents.tag(3, 0);
			contents.varint(p_value.size());
			message(r_writer, contents);
			return;
		}
		if constexpr (FixedSequence<T>::value) {
			const int64_t size = p_value.size() * sizeof(T);
			contents.tag(1, 2);
			contents.varint(size);
			const int64_t offset = contents.data.size();
			contents.raw(reinterpret_cast<const uint8_t *>(p_value.ptr()), size);
			if (contents.valid && size) {
				WGodotNativePayload::Value<T>::swap_components(contents.data.ptrw() + offset, size);
			}
		} else if constexpr (Value<T>::WIRE != 2) {
			Writer packed;
			for (int64_t i = 0; i < p_value.size(); i++) {
				Value<T>::write(packed, p_value[i], p_context);
			}
			contents.tag(1, 2);
			message(contents, packed);
		} else {
			for (int64_t i = 0; i < p_value.size(); i++) {
				field<T>(contents, 1, p_value[i], p_context);
			}
		}
		message(r_writer, contents);
	}
	template <class Container>
	static bool read(Reader &r_reader, Container &r_value, Context &p_context) {
		Reader contents = r_reader.slice(r_reader.varint());
		Container values;
		while (contents.valid && contents.remaining()) {
			const uint64_t tag = contents.varint();
			if ((tag >> 3) == 2) {
				T item{};
				if (!values.is_empty() || !field(contents, int(tag & 7), item, p_context) || contents.varint() != 24) {
					return false;
				}
				const uint64_t count = contents.varint();
				if (!contents.valid || contents.remaining() || count > INT32_MAX || values.resize(count) != OK) {
					return false;
				}
				values.fill(item);
				break;
			}
			if constexpr (FixedSequence<T>::value) {
				if (tag != 10) {
					return false;
				}
				Reader packed = contents.slice(contents.varint());
				const int64_t size = packed.remaining();
				if (!packed.valid || size % sizeof(T)) {
					return false;
				}
				const int64_t offset = values.size();
				if (values.resize(offset + size / sizeof(T)) != OK) {
					return false;
				}
				if (size) {
					T *destination = values.ptrw() + offset;
					memcpy(destination, packed.data, size);
					WGodotNativePayload::Value<T>::swap_components(destination, size);
				}
			} else if constexpr (Value<T>::WIRE != 2) {
				if (tag != 10) {
					return false;
				}
				Reader packed = contents.slice(contents.varint());
				while (packed.valid && packed.remaining()) {
					T value{};
					if (!Value<T>::read(packed, value, p_context) || values.push_back(value) != OK) {
						return false;
					}
				}
				contents.valid &= packed.valid;
			} else {
				T value{};
				if ((tag >> 3) != 1 || !field(contents, int(tag & 7), value, p_context) || values.push_back(value) != OK) {
					return false;
				}
			}
		}
		r_value = std::move(values);
		return contents.valid;
	}
};

template <class T>
struct Value<Vector<T>> : Sequence<T> {};

struct Field {
	uint32_t id;
	const void *type;
	bool (*read)(Resource *, Reader &, int, Context &);
	void (*write)(const Resource *, Writer &, Context &);
	void (*copy)(const Resource *, void *);
	bool (*share)(Resource *, const Resource *, const Field &);
	Ref<Resource> (*resource)(const Resource *);
};

template <class T>
const void *type_token() {
	// Writable storage keeps distinct template identities out of constant folding.
	static char token = 0;
	return &token;
}

struct Codec {
	uint64_t id;
	uint64_t schema;
	const char *name;
	Ref<Resource> (*create)();
	const Field *fields;
	uint32_t field_count;
	const Field *find(uint32_t p_id) const;
};

void initialize(const Codec *const *p_codecs, int p_count);
Ref<Resource> load(int64_t p_id, ResourceFormatLoader::CacheMode p_cache_mode);
} // namespace WGodotResourceProtobuf
