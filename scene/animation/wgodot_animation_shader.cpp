// wgodot-changes::file
#include "wgodot_animation_property.h"

#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/canvas_item.h"
#include "scene/resources/material.h"

namespace {
enum class Source {
	Unknown,
	Instance,
	CanvasMaterial,
	MaterialOverride,
	MaterialOverlay,
	SurfaceMaterial,
};

struct ShaderPath {
	Source source = Source::Unknown;
	int surface = -1;
	StringName parameter;
	bool shader_resource = false;
	int component = -1;
	bool color_component = false;
};

template <typename T> constexpr bool instance_value = std::is_arithmetic_v<T> ||
		std::is_same_v<T, Vector2> || std::is_same_v<T, Vector3> || std::is_same_v<T, Vector4> ||
		std::is_same_v<T, Vector2i> || std::is_same_v<T, Vector3i> || std::is_same_v<T, Vector4i> || std::is_same_v<T, Color>;

ShaderPath parse_path(const NodePath &p_path) {
	ShaderPath result;
	const int count = p_path.get_subname_count();
	if (count < 1) {
		return result;
	}
	const String first = p_path.get_subname(0);
	int parameter_end = 2;
	if (first.begins_with("instance_shader_parameters/") && first.length() > 27) {
		result.source = Source::Instance;
		result.parameter = first.trim_prefix("instance_shader_parameters/");
		parameter_end = 1;
	} else {
		if (count < 2) {
			return result;
		}
		if (first == "material") {
			result.source = Source::CanvasMaterial;
		} else if (first == "material_override") {
			result.source = Source::MaterialOverride;
		} else if (first == "material_overlay") {
			result.source = Source::MaterialOverlay;
		} else if (first.begins_with("surface_material_override/")) {
			const String index = first.trim_prefix("surface_material_override/");
			if (index.is_valid_int() && index.to_int() >= 0 && index.to_int() <= INT32_MAX) {
				result.source = Source::SurfaceMaterial;
				result.surface = index.to_int();
			}
		}
		const String leaf = p_path.get_subname(1);
		if (leaf == "shader") {
			result.shader_resource = true;
		} else if (leaf.begins_with("shader_parameter/") && leaf.length() > 17) {
			result.parameter = leaf.trim_prefix("shader_parameter/");
		} else {
			return {};
		}
	}
	if (count == parameter_end) {
		return result;
	}
	if (count != parameter_end + 1 || result.shader_resource) {
		return {};
	}
	const String component = p_path.get_subname(parameter_end);
	if (component.length() != 1) {
		return {};
	}
	result.component = String("xyzw").find(component);
	if (result.component < 0) {
		result.component = String("rgba").find(component);
		result.color_component = true;
	}
	if (result.component < 0) {
		return {};
	}
	return result;
}

class ShaderBinding : public RefCounted {
	ObjectID owner;
	ShaderPath path;
	Ref<ShaderMaterial> material;
	Ref<Shader> shader;
	uint64_t revision = 0;
	int slot = -1;
	Variant::Type component_type = Variant::NIL;
	Ref<WInstanceShaderParameters> instance;

	Ref<ShaderMaterial> current_material() const {
		Object *object = ObjectDB::get_instance(owner);
		if (!object) {
			return Ref<ShaderMaterial>();
		}
		switch (path.source) {
			case Source::CanvasMaterial:
				return static_cast<CanvasItem *>(object)->get_material();
			case Source::MaterialOverride:
				return static_cast<GeometryInstance3D *>(object)->get_material_override();
			case Source::MaterialOverlay:
				return static_cast<GeometryInstance3D *>(object)->get_material_overlay();
			case Source::SurfaceMaterial: {
				const auto *mesh = static_cast<MeshInstance3D *>(object);
				if (path.surface >= mesh->get_surface_override_material_count()) {
					return Ref<ShaderMaterial>();
				}
				return mesh->get_surface_override_material(path.surface);
			}
			default: return Ref<ShaderMaterial>();
		}
	}

	bool prepare() {
		const Ref<ShaderMaterial> next_material = current_material();
		const Ref<Shader> next_shader = next_material.is_valid() ? next_material->get_shader() : Ref<Shader>();
		const uint64_t next_revision = next_shader.is_valid() ? next_shader->get_parameter_revision() : 0;
		if (next_material != material || next_shader != shader || next_revision != revision) {
			material = next_material;
			shader = next_shader;
			revision = next_revision;
			slot = -1;
			component_type = Variant::NIL;
			if (material.is_valid() && shader.is_valid() && !path.shader_resource) {
				const auto &parameters = material->get_shader_parameters();
				if (parameters.layout.is_valid()) {
					slot = parameters.layout->find(path.parameter);
					if (slot >= 0 && !parameters.layout->entries[slot].array) {
						component_type = parameters.layout->entries[slot].value_type;
					}
				}
			}
		}
		return material.is_valid() && (path.shader_resource || slot >= 0);
	}

