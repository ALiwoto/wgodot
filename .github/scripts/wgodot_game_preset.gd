# wgodot-changes::file
extends SceneTree


@override
func _initialize() -> void:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	if args.size() != 4:
		push_error("Expected preset name, native module directory, debug template, and release template.")
		quit(1)
		return
	var presets: ConfigFile = ConfigFile.new()
	var error: Error = presets.load("res://export_presets.cfg")
	if error != OK:
		push_error("Cannot load export presets: %s" % error_string(error))
		quit(1)
		return
	for section: String in presets.get_sections():
		if section.ends_with(".options") or presets.get_value(section, "name", "") != args[0]:
			continue
		var features: String = presets.get_value(section, "custom_features", "")
		if not features.split(",").has("wgodot_native"):
			push_error("Preset must enable wgodot_native: %s" % args[0])
			quit(1)
			return
		var options: String = section + ".options"
		presets.set_value(options, "wgodot/native_module", args[1])
		presets.set_value(options, "custom_template/debug", args[2])
		presets.set_value(options, "custom_template/release", args[3])
		error = presets.save("res://export_presets.cfg")
		if error != OK:
			push_error("Cannot save export presets: %s" % error_string(error))
		quit(0 if error == OK else 1)
		return
	push_error("Native export preset not found: %s" % args[0])
	quit(1)
