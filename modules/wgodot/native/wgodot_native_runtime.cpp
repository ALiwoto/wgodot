// wgodot-changes::file
#include "wgodot_native_runtime.h"

#include "core/object/class_db.h"
#include "modules/wgodot/codecs/w_animation.h"
#include "modules/wgodot/serialization/serializable.h"

void WGodotNative::initialize_resource_types() {
	// Native games omit the editor module, including its type registration.
	GDREGISTER_CLASS(WAnimation);
}
