// wgodot-changes::file
#pragma once

#include "wgodot_native_warray.h"
#include "wgodot_native_wdictionary.h"

#include "modules/wgodot/codecs/resource_protobuf.h"

namespace WGodotResourceProtobuf {
template <class T>
T decode_destination() {
	if constexpr (std::is_constructible_v<T, WGodotNative::DeferredInitialization>) {
		return T(WGodotNative::DeferredInitialization{});
	} else {
		return T{};
	}
}

template <class T>
struct IsResourceRef : std::false_type {};
template <class T>
struct IsResourceRef<Ref<T>> : std::is_base_of<Resource, T> {};
template <class T, Variant::Type Kind>
struct Value<WGodotNative::WArray<T, Kind>> {
	static constexpr int WIRE = 2;
	using Array = WGodotNative::WArray<T, Kind>;
	static void write(Writer &r_writer, const Array &p_value, Context &p_context) {
		Sequence<T>::write(r_writer, p_value.native(), p_context);
	}
	static bool read(Reader &r_reader, Array &r_value, Context &p_context) {
		Vector<T> values;
		if (!Sequence<T>::read(r_reader, values, p_context)) {
			return false;
		}
		r_value.initialize_native(values);
		return true;
	}
};

template <class K, class V>
struct Value<WGodotNative::WDictionary<K, V>> {
	static constexpr int WIRE = 2;
	using Dictionary = WGodotNative::WDictionary<K, V>;
	static void write(Writer &r_writer, const Dictionary &p_value, Context &p_context) {
		Writer contents;
		for (const auto &item : p_value.native()) {
			Writer entry;
			field(entry, 1, item.key, p_context);
			field(entry, 2, item.value, p_context);
			contents.tag(1, 2);
			message(contents, entry);
		}
		message(r_writer, contents);
	}
	static bool read(Reader &r_reader, Dictionary &r_value, Context &p_context) {
		Reader contents = r_reader.slice(r_reader.varint());
		typename Dictionary::Native values;
		while (contents.valid && contents.remaining()) {
			if (contents.varint() != 10) {
				return false;
			}
			Reader entry = contents.slice(contents.varint());
			K key = decode_destination<K>();
			V value = decode_destination<V>();
			const uint64_t key_tag = entry.varint();
			if ((key_tag >> 3) != 1 || !field(entry, key_tag & 7, key, p_context)) {
				return false;
			}
			const uint64_t value_tag = entry.varint();
			if ((value_tag >> 3) != 2 || !field(entry, value_tag & 7, value, p_context) || entry.remaining() || !entry.valid || values.has(key)) {
				return false;
			}
			values.insert(key, value);
		}
		if (!contents.valid) {
			return false;
		}
		r_value.initialize_native(std::move(values));
		return true;
	}
};

template <class Owner, class T, auto Read, auto Write, auto Initialize = nullptr>
struct Binding {
	static bool read(Resource *p_resource, Reader &r_reader, int p_wire, Context &p_context) {
		if constexpr (Initialize != nullptr) {
			return field(r_reader, p_wire, static_cast<Owner *>(p_resource)->*Initialize, p_context);
		}
		T value = decode_destination<T>();
		if (!field(r_reader, p_wire, value, p_context)) {
			return false;
		}
		(static_cast<Owner *>(p_resource)->*Write)(value);
		return true;
	}
	static void write(const Resource *p_resource, Writer &r_writer, Context &p_context) {
		Value<T>::write(r_writer, (static_cast<Owner *>(const_cast<Resource *>(p_resource))->*Read)(), p_context);
	}
	static void copy(const Resource *p_resource, void *r_value) {
		*static_cast<T *>(r_value) = (static_cast<Owner *>(const_cast<Resource *>(p_resource))->*Read)();
	}
	static bool share(Resource *p_resource, const Resource *p_source, const Field &p_field) {
		if (p_field.type != type_token<T>()) {
			if constexpr (IsResourceRef<T>::value) {
				if (p_field.resource) {
					const Ref<Resource> source = p_field.resource(p_source);
					const T value = source;
					if (source.is_valid() && value.is_null()) {
						return false;
					}
					(static_cast<Owner *>(p_resource)->*Write)(value);
					return true;
				}
			}
			return false;
		}
		T value = decode_destination<T>();
		p_field.copy(p_source, &value);
		(static_cast<Owner *>(p_resource)->*Write)(value);
		return true;
	}
	static Ref<Resource> resource(const Resource *p_resource) {
		return (static_cast<Owner *>(const_cast<Resource *>(p_resource))->*Read)();
	}
	static Field field_info(uint32_t p_id) {
		if constexpr (IsResourceRef<T>::value) {
			return { p_id, type_token<T>(), read, write, copy, share, resource };
		} else {
			return { p_id, type_token<T>(), read, write, copy, share, nullptr };
		}
	}
};
} // namespace WGodotResourceProtobuf
