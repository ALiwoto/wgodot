// wgodot-changes::file
#include "cinematic_preview_resources.h"

#include "scene/animation/animation_player.h"
#include "scene/resources/camera_attributes.h"
#include "scene/resources/compositor.h"
#include "scene/resources/environment.h"
#include "scene/resources/material.h"

namespace {

class PreviewResources {
	HashSet<Ref<Resource>> animated;
	HashMap<Ref<Resource>, Ref<Resource>> copies;

	void collect_animation_targets(Node *p_node) {
		if (AnimationPlayer *player = Object::cast_to<AnimationPlayer>(p_node)) {
			Node *root = player->get_node_or_null(player->get_root_node());
			LocalVector<StringName> names;
			player->get_animation_list(&names);
			if (root) {
				for (const StringName &name : names) {
					Ref<Animation> animation = player->get_animation(name);
					for (int track = 0; track < animation->get_track_count(); track++) {
						NodePath path = animation->track_get_path(track);
						Node *target = root->get_node_or_null(NodePath(path.get_concatenated_names()));
						if (!target) {
							continue;
						}
						Variant value = target;
						// The final subname is the property being animated. Its containing
						// resources, including their parent chain, need private copies.
						for (int sub = 0; sub < path.get_subname_count() - 1; sub++) {
							bool valid = false;
							value = value.get_named(path.get_subname(sub), valid);
							if (!valid) {
								break;
							}
							if (value.get_type() == Variant::OBJECT) {
								Ref<Resource> resource = value;
								if (resource.is_valid()) {
									animated.insert(resource);
								}
							}
						}
					}
				}
			}
		}
		for (int i = 0; i < p_node->get_child_count(); i++) {
			collect_animation_targets(p_node->get_child(i));
		}
	}

	Variant remap(const Variant &p_value) {
		if (p_value.get_type() == Variant::OBJECT) {
			Ref<Resource> resource = p_value;
			if (resource.is_null()) {
				return p_value;
			}
			if (const Ref<Resource> *copy = copies.getptr(resource)) {
				return *copy;
			}
			// These rendering resources also contain callback state or receive
			// environment changes outside AnimationPlayer. Textures, meshes and
			// animation libraries remain shared unless a track actually edits them.
			if (!animated.has(resource) && !Object::cast_to<Material>(resource.ptr()) &&
					!Object::cast_to<CameraAttributes>(resource.ptr()) && !Object::cast_to<Environment>(resource.ptr()) &&
					!Object::cast_to<Compositor>(resource.ptr()) && !Object::cast_to<CompositorEffect>(resource.ptr())) {
				return p_value;
			}
			Ref<Resource> copy = resource->duplicate();
			if (copy.is_null()) {
				return p_value;
			}
			copies.insert(resource, copy);
			remap_properties(copy.ptr());
			return copy;
		}
		if (p_value.get_type() == Variant::ARRAY) {
			Array array = p_value;
			Array copy = array.duplicate();
			for (int i = 0; i < copy.size(); i++) {
				copy[i] = remap(array[i]);
			}
			return copy;
		}
		if (p_value.get_type() == Variant::DICTIONARY) {
			Dictionary dictionary = p_value;
			Dictionary copy = dictionary.duplicate();
			for (const Variant &key : dictionary.get_key_list()) {
				copy[key] = remap(dictionary[key]);
			}
			return copy;
		}
		return p_value;
	}

	void remap_properties(Object *p_object) {
		List<PropertyInfo> properties;
		p_object->get_property_list(&properties);
		for (const PropertyInfo &property : properties) {
			if (!(property.usage & PROPERTY_USAGE_STORAGE) || property.name == "script") {
				continue;
			}
			if (property.type != Variant::OBJECT && property.type != Variant::ARRAY && property.type != Variant::DICTIONARY) {
				continue;
			}
			Variant original = p_object->get(property.name);
			Variant value = remap(original);
			if (value != original) {
				p_object->set(property.name, value);
			}
		}
	}

	void remap_tree(Node *p_node) {
		remap_properties(p_node);
		for (int i = 0; i < p_node->get_child_count(); i++) {
			remap_tree(p_node->get_child(i));
		}
	}

public:
	void isolate(Node *p_root) {
		collect_animation_targets(p_root);
		remap_tree(p_root);
	}
};

} // namespace

void cinematic_isolate_preview_resources(Node *p_root) {
	PreviewResources resources;
	resources.isolate(p_root);
}
