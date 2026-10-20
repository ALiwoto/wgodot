// wgodot-changes::file
#pragma once

#include "core/io/resource.h"

class Node;

// Per-instantiation resource identity. Generated code supplies typed values and
// existing property values; no property names or serialized value tables enter here.
class WGodotSceneContext {
	HashMap<Node *, HashMap<Ref<Resource>, Ref<Resource>>> &resources;
	Node *base;
	bool inherited;
	bool main_edit;

	Ref<Resource> local_resource(const Ref<Resource> &p_resource, const Ref<Resource> &p_existing);

public:
	WGodotSceneContext(HashMap<Node *, HashMap<Ref<Resource>, Ref<Resource>>> &p_resources, Node *p_base, bool p_inherited, bool p_main_edit) :
			resources(p_resources), base(p_base), inherited(p_inherited), main_edit(p_main_edit) {}

	template <class T>
	Ref<T> resource(const Ref<T> &p_resource, const Ref<Resource> &p_existing = Ref<Resource>()) {
		if (p_resource.is_null() || !p_resource->is_local_to_scene()) {
			return p_resource;
		}
		return local_resource(p_resource, p_existing);
	}

	template <class T, class Read>
	Ref<T> resource(const Ref<T> &p_resource, Read p_read_existing) {
		if (p_resource.is_null() || !p_resource->is_local_to_scene()) {
			return p_resource;
		}
		return local_resource(p_resource, inherited ? Ref<Resource>(p_read_existing()) : Ref<Resource>());
	}
};

// Generated subclasses own concrete resource references. Scalar values are
// emitted directly in the initializers. Shared data never retains live nodes.
class WGodotSceneConstruction : public RefCounted {
public:
	virtual Ref<WGodotSceneConstruction> duplicate(const Resource::NativeCopyContext &p_context) const = 0;
	virtual void apply(int p_index, Node *p_node, WGodotSceneContext &p_context) const = 0;
	virtual void resolve(int p_index, Node *p_node, WGodotSceneContext &p_context) const = 0;
	virtual void connect(int p_index, Node *p_source, Node *p_target, int p_flags) const = 0;
};
