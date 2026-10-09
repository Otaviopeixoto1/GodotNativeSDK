@tool
extends EditorPlugin

# Both settings are written into project.godot by gdn init.
const SDK_PATH_SETTING := "native_builder/sdk_path"
const PYTHON_SETTING := "native_builder/python"

const NATIVE_DIR := "res://native"
const SOURCE_DIR := "res://native/src"
const CLASS_LIST_PATH := "res://native/native_classes.txt"

# gdn build exits with this when the SDK needs a variant built and nobody could confirm it.
const EXIT_CONFIRMATION_REQUIRED := 42

const ECSInspector := preload("ecs/ecs_inspector.gd")

var build_button: Button
var auto_toggle: CheckButton
var output_dialog: AcceptDialog
var output_label: RichTextLabel
var confirm_dialog: ConfirmationDialog
var watch_timer: Timer

var build_thread: Thread
var is_building := false
var source_mtimes := {}
var on_confirm := Callable()
var ecs_inspector


func _enter_tree() -> void:
	_register_settings()

	build_button = Button.new()
	build_button.text = "Build Native"
	build_button.tooltip_text = "Build res://native with gdn"
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

	confirm_dialog = ConfirmationDialog.new()
	confirm_dialog.size = Vector2i(700, 400)
	confirm_dialog.confirmed.connect(func(): on_confirm.call())
	EditorInterface.get_base_control().add_child(confirm_dialog)

	watch_timer = Timer.new()
	watch_timer.wait_time = 1.0
	watch_timer.timeout.connect(_check_for_source_changes)
	add_child(watch_timer)
	watch_timer.start()

	source_mtimes = _collect_source_mtimes(SOURCE_DIR)
	ecs_inspector = ECSInspector.new()
	ecs_inspector.install(self)


func _exit_tree() -> void:
	if ecs_inspector != null:
		ecs_inspector.uninstall(self)
		ecs_inspector = null
	if build_thread != null and build_thread.is_started():
		build_thread.wait_to_finish()
	if build_button:
		remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, build_button)
		build_button.queue_free()
	if auto_toggle:
		remove_control_from_container(EditorPlugin.CONTAINER_TOOLBAR, auto_toggle)
		auto_toggle.queue_free()
	if output_dialog:
		output_dialog.queue_free()
	if confirm_dialog:
		confirm_dialog.queue_free()
	if watch_timer:
		watch_timer.queue_free()


func _register_settings() -> void:
	for setting in [SDK_PATH_SETTING, PYTHON_SETTING]:
		if not ProjectSettings.has_setting(setting):
			ProjectSettings.set_setting(setting, "")
		ProjectSettings.set_initial_value(setting, "")
	ProjectSettings.add_property_info({"name": SDK_PATH_SETTING, "type": TYPE_STRING, "hint": PROPERTY_HINT_GLOBAL_DIR})
	ProjectSettings.add_property_info({"name": PYTHON_SETTING, "type": TYPE_STRING, "hint": PROPERTY_HINT_GLOBAL_FILE})


# Polls modification times instead of relying on a platform specific
# filesystem watch API, which keeps this portable.
func _collect_source_mtimes(path: String) -> Dictionary:
	var result := {}
	var dir := DirAccess.open(path)
	if dir == null:
		return result
	for file_name in dir.get_files():
		if file_name.ends_with(".cpp") or file_name.ends_with(".h") or file_name.ends_with(".hpp"):
			var full_path := path.path_join(file_name)
			result[full_path] = FileAccess.get_modified_time(full_path)
	for sub_dir in dir.get_directories():
		result.merge(_collect_source_mtimes(path.path_join(sub_dir)))
	return result


func _check_for_source_changes() -> void:
	if is_building or not auto_toggle.button_pressed:
		return
	var current := _collect_source_mtimes(SOURCE_DIR)
	if current != source_mtimes:
		source_mtimes = current
		_start_build(true)


# Returns an empty string when the settings are usable, otherwise what to fix.
func _check_settings() -> String:
	var sdk_path: String = ProjectSettings.get_setting(SDK_PATH_SETTING, "")
	var python: String = ProjectSettings.get_setting(PYTHON_SETTING, "")
	if sdk_path.is_empty() or python.is_empty():
		return "The SDK is not configured. Run 'gdn init' in the project folder, it sets %s and %s." % [SDK_PATH_SETTING, PYTHON_SETTING]
	if not DirAccess.dir_exists_absolute(sdk_path):
		return "SDK folder not found: %s\nCheck %s in Project Settings, or run 'gdn init' again." % [sdk_path, SDK_PATH_SETTING]
	if not FileAccess.file_exists(python):
		return "Python not found: %s\nCheck %s in Project Settings, or run 'gdn init' again." % [python, PYTHON_SETTING]
	return ""


