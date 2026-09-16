// wgodot-changes::file
#include "register_types.h"

#include "elements.h"
#include "smooth_scroll_element.h"
#include "ui_texture_drawing.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_GDSCRIPT_ENABLED
#include "modules/gdscript/wgodot_stdlib.h"
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
		WGodotGDScriptStdLib::register_interface({ "ElementBase", "Control", "::ElementBase", "modules/wgodot_ui/element_base.h", "FlatElement", {} });
		for (const char *name : { "FlatElement", "SurfaceElement", "ButtonElement", "TextBoxElement", "SmoothScrollElement" }) {
			WGodotGDScriptStdLib::register_native_implementation(name, "ElementBase");
		}
#endif
	}
}

void uninitialize_wgodot_ui_module(ModuleInitializationLevel p_level) {
}
