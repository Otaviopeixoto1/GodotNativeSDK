@tool
extends EditorPlugin

# Path to the gdextension file, relative to the project root.
const EXTENSION_PATH := "res://my_native.gdextension"
const NATIVE_DIR := "res://native"
const CLASS_LIST_PATH := "res://native/native_classes.txt"
const COUNTER_PATH := "res://native/.build_counter"
const KEEP_OLD_BUILDS := 2

var build_button: Button
var auto_toggle: CheckButton
var output_dialog: AcceptDialog
var output_label: RichTextLabel
var watch_timer: Timer

var is_building := false
var source_mtimes := {}


func _enter_tree() -> void:
	_ensure_sdk_path_setting()

	build_button = Button.new()
	build_button.text = "Build Native"
	build_button.tooltip_text = "Compile res://native with SCons"
	build_button.pressed.connect(func(): _start_build(false))
	add_control_to_container(EditorPlugin.CONTAINER_TOOLBAR, build_button)

	auto_toggle = CheckButton.new()
	auto_toggle.text = "Auto"
	auto_toggle.tooltip_text = "Rebuild automatically when native source files change"
	add_control_to_container(EditorPlugin.CONTAINER_TOOLBAR, auto_toggle)

	output_dialog = AcceptDialog.new()
	output_dialog.title = "Native Build Output"
	output_dialog.size = Vector2i(700, 500)
	output_label = RichTextLabel.new()
	output_label.custom_minimum_size = Vector2(680, 460)
	output_label.scroll_following = true
	output_dialog.add_child(output_label)
	EditorInterface.get_base_control().add_child(output_dialog)

	watch_timer = Timer.new()
	watch_timer.wait_time = 1.0
	watch_timer.timeout.connect(_check_for_source_changes)
	add_child(watch_timer)
	watch_timer.start()

	source_mtimes = _collect_source_mtimes()


func _exit_tree() -> void:
	if build_button:
		remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, build_button)
		build_button.queue_free()
	if auto_toggle:
		remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, auto_toggle)
		auto_toggle.queue_free()
	if output_dialog:
		output_dialog.queue_free()
	if watch_timer:
		watch_timer.queue_free()


func _ensure_sdk_path_setting() -> void:
	const SDK_PATH_SETTING := "native_builder/sdk_path"
	if not ProjectSettings.has_setting(SDK_PATH_SETTING):
		ProjectSettings.set_setting(SDK_PATH_SETTING, "")
		ProjectSettings.set_initial_value(SDK_PATH_SETTING, "")
		ProjectSettings.save()


# Watcher, ported from the idea behind Jenova's Sakura script change
# trigger mode: poll file modification times instead of relying on a
# platform specific filesystem watch API, which keeps this portable.
func _collect_source_mtimes() -> Dictionary:
	var result := {}
	var dir := DirAccess.open(NATIVE_DIR)
	if dir == null:
		return result
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		if file_name.ends_with(".cpp") or file_name.ends_with(".hpp"):
			var full_path := NATIVE_DIR.path_join(file_name)
			result[full_path] = FileAccess.get_modified_time(full_path)
		file_name = dir.get_next()
	dir.list_dir_end()
	return result


func _check_for_source_changes() -> void:
	if is_building or not auto_toggle.button_pressed:
		return
	var current := _collect_source_mtimes()
	if current != source_mtimes:
		source_mtimes = current
		_start_build(true)


func _start_build(is_auto_triggered: bool) -> void:
	if is_building:
		push_warning("A build is already running, ignoring the request.")
		return

	var sdk_path: String = ProjectSettings.get_setting("native_builder/sdk_path", "")
	if sdk_path.is_empty():
		_show_output("SDK path is not set. Configure native_builder/sdk_path in Project Settings.")
		return

	var live_instances := _find_live_instances()
	if not live_instances.is_empty() and not is_auto_triggered:
		# Manual builds ask for confirmation, auto builds proceed since
		# the developer already opted into the risk by enabling Auto.
		_show_output(
			"Found live instances of native classes in the open scene: %s\n" % ", ".join(live_instances) +
			"Reloading now will destroy and recreate them, unsaved node state will be lost.\n" +
			"Click Build Native again to proceed anyway.")
		return

	is_building = true
	build_button.disabled = true
	build_button.text = "Building..."

	# No unload before the build. The build always writes a new file
	# name, so the previous library is never touched on disk and stays
	# loaded and working for as long as the build takes, including if
	# it fails.
	var counter := _next_build_number()
	var result := _run_scons(sdk_path, counter)
	var log_text: String = result.log

	if result.ok:
		_rewrite_gdextension_library_path(result.lib_path)
		var reload_ok := _reload_extension()
		if reload_ok:
			_cleanup_old_builds(counter)
			log_text += "\nBuild succeeded, extension reloaded.\n"
		else:
			log_text += "\nBuild succeeded but the reload call did not report success.\n"
			log_text += "Check GDExtensionManager.get_loaded_extensions before relying on this build.\n"
	else:
		log_text += "\nBuild failed, previous extension is still loaded and unchanged.\n"

	_show_output(log_text)

	build_button.disabled = false
	build_button.text = "Build Native"
	is_building = false


