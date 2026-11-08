// wgodot-changes::file
#pragma once

#include "serializable.h"

#include "modules/wgodot/native/wgodot_native_warray.h"
#include "modules/wgodot/native/wgodot_native_wdictionary.h"

namespace WGodotSerialization {
template <Kind K, bool Protobuf, class T>
void write(Writer &r_writer, const T &p_value) {
	if constexpr (K == Kind::STRING) {
		const CharString bytes = p_value.utf8();
		write_bytes(r_writer, reinterpret_cast<const uint8_t *>(bytes.get_data()), bytes.length(), Protobuf);
	} else if constexpr (K == Kind::BYTES) {
		write_bytes(r_writer, p_value.native().ptr(), p_value.size(), Protobuf);
	} else if constexpr (K == Kind::MESSAGE) {
		if constexpr (!Protobuf) {
			r_writer.byte(p_value.is_valid() ? 1 : 0);
		}
		if (p_value.is_null()) {
			if constexpr (Protobuf) {
				r_writer.varint(0);
			}
			return;
		}
		Writer nested;
		nested.depth = r_writer.depth + 1;
		if (nested.depth > 100) {
			r_writer.valid = false;
			return;
		}
		if constexpr (Protobuf) {
			p_value->write_protobuf(nested);
		} else {
			p_value->write_binary(nested);
		}
		r_writer.valid &= nested.valid;
		write_bytes(r_writer, nested.data.ptr(), nested.data.size(), Protobuf);
	} else if constexpr (K == Kind::VECTOR3) {
		for (int i = 0; i < 3; ++i) {
			write_number(r_writer, Kind::FLOAT, 0, p_value[i], false);
		}
	} else if constexpr (K == Kind::FLOAT || K == Kind::DOUBLE) {
		write_number(r_writer, K, 0, double(p_value), Protobuf);
	} else {
		write_number(r_writer, K, int64_t(p_value), 0, Protobuf);
	}
}

template <Kind K, bool Protobuf, class T>
bool read(Reader &r_reader, T &r_value) {
	if constexpr (K == Kind::STRING) {
		return read_string(r_reader, r_value, Protobuf);
	} else if constexpr (K == Kind::BYTES) {
		PackedByteArray bytes;
		if (!read_bytes(r_reader, bytes, Protobuf)) {
			return false;
		}
		r_value = T(bytes);
		return true;
	} else if constexpr (K == Kind::MESSAGE) {
		if constexpr (!Protobuf) {
			const uint64_t present = r_reader.fixed(1);
			if (!r_reader.valid || present > 1) {
				return false;
			}
			if (!present) {
				r_value.unref();
				return true;
			}
		}
		Reader nested = r_reader.slice(Protobuf ? r_reader.varint() : r_reader.fixed(4));
		if (!nested.valid) {
			return false;
		}
		if (r_value.is_null()) {
			using Model = std::remove_pointer_t<decltype(r_value.ptr())>;
			r_value = Model::create();
		}
		const bool decoded = [&]() {
			if constexpr (Protobuf) {
				return r_value->read_protobuf(nested);
			} else {
				return r_value->read_binary(nested);
			}
		}();
		return decoded && nested.valid && !nested.remaining();
	} else if constexpr (K == Kind::VECTOR3) {
		for (int i = 0; i < 3; ++i) {
			int64_t integer = 0;
			double number = 0;
			if (!read_number(r_reader, Kind::FLOAT, integer, number, false)) {
				return false;
			}
			r_value[i] = number;
		}
		return true;
	} else {
		int64_t integer = 0;
		double number = 0;
		if (!read_number(r_reader, K, integer, number, Protobuf)) {
			return false;
		}
		if constexpr (K == Kind::FLOAT || K == Kind::DOUBLE) {
			r_value = T(number);
		} else {
			r_value = T(integer);
		}
		return true;
	}
}

template <Kind K, class T>
bool present(const T &p_value) {
	if constexpr (K == Kind::STRING || K == Kind::BYTES) {
		return !p_value.is_empty();
	} else if constexpr (K == Kind::MESSAGE) {
		return p_value.is_valid();
	} else {
		return p_value != T{};
	}
}

template <Kind K, bool Protobuf, class T>
void write_array(Writer &r_writer, const T &p_array, uint32_t p_id = 0, bool p_packed = true) {
	if constexpr (!Protobuf) {
		r_writer.fixed(p_array.size(), 4);
	} else if (p_packed && wire_type(K) != 2) {
		if (!p_array.is_empty()) {
			Writer packed;
			for (const auto &item : p_array.native()) {
				write<K, true>(packed, item);
			}
			r_writer.valid &= packed.valid;
			r_writer.tag(p_id, 2);
			write_bytes(r_writer, packed.data.ptr(), packed.data.size(), true);
		}
		return;
	}
	for (const auto &item : p_array.native()) {
		if constexpr (Protobuf) {
			r_writer.tag(p_id, wire_type(K));
		}
		write<K, Protobuf>(r_writer, item);
	}
}

template <Kind K, bool Protobuf, class T>
bool read_array(Reader &r_reader, T &r_array, int p_wire = -1) {
	if constexpr (Protobuf) {
		if (p_wire == 2 && wire_type(K) != 2) {
			Reader packed = r_reader.slice(r_reader.varint());
			while (packed.valid && packed.remaining()) {
				typename T::Element item{};
				if (!read<K, true>(packed, item)) {
					return false;
				}
				r_array.push_back(item);
			}
			return packed.valid;
		}
		if (p_wire != wire_type(K)) {
			return false;
		}
		typename T::Element item{};
		if (!read<K, true>(r_reader, item)) {
			return false;
		}
		r_array.push_back(item);
		return true;
	} else {
		const uint64_t count = r_reader.fixed(4);
		if (!r_reader.valid || count > uint64_t(r_reader.remaining())) {
			return false;
		}
		r_array.clear();
		for (uint64_t i = 0; i < count; ++i) {
			typename T::Element item{};
			if (!read<K, false>(r_reader, item)) {
				return false;
			}
			r_array.push_back(item);
		}
		return true;
	}
}

template <Kind K, Kind V, class T>
void write_map(Writer &r_writer, const T &p_map, uint32_t p_id) {
	for (const auto &item : p_map.native()) {
		Writer entry;
		entry.depth = r_writer.depth + 1;
		entry.tag(1, wire_type(K));
		write<K, true>(entry, item.key);
		entry.tag(2, wire_type(V));
		write<V, true>(entry, item.value);
		r_writer.valid &= entry.valid;
		r_writer.tag(p_id, 2);
		write_bytes(r_writer, entry.data.ptr(), entry.data.size(), true);
	}
}

template <Kind K, Kind V, class T>
bool read_map(Reader &r_reader, T &r_map, int p_wire) {
	if (p_wire != 2) {
		return false;
	}
	Reader entry = r_reader.slice(r_reader.varint());
	typename T::Key key{};
	typename T::Value value{};
	while (entry.valid && entry.remaining()) {
		const uint64_t tag = entry.varint();
		if (!entry.valid || !(tag >> 3) || (tag >> 3) > 536870911) {
			return false;
		}
		if ((tag >> 3) == 1) {
			if ((tag & 7) != uint64_t(wire_type(K)) || !read<K, true>(entry, key)) {
				return false;
			}
		} else if ((tag >> 3) == 2) {
			if ((tag & 7) != uint64_t(wire_type(V)) || !read<V, true>(entry, value)) {
				return false;
			}
		} else if (!entry.skip(tag & 7, tag >> 3)) {
			return false;
		}
	}
	if (!entry.valid) {
		return false;
	}
	r_map.set(key, value);
	return true;
}
} // namespace WGodotSerialization
