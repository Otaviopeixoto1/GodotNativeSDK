@tool
extends RefCounted

# Installs the ECS panel into the editor. native_builder.gd calls install and uninstall, so the
# panel needs no plugin of its own. Requires Godot 4.6 or later for EditorDock.

const Model := preload("ecs_inspector_model.gd")
const DebuggerPlugin := preload("ecs_debugger_plugin.gd")
const Dock := preload("ecs_dock.gd")

var model
var debugger
var dock


func install(plugin: EditorPlugin) -> void:
	model = Model.new()

	debugger = DebuggerPlugin.new()
	debugger.model = model
	model.outgoing.connect(debugger.send)
	plugin.add_debugger_plugin(debugger)

	dock = Dock.new()
	dock.setup(model)
	plugin.add_dock(dock)


func uninstall(plugin: EditorPlugin) -> void:
	if dock != null:
		plugin.remove_dock(dock)
		dock.queue_free()
		dock = null
	if debugger != null:
		plugin.remove_debugger_plugin(debugger)
		model.outgoing.disconnect(debugger.send)
		debugger = null
	model = null
