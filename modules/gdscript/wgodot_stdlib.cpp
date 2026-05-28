// wgodot-changes::file
/**************************************************************************/
/*  wgodot_stdlib.cpp                                                     */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/

#include "wgodot_stdlib.h"

#include "core/object/script_language.h"

namespace {

struct WGodotStdLibScript {
	const char *global_name = nullptr;
	const char *base_name = nullptr;
	const char *path = nullptr;
	const char *source = nullptr;
};

static const WGodotStdLibScript builtin_interfaces[] = {
	{
			"BinarySerializable",
			"",
			"wgodot://stdlib/BinarySerializable.gd",
#include "wgodot_stdlib/binary_serializable.gd.inc"
	},
};

const WGodotStdLibScript *find_interface_by_name(const StringName &p_name) {
	for (const WGodotStdLibScript &script : builtin_interfaces) {
		if (StringName(script.global_name) == p_name) {
			return &script;
		}
	}
	return nullptr;
}

const WGodotStdLibScript *find_script_by_path(const String &p_path) {
	for (const WGodotStdLibScript &script : builtin_interfaces) {
		if (p_path == script.path) {
			return &script;
		}
	}
	return nullptr;
}

} // namespace

bool WGodotGDScriptStdLib::has_global_interface(const StringName &p_name) {
	return find_interface_by_name(p_name) != nullptr;
}

String WGodotGDScriptStdLib::get_global_interface_path(const StringName &p_name) {
	const WGodotStdLibScript *script = find_interface_by_name(p_name);
	return script != nullptr ? String(script->path) : String();
}

void WGodotGDScriptStdLib::get_global_interface_list(LocalVector<StringName> &r_interfaces) {
	for (const WGodotStdLibScript &script : builtin_interfaces) {
		r_interfaces.push_back(StringName(script.global_name));
	}
}

bool WGodotGDScriptStdLib::has_script_path(const String &p_path) {
	return find_script_by_path(p_path) != nullptr;
}

String WGodotGDScriptStdLib::get_script_source(const String &p_path) {
	const WGodotStdLibScript *script = find_script_by_path(p_path);
	return script != nullptr ? String::utf8(script->source) : String();
}

void WGodotGDScriptStdLib::register_global_classes() {
	for (const WGodotStdLibScript &script : builtin_interfaces) {
		ScriptServer::add_global_class(StringName(script.global_name), StringName(script.base_name), "GDScript", script.path, true, false);
	}
}
