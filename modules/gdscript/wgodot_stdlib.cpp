// wgodot-changes::file
#include "wgodot_stdlib.h"

#include "gdscript.h"

#include "core/object/wgodot_native_interfaces.h"

void WGodotGDScriptStdLib::register_global_types() {
	LocalVector<StringName> interfaces;
	get_global_interface_list(interfaces);
	for (const StringName &name : interfaces) {
		Ref<GDScriptNativeClass> type(memnew(GDScriptNativeClass(name)));
		GDScriptLanguage::get_singleton()->add_global_constant(name, type);
	}
}

void WGodotGDScriptStdLib::clear_module_interfaces() {
	WGodotNativeInterfaces::clear_descriptors();
	WGodotNativeInterfaces::clear();
}