func _start_build(is_auto_triggered: bool, assume_yes := false, skip_instance_check := false) -> void:
	if is_building:
		push_warning("A build is already running, ignoring the request.")
		return

	var settings_error := _check_settings()
	if not settings_error.is_empty():
		_show_output(settings_error)
		return

	# Auto builds proceed, the developer opted into the risk by enabling Auto.
	if not is_auto_triggered and not skip_instance_check:
		var live_instances := _find_live_instances()
		if not live_instances.is_empty():
			_ask("Found live instances of native classes in the open scene: %s\n\n" % ", ".join(live_instances) +
				"Reloading destroys and recreates them, unsaved node state will be lost.\n\nBuild anyway?",
				func(): _start_build(false, assume_yes, true))
			return

	is_building = true
	build_button.disabled = true
	build_button.text = "Building..."

	var args := [
		"-m", "gdn", "build",
		"--sdk-root", ProjectSettings.get_setting(SDK_PATH_SETTING),
		"--project", ProjectSettings.globalize_path("res://").trim_suffix("/"),
	]
	if assume_yes:
		args.append("--yes")

	# The build can take minutes when the SDK needs a new variant, so keep it off the editor thread.
	build_thread = Thread.new()
	build_thread.start(_build_worker.bind(ProjectSettings.get_setting(PYTHON_SETTING), args))


func _build_worker(python: String, args: Array) -> void:
	var output := []
	var exit_code := OS.execute(python, args, output, true)
	var log_text := ""
	for chunk in output:
		log_text += str(chunk)
	_build_finished.call_deferred(exit_code, log_text)


func _build_finished(exit_code: int, log_text: String) -> void:
	build_thread.wait_to_finish()
	is_building = false
	build_button.disabled = false
	build_button.text = "Build Native"

	if exit_code == 0:
		_show_output(log_text + "\n" + _reload_extension())
	elif exit_code == EXIT_CONFIRMATION_REQUIRED:
		_ask(log_text + "\n\nBuild the SDK variant now? This can take several minutes.",
			func(): _start_build(false, true, true))
	elif exit_code == -1:
		_show_output("Could not start Python: %s" % ProjectSettings.get_setting(PYTHON_SETTING))
	else:
		_show_output(log_text + "\nBuild failed (exit code %d). The previously loaded extension is unchanged." % exit_code)


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


# The library name is set once in native/SConstruct and everything else, including the
# .gdextension location, follows from it.
func _library_name() -> String:
	var file := FileAccess.open(NATIVE_DIR.path_join("SConstruct"), FileAccess.READ)
	if file == null:
		return ""
	var regex := RegEx.create_from_string("(?m)^libname\\s*=\\s*\"([^\"]+)\"")
	var found := regex.search(file.get_as_text())
	return found.get_string(1) if found != null else ""


# reload_extension is the API Godot's own GDExtension hot reload is built
# around. The build installs the library by renaming a new file over the
# old one, so the loaded copy is never modified while it is in use.
func _reload_extension() -> String:
	var library_name := _library_name()
	if library_name.is_empty():
		return "Build succeeded, but libname was not found in native/SConstruct so the extension was not reloaded."
	var path := "res://bin/%s/%s.gdextension" % [library_name, library_name]
	if not FileAccess.file_exists(path):
		return "Build succeeded, but %s does not exist." % path

	var status: int
	if GDExtensionManager.is_extension_loaded(path):
		status = GDExtensionManager.reload_extension(path)
	else:
		EditorInterface.get_resource_filesystem().scan()
		status = GDExtensionManager.load_extension(path)

	match status:
		GDExtensionManager.LOAD_STATUS_OK:
			return "Build succeeded, extension loaded."
		GDExtensionManager.LOAD_STATUS_NEEDS_RESTART:
			return "Build succeeded, but Godot needs a restart to load this extension."
		_:
			return "Build succeeded, but the extension did not load (status %d). Check the editor output." % status


func _ask(text: String, callback: Callable) -> void:
	on_confirm = callback
	confirm_dialog.dialog_text = text
	confirm_dialog.popup_centered()


func _show_output(text: String) -> void:
	output_label.text = text
	output_dialog.popup_centered()
