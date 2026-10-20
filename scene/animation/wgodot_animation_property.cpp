// wgodot-changes::file
#include "wgodot_animation_property.h"

#include "core/templates/hash_map.h"

namespace {
// Written only at module initialization/shutdown. Preparation workers read it
// after registration, so playback needs neither a registry lock nor a mutation.
HashMap<NodePath, WGodotAnimationProperty::Resolver> property_paths;
bool native_bindings = false;
}

void WGodotAnimationProperty::register_path(const NodePath &p_property_path, Resolver p_resolver) {
	ERR_FAIL_COND(p_property_path.get_name_count() != 0 || p_property_path.get_subname_count() == 0);
	ERR_FAIL_COND(property_paths.has(p_property_path));
	property_paths.insert(p_property_path, p_resolver);
}

WGodotAnimationProperty::Resolver WGodotAnimationProperty::find(const NodePath &p_track_path) {
	if (property_paths.is_empty()) {
		return nullptr;
	}
	const NodePath property_path(Vector<StringName>(), p_track_path.get_subnames(), false);
	const Resolver *resolver = property_paths.getptr(property_path);
	return resolver ? *resolver : nullptr;
}

void WGodotAnimationProperty::clear() {
	property_paths.clear();
	native_bindings = false;
}

void WGodotAnimationProperty::require_native_bindings() {
	native_bindings = true;
}

bool WGodotAnimationProperty::native_bindings_required() {
	return native_bindings;
}

Variant WGodotAnimationProperty::read_variant(Object *p_target) const {
	switch (kind) {
#define W_READ_PROPERTY(m_name, m_type, m_variant) \
		case WAnimationValueKind::m_name: return static_cast<const WGodotAnimationPropertyTyped<m_type> *>(this)->read(p_target);
		W_ANIMATION_VALUE_TYPES(W_READ_PROPERTY)
#undef W_READ_PROPERTY
		default: ERR_FAIL_V(Variant());
	}
}

void WGodotAnimationProperty::write_variant(Object *p_target, const Variant &p_value) const {
	switch (kind) {
#define W_WRITE_PROPERTY(m_name, m_type, m_variant) \
		case WAnimationValueKind::m_name: static_cast<const WGodotAnimationPropertyTyped<m_type> *>(this)->write(p_target, p_value); return;
		W_ANIMATION_VALUE_TYPES(W_WRITE_PROPERTY)
#undef W_WRITE_PROPERTY
		default: ERR_FAIL();
	}
}

double WGodotAnimationProperty::read_number(Object *p_target) const {
	switch (kind) {
		case WAnimationValueKind::Float: return static_cast<const WGodotAnimationPropertyTyped<double> *>(this)->read(p_target);
		case WAnimationValueKind::Int: return static_cast<const WGodotAnimationPropertyTyped<int64_t> *>(this)->read(p_target);
		case WAnimationValueKind::Bool: return static_cast<const WGodotAnimationPropertyTyped<bool> *>(this)->read(p_target);
		default: ERR_FAIL_V(0);
	}
}

void WGodotAnimationProperty::write_number(Object *p_target, double p_value) const {
	switch (kind) {
		case WAnimationValueKind::Float: static_cast<const WGodotAnimationPropertyTyped<double> *>(this)->write(p_target, p_value); return;
		case WAnimationValueKind::Int: static_cast<const WGodotAnimationPropertyTyped<int64_t> *>(this)->write(p_target, int64_t(p_value)); return;
		case WAnimationValueKind::Bool: static_cast<const WGodotAnimationPropertyTyped<bool> *>(this)->write(p_target, bool(p_value)); return;
		default: ERR_FAIL();
	}
}