# Mirrors Jenova's Active vs Passive script distinction, in a simplified
# form. A class is unsafe to swap while a node of that class is alive
# in the currently edited scene, since the class is about to be
# unregistered and re-registered under the same name.
func _find_live_instances() -> Array:
	var watched := _read_class_list()
	var found := {}
	var root := EditorInterface.get_edited_scene_root()
	if root != null:
		_scan_node(root, watched, found)
	return found.keys()


func _read_class_list() -> Array:
	var result := []
	var file := FileAccess.open(CLASS_LIST_PATH, FileAccess.READ)
	if file == null:
		return result
	while not file.eof_reached():
		var line := file.get_line().strip_edges()
		if line != "" and not line.begins_with("#"):
			result.append(line)
	return result


func _scan_node(node: Node, watched: Array, found: Dictionary) -> void:
	if watched.has(node.get_class()):
		found[node.get_class()] = true
	for child in node.get_children():
		_scan_node(child, watched, found)


func _next_build_number() -> int:
	var counter := 0
	var file := FileAccess.open(COUNTER_PATH, FileAccess.READ)
	if file != null:
		counter = file.get_as_text().strip_edges().to_int()
	counter += 1
	var out_file := FileAccess.open(COUNTER_PATH, FileAccess.WRITE)
	if out_file != null:
		out_file.store_string(str(counter))
	return counter


func _get_gdextension_manager() -> Object:
	if not ClassDB.class_exists("GDExtensionManager"):
		return null
	return Engine.get_singleton("GDExtensionManager")


# reload_extension is the API Godot's own GDExtension hot reload feature
# is built around, not a manual unload followed by a separate load.
# The engine-side reload path can call back into the extension to
# recreate its native data for objects that already exist, instead of
# always destroying and recreating them. A manual unload_extension plus
# load_extension pair skips that path entirely.
# Whether existing Player/Enemy instances actually survive depends on
# the godot-cpp version this SDK is built against implementing that
# recreation callback, this scaffold cannot guarantee that from here,
# only that it uses the API that makes it possible.
func _reload_extension() -> bool:
	var manager := _get_gdextension_manager()
	if manager == null:
		return false
	if manager.is_extension_loaded(EXTENSION_PATH):
		var status: int = manager.reload_extension(EXTENSION_PATH)
		return status == 0 # LOAD_STATUS_OK
	else:
		var status: int = manager.load_extension(EXTENSION_PATH)
		return status == 0


func _run_scons(sdk_path: String, build_number: int) -> Dictionary:
	var native_dir := ProjectSettings.globalize_path(NATIVE_DIR)

	var args := [
		"-C", native_dir,
		"sdk_path=%s" % sdk_path,
		"build_number=%d" % build_number,
	]

	var output := []
	var exit_code := OS.execute("scons", args, output, true)

	var log_text := ""
	for line in output:
		log_text += str(line)

	if exit_code != 0:
		return {"ok": false, "log": log_text, "lib_path": ""}

	# The plugin knows the naming convention SConstruct uses, this keeps
	# the two files in agreement without parsing SCons output.
	var lib_path := ""
	var dir := DirAccess.open("res://bin")
	if dir:
		dir.list_dir_begin()
		var file_name := dir.get_next()
		while file_name != "":
			if file_name.find(".b%d." % build_number) != -1:
				lib_path = "res://bin".path_join(file_name)
			file_name = dir.get_next()
		dir.list_dir_end()

	return {"ok": not lib_path.is_empty(), "log": log_text, "lib_path": lib_path}


# Rewrites every platform entry in the gdextension file to point at the
# freshly built library. This is the same idea cr.h uses when it always
# loads a newly named copy instead of the original file, applied to
# Godot's gdextension indirection instead of dlopen directly.
func _rewrite_gdextension_library_path(new_lib_path: String) -> void:
	var config := ConfigFile.new()
	var err := config.load(EXTENSION_PATH)
	if err != OK:
		push_error("Could not read %s" % EXTENSION_PATH)
		return

	var current_platform := _current_platform_key()
	if config.has_section_key("libraries", current_platform):
		config.set_value("libraries", current_platform, new_lib_path)
		config.save(EXTENSION_PATH)


func _current_platform_key() -> String:
	var os_name := OS.get_name().to_lower()
	# The gdextension file uses short target names in its keys, the
	# filenames still contain the full template_debug / template_release
	# name that SCons produces, see SConstruct in native/.
	var target := "debug" if OS.is_debug_build() else "release"
	var arch := "x86_64"

	var platform_map := {
		"linux": "linux",
		"windows": "windows",
		"macos": "macos",
	}
	var mapped: String = platform_map.get(os_name, os_name)
	return "%s.%s.%s" % [mapped, target, arch]


func _cleanup_old_builds(current_build_number: int) -> void:
	var dir := DirAccess.open("res://bin")
	if dir == null:
		return
	dir.list_dir_begin()
	var file_name := dir.get_next()
	while file_name != "":
		var marker := file_name.find(".b")
		if marker != -1:
			var rest := file_name.substr(marker + 2)
			var digits := rest.split(".")[0]
			if digits.is_valid_int():
				var number := digits.to_int()
				if current_build_number - number > KEEP_OLD_BUILDS:
					dir.remove(file_name)
		file_name = dir.get_next()
	dir.list_dir_end()


func _show_output(text: String) -> void:
	output_label.text = text
	output_dialog.popup_centered()
