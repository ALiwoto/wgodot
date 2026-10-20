// wgodot-changes::file
#pragma once

#include "core/object/object.h"
#include "core/string/node_path.h"
#include "scene/resources/wgodot_animation_values.h"

class Node;
struct WGodotAnimationPropertyBinding;

// Native animation access is separate from Object's public property interface.
// Resolve once per mixer cache; descriptors contain code, never instance state.
struct WGodotAnimationProperty {
	WAnimationValueKind kind;
	// Nested Resource tracks still report the animated Resource to the
	// postprocessing hook, while cache lifetime follows the owning Node.
	ObjectID (*target_id)(Object *p_target) = nullptr;

	using Resolver = const WGodotAnimationProperty *(*)(Object *p_target);
	static void register_path(const NodePath &p_property_path, Resolver p_resolver);
	static Resolver find(const NodePath &p_track_path);
	static void clear();
	static void require_native_bindings();
	static bool native_bindings_required();
	static bool supports_engine_path(const NodePath &p_path);
	static bool supports_engine_value(const NodePath &p_path, WAnimationValueKind p_kind);
	static WGodotAnimationPropertyBinding bind_engine(Node *p_node, const NodePath &p_path, WAnimationValueKind p_kind);
	Variant read_variant(Object *p_target) const;
	void write_variant(Object *p_target, const Variant &p_value) const;
	double read_number(Object *p_target) const;
	void write_number(Object *p_target, double p_value) const;
};

// Generated field accessors need no state. Shader bindings keep cached slots
// here, using the same typed descriptor interface and mixer as ordinary fields.
struct WGodotAnimationPropertyBinding {
	const WGodotAnimationProperty *property = nullptr;
	Ref<RefCounted> context;
};

template <typename T> struct WGodotAnimationPropertyTyped : WGodotAnimationProperty {
	T (*read)(Object *p_target);
	void (*write)(Object *p_target, T p_value);
	constexpr WGodotAnimationPropertyTyped(T (*p_read)(Object *), void (*p_write)(Object *, T), ObjectID (*p_target_id)(Object *) = nullptr) :
			WGodotAnimationProperty{ WAnimationValueType<T>::kind, p_target_id }, read(p_read), write(p_write) {}
};
