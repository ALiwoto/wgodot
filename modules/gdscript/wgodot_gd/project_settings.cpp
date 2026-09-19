// wgodot-changes::file

#include "project_settings.h"

#include "core/config/project_settings.h"

namespace WGodotGDScript {

void register_project_settings() {
	GLOBAL_DEF("wgodot/gdscript/disable_embedded_gdscript", true);
	GLOBAL_DEF("wgodot/gdscript/strict_override_checking", true);
	GLOBAL_DEF("wgodot/gdscript/disable_strict_override_checking_for_addons", true);
	GLOBAL_DEF("wgodot/gdscript/strict_type_checking", true);
	GLOBAL_DEF("wgodot/gdscript/disable_strict_type_checking_for_addons", true);
	GLOBAL_DEF("wgodot/gdscript/strict_signal_callable_checking", true);
	GLOBAL_DEF("wgodot/export/deconst_exports", true);
#ifdef TOOLS_ENABLED
	GLOBAL_DEF("wgodot/export/obfuscate_strings", true);
	GLOBAL_DEF("wgodot/export/redact_diagnostics", false);
	GLOBAL_DEF("wgodot/export/timing_logs_enabled", false);
	GLOBAL_DEF("wgodot/export/timing_verbose_logs_enabled", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "wgodot/export/timing_slow_threshold_msec", PROPERTY_HINT_RANGE, "0,60000,1,or_greater"), 250);
#endif // TOOLS_ENABLED
}

} // namespace WGodotGDScript