	template <bool Write, typename T> double access_component(double p_value, const WInstanceShaderValue &p_instance_value) {
		constexpr bool color = std::is_same_v<T, Color>;
		ERR_FAIL_COND_V_MSG(path.color_component != color, 0.0, "Shader animation component names do not match the uniform type.");
		if constexpr (!color) {
			ERR_FAIL_INDEX_V(path.component, T::AXIS_COUNT, 0.0);
		}
		T value = path.source == Source::Instance ? p_instance_value.unpack<T>() : material->get_shader_parameter_at<T>(slot);
		if constexpr (Write) {
			auto &component = value[path.component];
			component = std::remove_reference_t<decltype(component)>(p_value);
			// Read the current vector for each write, preserving other component tracks.
			write(value);
		}
		return value[path.component];
	}

	template <bool Write> double access_component(double p_value = 0.0) {
		WInstanceShaderValue instance_parameter;
		Variant::Type type;
		if (path.source == Source::Instance) {
			instance_parameter = instance->value(slot);
			type = instance_parameter.source_type;
		} else {
			if (!prepare()) {
				return 0.0;
			}
			type = component_type;
		}
		// Type tags are compiler metadata; all values and component access stay typed.
		switch (type) {
			case Variant::VECTOR2:
				return access_component<Write, Vector2>(p_value, instance_parameter);
			case Variant::VECTOR3:
				return access_component<Write, Vector3>(p_value, instance_parameter);
			case Variant::VECTOR4:
				return access_component<Write, Vector4>(p_value, instance_parameter);
			case Variant::VECTOR2I:
				return access_component<Write, Vector2i>(p_value, instance_parameter);
			case Variant::VECTOR3I:
				return access_component<Write, Vector3i>(p_value, instance_parameter);
			case Variant::VECTOR4I:
				return access_component<Write, Vector4i>(p_value, instance_parameter);
			case Variant::COLOR:
				return access_component<Write, Color>(p_value, instance_parameter);
			default:
				ERR_FAIL_V_MSG(0.0, "Shader animation component access requires a vector or Color uniform.");
		}
	}

public:
	ShaderBinding(Node *p_owner, const ShaderPath &p_path) : owner(p_owner->get_instance_id()), path(p_path) {
		if (path.source == Source::Instance) {
			auto *geometry = static_cast<GeometryInstance3D *>(p_owner);
			instance = geometry->get_instance_shader_parameters();
			slot = instance->find_or_add(path.parameter);
		} else {
			prepare();
		}
	}

	ObjectID target_id() {
		return path.source == Source::Instance ? owner : prepare() ? material->get_instance_id() : ObjectID();
	}

	template <typename T> T read() {
		if constexpr (std::is_arithmetic_v<T>) {
			if (path.component >= 0) {
				return T(access_component<false>());
			}
		}
		if constexpr (std::is_same_v<T, Ref<Resource>>) {
			if (!prepare()) {
				return T();
			}
			return path.shader_resource ? Ref<Resource>(material->get_shader()) : Ref<Resource>(material->get_shader_texture_at(slot));
		} else {
			if constexpr (instance_value<T>) {
				if (path.source == Source::Instance) {
					const auto value = instance->value(slot);
					return value.valid() ? value.unpack<T>() : T();
				}
			}
			return prepare() ? material->get_shader_parameter_at<T>(slot) : T();
		}
	}

