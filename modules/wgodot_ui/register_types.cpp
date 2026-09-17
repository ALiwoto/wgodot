// wgodot-changes::file
#include "register_types.h"

#include "elements.h"
#include "smooth_scroll_element.h"
#include "ui_texture_drawing.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "core/object/wgodot_interface_registry.h"
#include "core/object/wgodot_native_interfaces.h"

void ElementBase::_bind_interface(WGodotNativeInterfaces::Builder &p_builder) {
	p_builder.import_api("FlatElement");
}
#endif

void initialize_wgodot_ui_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(UITextureDrawing);
		GDREGISTER_CLASS(FlatElement);
		GDREGISTER_CLASS(SurfaceElement);
		GDREGISTER_CLASS(ButtonElement);
		GDREGISTER_CLASS(TextBoxElement);
		GDREGISTER_CLASS(SmoothScrollElement);
#ifdef MODULE_GDSCRIPT_ENABLED
		WGDREGISTER_INTERFACE(ElementBase, "Control", "modules/wgodot_ui/element_base.h");
		for (const char *name : { "FlatElement", "SurfaceElement", "ButtonElement", "TextBoxElement", "SmoothScrollElement" }) {
			WGodotNativeInterfaces::add_implementation("ElementBase", name);
		}
#endif
	}
}

void uninitialize_wgodot_ui_module(ModuleInitializationLevel p_level) {
}
