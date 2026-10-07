// wgodot-changes::file
#include "wgodot_native_resource_policy.h"

#include "wgodot_export_target.h"

#include "core/io/file_access.h"
#include "core/object/script_language.h"
#include "core/templates/hash_set.h"
#include "scene/resources/animation.h"
#include "scene/resources/packed_scene.h"

namespace {
class OwnedResources {
	String owner;
	HashSet<const Resource *> visited;

public:
	bool scripted = false;
	bool target_nodes = false;

	explicit OwnedResources(const Ref<Resource> &p_resource) : owner(p_resource->get_path().get_slice("::", 0)) {
		visit(p_resource);
	}

	void visit(const Variant &p_value) {
		if (p_value.get_type() == Variant::OBJECT) {
			const Ref<Resource> resource = p_value;
			if (resource.is_null()) {
				return;
			}
			if (Object::cast_to<Script>(resource.ptr())) {
				scripted = true;
				return;
			}
			const String path = resource->get_path();
			if ((!path.is_empty() && path.get_slice("::", 0) != owner) || visited.has(resource.ptr())) {
				return; // External resources keep their own serialization boundary.
			}
			visited.insert(resource.ptr());
			const Ref<Script> script = resource->get_script();
			scripted |= script.is_valid();
			const Ref<PackedScene> scene = resource;
			if (scene.is_valid()) {
				const Ref<SceneState> state = scene->get_state();
				for (int i = 0; i < state->get_node_count(); i++) {
					for (int j = 0; j < state->get_node_property_count(i); j++) {
						target_nodes |= state->get_node_property_name(i, j) == WGodotExportTarget::NODE_PROPERTY;
					}
				}
			}
			List<PropertyInfo> properties;
			WGodotNativeResourcePolicy::get_reference_properties(resource, properties);
			for (const PropertyInfo &property : properties) {
				if ((property.usage & PROPERTY_USAGE_STORAGE) && property.name != SNAME("script")) {
					visit(resource->get(property.name));
				}
			}
		} else if (p_value.get_type() == Variant::ARRAY) {
			const Array values = p_value;
			visit(values.get_typed_script());
			for (const Variant &value : values) {
				visit(value);
			}
		} else if (p_value.get_type() == Variant::DICTIONARY) {
			const Dictionary values = p_value;
			visit(values.get_typed_key_script());
			visit(values.get_typed_value_script());
			for (const KeyValue<Variant, Variant> &entry : values) {
				visit(entry.key);
				visit(entry.value);
			}
		}
	}
};
} // namespace

void WGodotNativeResourcePolicy::get_reference_properties(const Ref<Resource> &p_resource, List<PropertyInfo> &r_properties) {
	const Animation *animation = Object::cast_to<Animation>(p_resource.ptr());
	if (animation) {
		animation->get_reference_property_list(&r_properties);
	} else {
		p_resource->get_property_list(&r_properties);
	}
	for (List<PropertyInfo>::Element *element = r_properties.front(); element;) {
		List<PropertyInfo>::Element *next = element->next();
		const PropertyInfo &property = element->get();
		const bool references = property.type == Variant::OBJECT || property.type == Variant::NODE_PATH || property.type == Variant::ARRAY || property.type == Variant::DICTIONARY || (property.type == Variant::NIL && (property.usage & PROPERTY_USAGE_NIL_IS_VARIANT));
		if (!(property.usage & PROPERTY_USAGE_STORAGE) || !references) {
			r_properties.erase(element);
		}
		element = next;
	}
}

bool WGodotNativeResourcePolicy::is_authored(const String &p_path) {
	const String extension = p_path.get_extension().to_lower();
	return (extension == "tscn" || extension == "scn" || extension == "tres" || extension == "res") && !FileAccess::exists(p_path + ".import");
}

bool WGodotNativeResourcePolicy::needs_factory(const Ref<Resource> &p_resource) {
	return Object::cast_to<PackedScene>(p_resource.ptr()) || OwnedResources(p_resource).scripted;
}

Error WGodotNativeResourcePolicy::validate_external(const Ref<Resource> &p_resource, String &r_error) {
	const OwnedResources contents(p_resource);
	if (contents.scripted || contents.target_nodes) {
		r_error = "External asset contains script attachments or target-specific nodes. Put these in an authored .tscn/.tres wrapper so its native bindings and target filtering can be compiled: " + p_resource->get_path();
		return ERR_UNAVAILABLE;
	}
	return OK;
}