	template <typename T> void write(const T &p_value) {
		if constexpr (std::is_arithmetic_v<T>) {
			if (path.component >= 0) {
				access_component<true>(double(p_value));
				return;
			}
		}
		if constexpr (std::is_same_v<T, Ref<Resource>>) {
			if (!prepare()) {
				return;
			}
			if (path.shader_resource) {
				const Ref<Shader> value = p_value;
				ERR_FAIL_COND_MSG(p_value.is_valid() && value.is_null(), "A shader animation track requires a Shader resource.");
				if (material->get_shader() != value) {
					material->set_shader(value);
				}
			} else {
				const Ref<Texture> value = p_value;
				ERR_FAIL_COND_MSG(p_value.is_valid() && value.is_null(), "A sampler animation track requires a Texture resource.");
				material->set_shader_parameter_at(slot, value);
			}
		} else {
			if constexpr (instance_value<T>) {
				if (path.source == Source::Instance) {
					auto *geometry = Object::cast_to<GeometryInstance3D>(ObjectDB::get_instance(owner));
					if (geometry) {
						geometry->set_instance_shader_parameter_at(slot, p_value);
					}
					return;
				}
			}
			if (prepare()) {
				material->set_shader_parameter_at(slot, p_value);
			}
		}
	}
};

template <typename T> const WGodotAnimationProperty *descriptor() {
	static constexpr WGodotAnimationPropertyTyped<T> property{
		[](Object *p_context) -> T { return static_cast<ShaderBinding *>(p_context)->read<T>(); },
		[](Object *p_context, T p_value) { static_cast<ShaderBinding *>(p_context)->write(p_value); },
		[](Object *p_context) { return static_cast<ShaderBinding *>(p_context)->target_id(); },
	};
	return &property;
}

const WGodotAnimationProperty *descriptor_for(const ShaderPath &p_path, WAnimationValueKind p_kind) {
	if (p_path.component >= 0 && p_kind != WAnimationValueKind::Int && p_kind != WAnimationValueKind::Float && p_kind != WAnimationValueKind::Number) {
		return nullptr;
	}
	if (p_path.shader_resource && p_kind != WAnimationValueKind::Resource) {
		return nullptr;
	}
	switch (p_kind) {
		case WAnimationValueKind::Bool: return descriptor<bool>();
		case WAnimationValueKind::Int: return descriptor<int64_t>();
		case WAnimationValueKind::Number:
		case WAnimationValueKind::Float: return descriptor<double>();
		case WAnimationValueKind::Vector2: return descriptor<Vector2>();
		case WAnimationValueKind::Vector3: return descriptor<Vector3>();
		case WAnimationValueKind::Vector4: return descriptor<Vector4>();
		case WAnimationValueKind::Vector2i: return descriptor<Vector2i>();
		case WAnimationValueKind::Vector3i: return descriptor<Vector3i>();
		case WAnimationValueKind::Vector4i: return descriptor<Vector4i>();
		case WAnimationValueKind::Color: return descriptor<Color>();
		default: break;
	}
	if (p_path.source == Source::Instance) {
		return nullptr;
	}
	switch (p_kind) {
		case WAnimationValueKind::Resource: return descriptor<Ref<Resource>>();
		case WAnimationValueKind::Basis: return descriptor<Basis>();
		case WAnimationValueKind::Transform2D: return descriptor<Transform2D>();
		case WAnimationValueKind::Transform3D: return descriptor<Transform3D>();
		case WAnimationValueKind::Float32Array: return descriptor<PackedFloat32Array>();
		case WAnimationValueKind::Float64Array: return descriptor<PackedFloat64Array>();
		case WAnimationValueKind::Int32Array: return descriptor<PackedInt32Array>();
		case WAnimationValueKind::Int64Array: return descriptor<PackedInt64Array>();
		case WAnimationValueKind::Vector2Array: return descriptor<PackedVector2Array>();
		case WAnimationValueKind::Vector3Array: return descriptor<PackedVector3Array>();
		case WAnimationValueKind::Vector4Array: return descriptor<PackedVector4Array>();
		case WAnimationValueKind::ColorArray: return descriptor<PackedColorArray>();
		default: return nullptr;
	}
}
} // namespace

bool WGodotAnimationProperty::is_shader_assignment(const NodePath &p_path) {
	return parse_path(p_path).shader_resource;
}

bool WGodotAnimationProperty::supports_engine_path(const NodePath &p_path) {
	return parse_path(p_path).source != Source::Unknown;
}

bool WGodotAnimationProperty::supports_engine_value(const NodePath &p_path, WAnimationValueKind p_kind) {
	const ShaderPath path = parse_path(p_path);
	return path.source != Source::Unknown && descriptor_for(path, p_kind);
}

WGodotAnimationPropertyBinding WGodotAnimationProperty::bind_engine(Node *p_node, const NodePath &p_path, WAnimationValueKind p_kind) {
	const ShaderPath path = parse_path(p_path);
	if (path.source == Source::Unknown ||
			(path.source == Source::CanvasMaterial && !Object::cast_to<CanvasItem>(p_node)) ||
			(path.source == Source::SurfaceMaterial && !Object::cast_to<MeshInstance3D>(p_node)) ||
			((path.source == Source::Instance || path.source == Source::MaterialOverride || path.source == Source::MaterialOverlay) && !Object::cast_to<GeometryInstance3D>(p_node))) {
		return {};
	}
	WGodotAnimationPropertyBinding result;
	result.property = descriptor_for(path, p_kind);
	if (!result.property) {
		return result;
	}
	result.context = memnew(ShaderBinding(p_node, path));
	return result;
}
