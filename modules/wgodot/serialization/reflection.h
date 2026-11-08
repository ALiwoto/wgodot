// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "serializable.h"

#include "modules/gdscript/gdscript.h"

namespace WGodotSerialization {
struct ReflectedField {
	StringName name;
	GDScriptDataType type;
	Kind kind = Kind::INVALID;
	Kind key = Kind::INVALID;
	Kind value = Kind::INVALID;
	int id = -1;
	bool packed = true;
	String oneof;
	String json;
};
void reflected_write(const Object *p_object, Writer &r_writer, bool p_protobuf);
bool reflected_read(Object *p_object, Reader &r_reader, bool p_protobuf);
Dictionary reflected_json(const Object *p_object);
bool reflected_parse_json(Object *p_object, const Dictionary &p_data);
bool reflected_has_field(const Object *p_object, uint32_t p_id);
void reflected_set_field(Object *p_object, uint32_t p_id, bool p_present = true);
void reflected_clear_fields(Object *p_object);
Variant argument(const Dictionary &p_attributes, const StringName &p_name, const Variant &p_default = Variant(), int p_index = 0);
Vector<ReflectedField> reflected_fields(const Object *p_object, bool p_protobuf);
bool reflected_big_endian(const Object *p_object);
bool reflected_strict(const Object *p_object);
void write_value(Writer &r_writer, const Variant &p_value, Kind p_kind, bool p_protobuf);
bool read_value(Reader &r_reader, Variant &r_value, const GDScriptDataType &p_type, Kind p_kind, bool p_protobuf);
Variant default_value(const GDScriptDataType &p_type);
Variant new_model(const GDScriptDataType &p_type);
Variant json_value(const Variant &p_value, const GDScriptDataType &p_type);
bool parse_json_value(const Variant &p_value, const GDScriptDataType &p_type, Variant &r_value);
} // namespace WGodotSerialization
#endif
