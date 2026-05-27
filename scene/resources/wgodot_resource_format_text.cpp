// wgodot-changes::file
/**************************************************************************/
/*  wgodot_resource_format_text.cpp                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "resource_format_text.h"

#include "core/config/project_settings.h"

bool ResourceFormatSaverTextInstance::wgodot_embedded_gdscript_save_guard_enabled() const {
	const char *setting = "debug/gdscript/wgodot/disable_embedded_gdscript";
	if (!ProjectSettings::get_singleton()->has_setting(setting)) {
		return true;
	}

	return GLOBAL_GET_CACHED(bool, setting);
}

bool ResourceFormatSaverTextInstance::wgodot_is_embedded_gdscript_resource(const Ref<Resource> &p_resource) const {
	return p_resource.is_valid() && p_resource->is_built_in() && p_resource->is_class(SNAME("GDScript"));
}

bool ResourceFormatSaverTextInstance::wgodot_variant_contains_embedded_gdscript(const Variant &p_variant, HashSet<Ref<Resource>> &r_seen, bool p_main) const {
	switch (p_variant.get_type()) {
		case Variant::OBJECT: {
			Ref<Resource> resource = p_variant;
			if (resource.is_null() || resource->get_meta(SNAME("_skip_save_"), false)) {
				return false;
			}

			if (!p_main && wgodot_is_embedded_gdscript_resource(resource)) {
				return true;
			}

			if (r_seen.has(resource)) {
				return false;
			}
			r_seen.insert(resource);

			List<PropertyInfo> property_list;
			resource->get_property_list(&property_list);

			for (const PropertyInfo &property : property_list) {
				if (!(property.usage & PROPERTY_USAGE_STORAGE)) {
					continue;
				}

				if (wgodot_variant_contains_embedded_gdscript(resource->get(property.name), r_seen)) {
					return true;
				}
			}
		} break;
		case Variant::ARRAY: {
			Array array = p_variant;
			if (wgodot_variant_contains_embedded_gdscript(array.get_typed_script(), r_seen)) {
				return true;
			}
			for (const Variant &element : array) {
				if (wgodot_variant_contains_embedded_gdscript(element, r_seen)) {
					return true;
				}
			}
		} break;
		case Variant::DICTIONARY: {
			Dictionary dictionary = p_variant;
			if (wgodot_variant_contains_embedded_gdscript(dictionary.get_typed_key_script(), r_seen) ||
					wgodot_variant_contains_embedded_gdscript(dictionary.get_typed_value_script(), r_seen)) {
				return true;
			}
			for (const KeyValue<Variant, Variant> &kv : dictionary) {
				if (wgodot_variant_contains_embedded_gdscript(kv.key, r_seen) ||
						wgodot_variant_contains_embedded_gdscript(kv.value, r_seen)) {
					return true;
				}
			}
		} break;
		default:
			break;
	}

	return false;
}
