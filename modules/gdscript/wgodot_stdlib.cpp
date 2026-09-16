// wgodot-changes::file

#include "wgodot_stdlib.h"

#include "gdscript.h"
#include "gdscript_cache.h"

#include "core/error/error_macros.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/templates/vector.h"

#include "modules/wgodot/native/binary_serializable.h"

namespace {

struct InterfaceSource {
	WGodotGDScriptStdLib::NativeInterface native;
	String path;
	String source;
};

Vector<InterfaceSource> interfaces;

struct NativeImplementation {
	StringName class_name;
	StringName interface_name;
};
Vector<NativeImplementation> native_implementations;

// The core interface keeps slot zero, independent of optional modules.
void ensure_core_interfaces() {
	if (interfaces.is_empty()) {
		WGodotGDScriptStdLib::NativeInterface binary{ SNAME("BinarySerializable"), SNAME("Object"), "::BinarySerializable", "modules/wgodot/native/binary_serializable.h", StringName(), {} };
		binary.methods.push_back(WGodotGDScriptStdLib::interface_method("serialize_binary", &BinarySerializable::serialize_binary));
		interfaces.push_back({ binary, "wgodot-interface://BinarySerializable", WGodotGDScriptStdLib::make_interface_source(binary) });
	}
}

const InterfaceSource &get_interface(int p_index) {
	ensure_core_interfaces();
	return interfaces[p_index];
}

int find_path(const String &p_path) {
	for (int i = 0; i < WGodotGDScriptStdLib::get_builtin_interface_count(); i++) {
		if (get_interface(i).path == p_path) {
			return i;
		}
	}
	return -1;
}

} // namespace

Error WGodotGDScriptStdLib::register_interface(const NativeInterface &p_interface) {
	ERR_FAIL_COND_V_MSG(has_global_interface(p_interface.name), ERR_ALREADY_EXISTS, vformat("Interface '%s' is already registered.", p_interface.name));
	const String path = "wgodot-interface://" + String(p_interface.name);
	interfaces.push_back({ p_interface, path, make_interface_source(p_interface) });
	ScriptServer::add_global_class(p_interface.name, p_interface.native_base, "GDScript", path, true, false);
	return OK;
}

const WGodotGDScriptStdLib::NativeInterface *WGodotGDScriptStdLib::get_native_interface(const StringName &p_name) {
	const int index = get_builtin_interface_index(p_name);
	return index >= 0 ? &get_interface(index).native : nullptr;
}

void WGodotGDScriptStdLib::clear_module_interfaces() {
	native_implementations.clear();
	interfaces.clear();
}

void WGodotGDScriptStdLib::register_native_implementation(const StringName &p_class, const StringName &p_interface) {
	ERR_FAIL_COND_MSG(!ClassDB::class_exists(p_class), vformat("Native class '%s' must be registered before its interfaces.", p_class));
	ERR_FAIL_COND_MSG(!has_global_interface(p_interface), vformat("Interface '%s' is not registered.", p_interface));
	native_implementations.push_back({ p_class, p_interface });
}

void WGodotGDScriptStdLib::initialize_native_interfaces() {
	// Compilation needs GDScript's native globals. Modules declare these bindings
	// during scene registration; validate them once the language is initialized.
	for (const NativeImplementation &implementation : native_implementations) {
		Error error = OK;
		Ref<GDScript> contract = GDScriptCache::get_full_script(get_global_interface_path(implementation.interface_name), error);
		ERR_CONTINUE_MSG(error != OK, vformat("Could not load native interface '%s': %s.", implementation.interface_name, error_names[error]));
		ClassDB::wgodot_register_interface(implementation.class_name, contract.ptr());
	}
}

bool WGodotGDScriptStdLib::has_global_interface(const StringName &p_name) {
	return get_builtin_interface_index(p_name) >= 0;
}

String WGodotGDScriptStdLib::get_global_interface_path(const StringName &p_name) {
	const int index = get_builtin_interface_index(p_name);
	return index >= 0 ? get_interface(index).path : String();
}

void WGodotGDScriptStdLib::get_global_interface_list(LocalVector<StringName> &r_interfaces) {
	for (int i = 0; i < get_builtin_interface_count(); i++) {
		r_interfaces.push_back(get_interface(i).native.name);
	}
}

int WGodotGDScriptStdLib::get_builtin_interface_count() {
	ensure_core_interfaces();
	return interfaces.size();
}

int WGodotGDScriptStdLib::get_builtin_interface_index(const StringName &p_name) {
	for (int i = 0; i < get_builtin_interface_count(); i++) {
		if (get_interface(i).native.name == p_name) {
			return i;
		}
	}
	return -1;
}

String WGodotGDScriptStdLib::get_builtin_interface_path(int p_index) {
	ERR_FAIL_INDEX_V(p_index, get_builtin_interface_count(), String());
	return get_interface(p_index).path;
}

String WGodotGDScriptStdLib::get_builtin_interface_source(int p_index) {
	ERR_FAIL_INDEX_V(p_index, get_builtin_interface_count(), String());
	return get_interface(p_index).source;
}

bool WGodotGDScriptStdLib::has_script_path(const String &p_path) {
	return find_path(p_path) >= 0;
}

String WGodotGDScriptStdLib::get_script_source(const String &p_path) {
	const int index = find_path(p_path);
	return index >= 0 ? get_interface(index).source : String();
}

void WGodotGDScriptStdLib::register_global_classes() {
	for (int i = 0; i < get_builtin_interface_count(); i++) {
		const InterfaceSource source = get_interface(i);
		ScriptServer::add_global_class(source.native.name, source.native.native_base, "GDScript", source.path, true, false);
	}
}
