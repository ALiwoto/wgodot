// wgodot-changes::file
#pragma once

#include "wgodot_shader_parameters.h"

// The Inspector, text loader and GDScript VM exchange Variants. Conversion is
// confined to this boundary; typed native access never calls these functions.
namespace WShaderParameterIO {
bool write(WShaderParameters &r_parameters, int p_slot, const Variant &p_value);
Variant read(const WShaderParameters &p_parameters, int p_slot);
}
