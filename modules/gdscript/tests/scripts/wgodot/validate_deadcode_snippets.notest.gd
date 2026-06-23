# wgodot-changes::file
extends SceneTree

const DEADCODE_ID: String = "123456789"


func _init() -> void:
	var deadcode_dir: String = _get_arg_value("--deadcode-dir")
	if deadcode_dir.is_empty():
		printerr("Missing --deadcode-dir=<path>.")
		quit(1)
		return

	var failed: bool = false
	failed = _validate_pool(deadcode_dir, "deadcode", false) or failed
	failed = _validate_pool(deadcode_dir, "static_deadcode", true) or failed

	if failed:
		quit(1)
		return

	print("WGodot deadcode snippets passed parse/analyze validation.")
	quit(0)


func _get_arg_value(name: String) -> String:
	var args: PackedStringArray = OS.get_cmdline_user_args()
	var prefix: String = name + "="
	for i: int in range(args.size()):
		var arg: String = String(args[i])
		if arg == name and i + 1 < args.size():
			return String(args[i + 1])
		if arg.begins_with(prefix):
			return arg.substr(prefix.length())
	return ""


func _validate_pool(deadcode_dir: String, prefix: String, static_class: bool) -> bool:
	var failed: bool = false
	var files: Array[String] = _get_template_files(deadcode_dir, prefix)
	for file_name: String in files:
		failed = _validate_template(deadcode_dir.path_join(file_name), file_name, static_class) or failed
	return failed


func _get_template_files(deadcode_dir: String, prefix: String) -> Array[String]:
	var result: Array[String] = []
	var dir: DirAccess = DirAccess.open(deadcode_dir)
	if dir == null:
		printerr("Cannot open deadcode directory: " + deadcode_dir)
		return result

	dir.list_dir_begin()
	while true:
		var file_name: String = dir.get_next()
		if file_name.is_empty():
			break
		if dir.current_is_dir():
			continue
		if file_name.begins_with(prefix) and file_name.ends_with(".txt"):
			result.append(file_name)
	result.sort()
	return result


func _validate_template(path: String, file_name: String, static_class: bool) -> bool:
	var raw: String = FileAccess.get_file_as_string(path)
	if FileAccess.get_open_error() != OK:
		printerr("Cannot read deadcode template: " + path)
		return true

	var source: String = _make_source(raw, file_name, static_class)
	var script: GDScript = GDScript.new()
	script.source_code = source
	script.resource_path = "res://wgodot/deadcode_validation/" + file_name.get_basename() + ".gd"

	var error: Error = script.reload()
	if error == OK:
		return false

	printerr("Deadcode template failed parse/analyze: " + file_name)
	printerr("Generated source:")
	printerr(source)
	return true


func _make_source(raw: String, file_name: String, static_class: bool) -> String:
	var snippet: String = raw.strip_edges()
	snippet = snippet.replace("_wgdc_", "_wgodot_dc_WGODOT_DC_ID_")
	snippet = snippet.replace("WGODOT_DC_ID", DEADCODE_ID)

	if static_class:
		return "@static_class\nclass_name WGodotDeadCodeStaticValidation_" + file_name.get_basename() + "\n\n" + snippet + "\n"

	return "extends Node\n\n" + snippet + "\n"
