// wgodot-changes::file
#pragma once

#include "wgodot_native_calls.h"
#include "scene/main/node.h"
#include <algorithm>

namespace WGodotNative {
template <class Packed, class Element, size_t Count>
Packed packed_data(const Element (&p_data)[Count]) {
	Packed result;
	result.resize(Count);
	std::copy_n(p_data, Count, result.ptrw());
	return result;
}

template <class T>
Variant serialized_value(const T &p_value) {
	if constexpr (IsWArray<T>::value) {
		return p_value.duplicate_to_array();
	} else if constexpr (IsWDictionary<T>::value) {
		return p_value.duplicate_to_dictionary();
	} else {
		return Variant(p_value);
	}
}

template <class T>
T deserialize_value(const Variant &p_value) {
	if constexpr (IsWArray<T>::value) {
		T result;
		const Array values = p_value;
		result.resize(values.size());
		for (int i = 0; i < values.size(); i++) {
			result.set(i, deserialize_value<typename T::Element>(values[i]));
		}
		return result;
	} else if constexpr (IsWDictionary<T>::value) {
		T result;
		const Dictionary values = p_value;
		for (const KeyValue<Variant, Variant> &entry : values) {
			result.set(deserialize_value<typename T::Key>(entry.key), deserialize_value<typename T::Value>(entry.value));
		}
		return result;
	} else {
		return convert<T>(p_value);
	}
}

inline Variant duplicate_node_value(const Variant &p_value, const Node *p_root, const Node *p_source, Node *p_copy, bool p_always_duplicate = false) {
	if (p_value.get_type() == Variant::OBJECT) {
		Object *object = p_value;
		if (Node *node = Object::cast_to<Node>(object)) {
			if (node == p_root || p_root->is_ancestor_of(node)) {
				return p_copy->get_node_or_null(p_source->get_path_to(node));
			}
		} else if (p_always_duplicate) {
			if (Resource *resource = Object::cast_to<Resource>(object)) {
				return resource->duplicate();
			}
		}
		return p_value;
	}
	if (p_value.get_type() == Variant::ARRAY) {
		Array values = p_value;
		values = values.duplicate(true);
		if (values.get_typed_builtin() == Variant::OBJECT) {
			for (int i = 0; i < values.size(); i++) {
				Object *object = values[i];
				Node *node = Object::cast_to<Node>(object);
				if (node && (node == p_root || p_root->is_ancestor_of(node))) {
					values[i] = p_copy->get_node_or_null(p_source->get_path_to(node));
				}
			}
		}
		return values;
	}
	return p_value.duplicate(true);
}
} // namespace WGodotNative
