// wgodot-changes::file
#include "arrays.h"
#include "reflection.h"

#ifdef TOOLS_ENABLED
using namespace WGodotSerialization;

void WGodotSerialization::reflected_write(const Object *p_object, Writer &r_writer, bool pb) {
	r_writer.big_endian = !pb && reflected_big_endian(p_object);
	HashSet<String> oneofs;
	for (const ReflectedField &field : reflected_fields(p_object, pb)) {
		if (field.id < 0) {
			continue;
		}
		const Variant value = p_object->get(field.name);
		if (field.repeated) {
			Variant collection = value;
			r_writer.valid &= visit_array(collection, [&](const auto &array) {
				if (!pb) {
					r_writer.fixed(array.size(), 4);
				}
				if (pb && field.packed && wire_type(field.kind) != 2) {
					if (array.is_empty()) {
						return true;
					}
					Writer packed;
					for (int64_t i = 0; i < array.size(); ++i) {
						write_value(packed, array[i], field.kind, true);
					}
					r_writer.valid &= packed.valid;
					r_writer.tag(field.id, 2);
					write_bytes(r_writer, packed.data.ptr(), packed.data.size(), true);
				} else {
					for (int64_t i = 0; i < array.size(); ++i) {
						if (pb) {
							r_writer.tag(field.id, wire_type(field.kind));
						}
						write_value(r_writer, array[i], field.kind, pb);
					}
				}
				return r_writer.valid;
			});
		} else if (field.type.builtin_type == Variant::DICTIONARY) {
			if (!pb) {
				r_writer.valid = false;
				return;
			}
			const Dictionary dictionary = value;
			const Array keys = dictionary.keys();
			for (int64_t i = 0; i < keys.size(); ++i) {
				Writer entry;
				entry.depth = r_writer.depth + 1;
				entry.tag(1, wire_type(field.key));
				write_value(entry, keys[i], field.key, true);
				entry.tag(2, wire_type(field.value));
				write_value(entry, dictionary[keys[i]], field.value, true);
				r_writer.valid &= entry.valid;
				r_writer.tag(field.id, 2);
				write_bytes(r_writer, entry.data.ptr(), entry.data.size(), true);
			}
		} else {
			if (pb) {
				const bool present = !value.is_zero() || bool(const_cast<Object *>(p_object)->call(SNAME("has_field"), field.id));
				if (!present) {
					continue;
				}
				if (!field.oneof.is_empty() && oneofs.has(field.oneof)) {
					r_writer.valid = false;
					return;
				}
				oneofs.insert(field.oneof);
				r_writer.tag(field.id, wire_type(field.kind));
			}
			write_value(r_writer, value, field.kind, pb);
		}
	}
}

bool WGodotSerialization::reflected_read(Object *p_object, Reader &r_reader, bool pb) {
	r_reader.big_endian = !pb && reflected_big_endian(p_object);
	const Vector<ReflectedField> fields = reflected_fields(p_object, pb);
	int binary_index = 0;
	while (pb ? r_reader.remaining() > 0 : binary_index < fields.size()) {
		const ReflectedField *field = nullptr;
		int wire = -1;
		if (pb) {
			const uint64_t tag = r_reader.varint();
			const uint64_t id = tag >> 3;
			wire = tag & 7;
			if (!r_reader.valid || !id || id > 536870911) {
				return false;
			}
			for (const auto &candidate : fields) {
				if (candidate.id == int(id)) {
					field = &candidate;
					break;
				}
			}
			if (!field) {
				if (!r_reader.skip(wire, id)) {
					return false;
				}
				continue;
			}
		} else {
			field = &fields[binary_index++];
			if (field->id < 0) {
				continue;
			}
			if (!r_reader.remaining() && !reflected_strict(p_object)) {
				return true;
			}
		}
		Variant value = p_object->get(field->name);
		if (field->repeated) {
			const GDScriptDataType element = sequence_element(field->type);
			if (!visit_array(value, [&](auto &array) {
					if (!pb || (wire == 2 && wire_type(field->kind) != 2)) {
						Reader packed;
						uint64_t count = 0;
						if (pb) {
							packed = r_reader.slice(r_reader.varint());
						} else {
							array.clear();
							count = r_reader.fixed(4);
							if (!r_reader.valid || count > uint64_t(r_reader.remaining())) {
								return false;
							}
						}
						if (pb && !packed.valid) {
							return false;
						}
						while (pb ? packed.remaining() > 0 : count-- > 0) {
							Variant item = default_value(element);
							if (!read_value(pb ? packed : r_reader, item, element, field->kind, pb) || !fits_array(item, field->type.builtin_type)) {
								return false;
							}
							array.push_back(item);
						}
					} else {
						if (wire != wire_type(field->kind)) {
							return false;
						}
						Variant item = default_value(element);
						if (!read_value(r_reader, item, element, field->kind, true) || !fits_array(item, field->type.builtin_type)) {
							return false;
						}
						array.push_back(item);
					}
					return r_reader.valid;
				})) {
				return false;
			}
		} else if (field->type.builtin_type == Variant::DICTIONARY) {
			if (!pb || wire != 2) {
				return false;
			}
			Reader entry = r_reader.slice(r_reader.varint());
			if (!entry.valid) {
				return false;
			}
			const auto key_type = field->type.get_container_element_type(0);
			const auto value_type = field->type.get_container_element_type(1);
			Variant key = default_value(key_type);
			Variant item = default_value(value_type);
			while (entry.remaining()) {
				const uint64_t tag = entry.varint();
				if (!entry.valid || !(tag >> 3) || (tag >> 3) > 536870911) {
					return false;
				}
				if ((tag >> 3) == 1 || (tag >> 3) == 2) {
					const bool is_key = (tag >> 3) == 1;
					const Kind kind = is_key ? field->key : field->value;
					if (int(tag & 7) != wire_type(kind) || !read_value(entry, is_key ? key : item, is_key ? key_type : value_type, kind, true)) {
						return false;
					}
				} else if (!entry.skip(tag & 7, tag >> 3)) {
					return false;
				}
			}
			Dictionary dictionary = value;
			dictionary[key] = item;
			value = dictionary;
		} else {
			if (pb && wire != wire_type(field->kind)) {
				return false;
			}
			if (!read_value(r_reader, value, field->type, field->kind, pb)) {
				return false;
			}
			if (pb && !field->oneof.is_empty()) {
				for (const auto &other : fields) {
					if (other.id != field->id && other.oneof == field->oneof) {
						p_object->set(other.name, default_value(other.type));
						p_object->call(SNAME("set_field_present"), other.id, false);
					}
				}
			}
			if (pb) {
				p_object->call(SNAME("set_field_present"), field->id, true);
			}
		}
		p_object->set(field->name, value);
		if (!r_reader.valid) {
			return false;
		}
	}
	return r_reader.valid;
}
#endif
