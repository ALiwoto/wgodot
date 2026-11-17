// wgodot-changes::file
#include "native_fields.h"

#include "core/io/wgodot_native_resource_fields.h"
#include "core/object/class_db.h"
#include "scene/resources/theme.h"

namespace {
bool theme_field(const String &p_name, PropertyInfo &r_info) {
	const Vector<String> parts = p_name.split("/", true);
	if (parts.size() < 2 || !Theme::is_valid_type_name(parts[0])) {
		return false;
	}
	StringName setter;
	if (parts.size() == 2 && parts[1] == "base_type") {
		setter = SNAME("set_type_variation");
	} else if (parts.size() == 3 && Theme::is_valid_item_name(parts[2])) {
		const char *kinds[] = { "icons", "styles", "fonts", "font_sizes", "colors", "constants" };
		const char *setters[] = { "set_icon", "set_stylebox", "set_font", "set_font_size", "set_color", "set_constant" };
		for (int i = 0; i < 6; i++) {
			if (parts[1] == kinds[i]) {
				setter = setters[i];
				break;
			}
		}
	}
	MethodInfo method;
	if (setter.is_empty() || !ClassDB::get_method_info(SNAME("Theme"), setter, &method) || method.arguments.is_empty()) {
		return false;
	}
	r_info = method.arguments[method.arguments.size() - 1];
	r_info.name = p_name;
	return true;
}

bool index(const String &p_text) {
	return p_text.is_valid_int() && p_text.to_int() >= 0 && p_text.to_int() <= INT32_MAX;
}

bool bus_field(const String &p_name, PropertyInfo &r_info) {
	const Vector<String> parts = p_name.split("/", true);
	if (parts.size() < 3 || parts[0] != "bus" || !index(parts[1])) {
		return false;
	}
	if (parts.size() == 3) {
		if (parts[2] == "name" || parts[2] == "send") {
			r_info = PropertyInfo(Variant::STRING_NAME, p_name);
		} else if (parts[2] == "solo" || parts[2] == "mute" || parts[2] == "bypass_fx") {
			r_info = PropertyInfo(Variant::BOOL, p_name);
		} else if (parts[2] == "volume_db") {
			r_info = PropertyInfo(Variant::FLOAT, p_name);
		} else {
			return false;
		}
		return true;
	}
	if (parts.size() == 5 && parts[2] == "effect" && index(parts[3])) {
		if (parts[4] == "effect") {
			r_info = PropertyInfo(Variant::OBJECT, p_name, PROPERTY_HINT_RESOURCE_TYPE, "AudioEffect");
			return true;
		}
		if (parts[4] == "enabled") {
			r_info = PropertyInfo(Variant::BOOL, p_name);
			return true;
		}
	}
	return false;
}
}

void register_native_resource_fields() {
	WGodotNativeResourceFields::register_type("Theme", &theme_field);
	WGodotNativeResourceFields::register_type("AudioBusLayout", &bus_field);
}

void unregister_native_resource_fields() {
	WGodotNativeResourceFields::unregister_type("Theme");
	WGodotNativeResourceFields::unregister_type("AudioBusLayout");
}
