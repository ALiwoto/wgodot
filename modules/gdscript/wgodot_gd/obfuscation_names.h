// wgodot-changes::file
/**************************************************************************/
/*  obfuscation_names.h                                                   */
/**************************************************************************/

#pragma once

#include "export_transform.h"

#include "core/math/random_pcg.h"
#include "core/string/string_name.h"
#include "core/string/ustring.h"
#include "core/templates/hash_set.h"

namespace WGodotGDScriptExportTransform {

String make_short_obfuscated_name(int p_index);
String make_random_short_obfuscated_name(RandomPCG &r_random);
String make_obfuscated_name(ObfuscationStrategy p_strategy, RandomPCG &r_random, HashSet<StringName> &r_reserved_names, const String &p_warning_context);

} // namespace WGodotGDScriptExportTransform
