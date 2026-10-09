@tool
extends RefCounted

# Data of the ECS panel: what the running game reported and what the panel asks for.
# It knows neither the UI nor the transport. Messages to send go out through the outgoing signal,
# and the UI listens to the other signals. Message layouts are described in src/ecs/debug_server.cpp.

const PROTOCOL_VERSION := 1
const CAPTURE := "gdn"

signal outgoing(message: String, data: Array)
signal schema_changed
signal worlds_changed
signal query_changed
signal rows_changed
signal detail_changed
signal status_changed(text: String)

var connected := false
# True while the panel is visible, the game only samples while someone looks.
var active := false

# Reported once per session by gdn:hello.
var components: Array = []
var component_index := {}
var actions := PackedStringArray()

# Reported by gdn:worlds whenever a world is created or destroyed.
var world_ids := PackedInt64Array()
var world_names := PackedStringArray()

# The query the table shows.
var world_id := -1
var include := PackedStringArray()
var exclude := PackedStringArray()
var offset := 0
var limit := 100
var rate_hz := 10.0

# The latest page. Each entry of column_info describes the column at the same index.
var frame := 0
var total := 0
var entities := PackedInt64Array()
var columns: Array = []
var column_info: Array = []

# The selected entity and all its components, as [component, values] pairs.
var selected_entity := -1
var detail: Array = []


func session_started() -> void:
	reset()
	connected = true
	status_changed.emit("Waiting for the game to create an ECSWorld.")
	# The game sends hello when its first world appears. Asking covers a panel that missed it.
	_send("hello", [])


func session_stopped() -> void:
	connected = false
	# The values belong to the game that just stopped. The query stays for the next run.
	_clear_runtime_data()
	status_changed.emit("The game is not running.")


func reset() -> void:
	components = []
	component_index = {}
	actions = PackedStringArray()
	_rebuild_column_info()
	schema_changed.emit()
	query_changed.emit()
	_clear_runtime_data()


# Drops everything that came from a running game: worlds, the current page and the selection.
func _clear_runtime_data() -> void:
	world_ids = PackedInt64Array()
	world_names = PackedStringArray()
	world_id = -1
	frame = 0
	total = 0
	offset = 0
	entities = PackedInt64Array()
	columns = []
	selected_entity = -1
	detail = []
	worlds_changed.emit()
	rows_changed.emit()
	detail_changed.emit()


# Entry point for every gdn message coming from the game.
func handle(message: String, data: Array) -> void:
	match message:
		"gdn:hello":
			_on_hello(data)
		"gdn:worlds":
			_on_worlds(data)
		"gdn:rows":
			_on_rows(data)
		"gdn:detail":
			_on_detail(data)
		"gdn:error":
			status_changed.emit("Game: %s" % data[0])


func set_active(on: bool) -> void:
	if active == on:
		return
	active = on
	if active:
		request_view()
		if selected_entity >= 0:
			_send("select", [world_id, selected_entity])
	else:
		_send("stop", [])


func set_world(id: int) -> void:
	if id == world_id:
		return
	world_id = id
	offset = 0
	select(-1)
	request_view()


func set_query(p_include: PackedStringArray, p_exclude: PackedStringArray) -> void:
	include = p_include
	exclude = p_exclude
	offset = 0
	_rebuild_column_info()
	query_changed.emit()
	request_view()


func set_page(p_offset: int) -> void:
	offset = maxi(0, p_offset)
	request_view()


func set_rate(hz: float) -> void:
	rate_hz = hz
	request_view()


func request_view() -> void:
	if connected and active and world_id >= 0:
		_send("view", [world_id, include, exclude, offset, limit, rate_hz])


func select(entity: int) -> void:
	selected_entity = entity
	detail = []
	detail_changed.emit()
	if world_id >= 0:
		_send("select", [world_id, entity])


func edit(entity: int, component: String, field: String, value: Variant) -> void:
	_send("set", [world_id, entity, component, field, value])


func run_action(action: String, entity: int, args: Array = []) -> void:
	_send("action", [world_id, entity, action, args])


# Value of one table cell. Bool columns travel as bytes and come back as bool.
func cell_value(row: int, column: int) -> Variant:
	var value: Variant = columns[column][row]
	if column_info[column].type == TYPE_BOOL:
		return value != 0
	return value


func fields_of(component: String) -> Array:
	var entry: Dictionary = component_index.get(component, {})
	return entry.get("fields", [])


func _on_hello(data: Array) -> void:
	if int(data[0]) != PROTOCOL_VERSION:
		status_changed.emit("The game uses ECS debugger protocol %d, the editor plugin uses %d. Rebuild both." % [data[0], PROTOCOL_VERSION])
		return
	components = data[1]
	component_index = {}
	for component in components:
		component_index[component.name] = component
	actions = data[2]
	# Keep only the query parts the game still knows.
	include = _known(include)
	exclude = _known(exclude)
	_rebuild_column_info()
	schema_changed.emit()
	query_changed.emit()
	status_changed.emit("Connected.")
	request_view()


func _on_worlds(data: Array) -> void:
	world_ids = data[0]
	world_names = data[1]
	if not world_ids.has(world_id):
		world_id = world_ids[0] if world_ids.size() > 0 else -1
		offset = 0
		selected_entity = -1
		detail = []
		detail_changed.emit()
	worlds_changed.emit()
	request_view()


func _on_rows(data: Array) -> void:
	if int(data[0]) != world_id:
		return
	frame = data[1]
	total = data[2]
	offset = data[3]
	entities = data[4]
	columns = data[5]
	# A page can arrive for a query that just changed, drop it until the new one comes.
	if columns.size() != column_info.size():
		return
	rows_changed.emit()


func _on_detail(data: Array) -> void:
	if int(data[0]) != world_id or int(data[1]) != selected_entity:
		return
	detail = data[2]
	detail_changed.emit()


func _rebuild_column_info() -> void:
	column_info = []
	for component in include:
		for field in fields_of(component):
			column_info.append({"component": component, "field": field.name, "type": field.type})


func _known(names: PackedStringArray) -> PackedStringArray:
	var out := PackedStringArray()
	for name in names:
		if component_index.has(name):
			out.append(name)
	return out


func _send(message: String, data: Array) -> void:
	outgoing.emit("%s:%s" % [CAPTURE, message], data)
