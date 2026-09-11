// wgodot-changes::file
#include "register_types.h"

#include "ui_texture_drawing.h"
#include "elements.h"
#include "smooth_scroll_element.h"
#include "modules/gdscript/wgodot_stdlib.h"

void initialize_wgodot_ui_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(UITextureDrawing);
		GDREGISTER_CLASS(FlatElement);
		GDREGISTER_CLASS(SurfaceElement);
		GDREGISTER_CLASS(ButtonElement);
		GDREGISTER_CLASS(TextBoxElement);
		GDREGISTER_CLASS(SmoothScrollElement);
		const char *element_contract =
#include "element_base.gd.inc"
				;
		WGodotGDScriptStdLib::register_interface("ElementBase", "Control", element_contract);
		for (const char *name : { "FlatElement", "SurfaceElement", "ButtonElement", "TextBoxElement", "SmoothScrollElement" }) {
			WGodotGDScriptStdLib::register_native_implementation(name, "ElementBase");
		}
	}
}

void uninitialize_wgodot_ui_module(ModuleInitializationLevel p_level) {
}
