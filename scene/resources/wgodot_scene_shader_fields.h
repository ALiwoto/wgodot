// wgodot-changes::file
#pragma once

#include "core/io/wgodot_resource_value.h"
#include "scene/resources/material.h"

// Per-node declaration context. Resource loading never instantiates a node to
// discover its shader fields, and renderer access uses its synchronized API.
class WGodotSceneShaderFields : public WGodotResourceValueParser::FieldResolver {
	HashMap<StringName, Variant> bindings;
	HashMap<RID, HashMap<StringName, PropertyInfo>> declarations;
	void materials(Vector<Ref<Material>> &r_materials) const;

public:
	static bool is_binding(const String &p_name);
	explicit WGodotSceneShaderFields(const HashMap<StringName, Variant> &p_bindings) : bindings(p_bindings) {}
	void assign(const String &p_name, const Variant &p_value);
	Error field(const String &p_name, WGodotResourceDataType &r_type, bool &r_found, String &r_error) override;
};
