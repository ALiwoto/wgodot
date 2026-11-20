// wgodot-changes::file
#include "wgodot_scene_shader_fields.h"

#include "scene/resources/mesh.h"
#include "servers/rendering/rendering_server.h"

bool WGodotSceneShaderFields::is_binding(const String &p_name) {
	return p_name == "material_override" || p_name == "material_overlay" || p_name == "mesh" || p_name.begins_with("surface_material_override/");
}

void WGodotSceneShaderFields::assign(const String &p_name, const Variant &p_value) {
	if (is_binding(p_name)) {
		bindings.insert(p_name, p_value);
	}
}

void WGodotSceneShaderFields::materials(Vector<Ref<Material>> &r_materials) const {
	const auto material = [&](const StringName &p_name) -> Ref<Material> {
		const Variant *value = bindings.getptr(p_name);
		return value ? Ref<Material>(*value) : Ref<Material>();
	};
	const Ref<Material> overlay = material(SNAME("material_overlay"));
	if (overlay.is_valid()) {
		r_materials.push_back(overlay);
	}
	const Ref<Material> override = material(SNAME("material_override"));
	if (override.is_valid()) {
		r_materials.push_back(override);
		return;
	}
	const Variant *mesh_value = bindings.getptr(SNAME("mesh"));
	const Ref<Mesh> mesh = mesh_value ? Ref<Mesh>(*mesh_value) : Ref<Mesh>();
	if (mesh.is_valid()) {
		for (int i = 0; i < mesh->get_surface_count(); i++) {
			Ref<Material> surface = material("surface_material_override/" + itos(i));
			if (surface.is_null()) {
				surface = mesh->surface_get_material(i);
			}
			if (surface.is_valid()) {
				r_materials.push_back(surface);
			}
		}
	} else {
		// Explicit surface overrides also declare types before an inherited or
		// runtime mesh becomes available.
		for (const auto &binding : bindings) {
			if (String(binding.key).begins_with("surface_material_override/")) {
				const Ref<Material> surface = binding.value;
				if (surface.is_valid()) {
					r_materials.push_back(surface);
				}
			}
		}
	}
}

Error WGodotSceneShaderFields::field(const String &p_name, WGodotResourceDataType &r_type, bool &r_found, String &r_error) {
	r_found = false;
	if (!p_name.begins_with("instance_shader_parameters/")) {
		return OK;
	}
	const StringName uniform = p_name.trim_prefix("instance_shader_parameters/");
	Vector<Ref<Material>> active;
	materials(active);
	for (const Ref<Material> &material : active) {
		const RID rid = material->get_rid();
		if (!declarations.has(rid)) {
			List<PropertyInfo> parameters;
			RenderingServer::get_singleton()->material_get_instance_shader_parameter_list(rid, &parameters);
			HashMap<StringName, PropertyInfo> fields;
			for (const PropertyInfo &parameter : parameters) {
				fields.insert(parameter.name, parameter);
			}
			declarations.insert(rid, fields);
		}
		const PropertyInfo *info = declarations[rid].getptr(uniform);
		if (!info) {
			continue;
		}
		const WGodotResourceDataType type = WGodotResourceDataType::from_property(*info);
		if (r_found && r_type.text() != type.text()) {
			r_error = "Conflicting material declarations for " + p_name + ".";
			return ERR_PARSE_ERROR;
		}
		r_found = true;
		r_type = type;
	}
	if (!r_found && !active.is_empty()) {
		r_error = "No bound material declares " + p_name + ".";
		return ERR_PARSE_ERROR;
	}
	return OK;
}
