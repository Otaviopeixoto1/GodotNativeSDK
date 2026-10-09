@tool
extends EditorDock

# UI of the ECS panel. It only renders the model and turns user input into model calls, so it can be
# replaced by another frontend without touching the model or the transport.
# As an EditorDock it can sit in a side slot, the bottom slot, or float as its own window.

const HINT := "Pause and step the game with Suspend and Next Frame (F10) in the Game view."

var model

var world_select: OptionButton
var include_menu: MenuButton
var exclude_menu: MenuButton
var rate_spin: SpinBox
var prev_button: Button
var next_button: Button
var page_label: Label
var frame_label: Label
var status_label: Label
var table: Tree
var detail_tree: Tree
var action_menu: PopupMenu

var _context_entity := -1


func _init() -> void:
	title = "ECS"
	layout_key = "godot_native_sdk_ecs"
	default_slot = DOCK_SLOT_BOTTOM
	available_layouts = DOCK_LAYOUT_ALL
	_build_ui()


func setup(p_model) -> void:
	model = p_model
	model.schema_changed.connect(_on_schema_changed)
	model.worlds_changed.connect(_on_worlds_changed)
	model.query_changed.connect(_rebuild_table_columns)
	model.rows_changed.connect(_on_rows_changed)
	model.detail_changed.connect(_on_detail_changed)
	model.status_changed.connect(func(text: String): status_label.text = text)
	_on_schema_changed()
	_on_worlds_changed()


func _notification(what: int) -> void:
	if what == NOTIFICATION_VISIBILITY_CHANGED and model != null:
		model.set_active(is_visible_in_tree())


#
# Layout
#

func _build_ui() -> void:
	var root := VBoxContainer.new()
	add_child(root)

	var bar := HBoxContainer.new()
	root.add_child(bar)

	world_select = OptionButton.new()
	world_select.tooltip_text = "World"
	world_select.item_selected.connect(func(index: int): model.set_world(model.world_ids[index]))
	bar.add_child(world_select)

	include_menu = _make_component_menu("With", "Entities must have every checked component. Their fields become columns.")
	bar.add_child(include_menu)
	exclude_menu = _make_component_menu("Without", "Entities must have none of the checked components.")
	bar.add_child(exclude_menu)

	var rate_label := Label.new()
	rate_label.text = "Hz"
	bar.add_child(rate_label)
	rate_spin = SpinBox.new()
	rate_spin.min_value = 1
	rate_spin.max_value = 60
	rate_spin.value = 10
	rate_spin.tooltip_text = "Samples per second"
	rate_spin.value_changed.connect(func(value: float): model.set_rate(value))
	bar.add_child(rate_spin)

	prev_button = Button.new()
	prev_button.text = "<"
	prev_button.pressed.connect(func(): model.set_page(model.offset - model.limit))
	bar.add_child(prev_button)
	page_label = Label.new()
	bar.add_child(page_label)
	next_button = Button.new()
	next_button.text = ">"
	next_button.pressed.connect(func(): model.set_page(model.offset + model.limit))
	bar.add_child(next_button)

	frame_label = Label.new()
	bar.add_child(frame_label)

	status_label = Label.new()
	status_label.text = HINT
	status_label.text_overrun_behavior = TextServer.OVERRUN_TRIM_ELLIPSIS
	status_label.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	root.add_child(status_label)

	var split := HSplitContainer.new()
	split.size_flags_vertical = Control.SIZE_EXPAND_FILL
	root.add_child(split)

	table = Tree.new()
	table.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	table.size_flags_stretch_ratio = 2.0
	table.hide_root = true
	table.column_titles_visible = true
	table.allow_rmb_select = true
	table.item_selected.connect(_on_table_selected)
	table.item_edited.connect(_on_table_edited)
	table.item_mouse_selected.connect(_on_table_mouse_selected)
	split.add_child(table)

	detail_tree = Tree.new()
	detail_tree.size_flags_horizontal = Control.SIZE_EXPAND_FILL
	detail_tree.hide_root = true
	detail_tree.columns = 2
	detail_tree.column_titles_visible = true
	detail_tree.set_column_title(0, "Component")
	detail_tree.set_column_title(1, "Value")
	detail_tree.item_edited.connect(_on_detail_edited)
	split.add_child(detail_tree)

	action_menu = PopupMenu.new()
	action_menu.id_pressed.connect(func(id: int): model.run_action(model.actions[id], _context_entity))
	add_child(action_menu)

	_rebuild_table_columns()


func _make_component_menu(text: String, tooltip: String) -> MenuButton:
	var menu := MenuButton.new()
	menu.text = text
	menu.tooltip_text = tooltip
	menu.flat = false
	var popup := menu.get_popup()
	popup.hide_on_checkable_item_selection = false
	popup.id_pressed.connect(func(id: int): _on_component_toggled(popup, id))
	return menu


#
# Model to UI
#

func _on_schema_changed() -> void:
	for menu in [include_menu, exclude_menu]:
		var popup: PopupMenu = menu.get_popup()
		popup.clear()
		for i in model.components.size():
			var component_name: String = model.components[i].name
			popup.add_check_item(component_name, i)
			var checked: bool = (model.include if menu == include_menu else model.exclude).has(component_name)
			popup.set_item_checked(i, checked)
	_rebuild_table_columns()


func _on_worlds_changed() -> void:
	world_select.clear()
	for i in model.world_ids.size():
		world_select.add_item(model.world_names[i], i)
		if model.world_ids[i] == model.world_id:
			world_select.select(i)
	world_select.disabled = model.world_ids.is_empty()


func _rebuild_table_columns() -> void:
	table.clear()
	table.create_item()
	table.columns = 1 + model.column_info.size() if model != null else 1
	table.set_column_title(0, "Entity")
	table.set_column_expand(0, false)
	table.set_column_custom_minimum_width(0, 70)
	if model == null:
		return
	for i in model.column_info.size():
		var info: Dictionary = model.column_info[i]
		table.set_column_title(i + 1, "%s.%s" % [info.component, info.field])
		table.set_column_expand(i + 1, true)
	_update_page_label()


func _on_rows_changed() -> void:
	var root := table.get_root()
	var items := root.get_children()
	# Items are reused so an edit in progress is not interrupted by the next sample.
	while items.size() > model.entities.size():
		items.pop_back().free()
	while items.size() < model.entities.size():
		items.append(table.create_item(root))
	for row in model.entities.size():
		var item: TreeItem = items[row]
		var entity: int = model.entities[row]
		item.set_text(0, str(entity))
		item.set_metadata(0, entity)
		for column in model.column_info.size():
			_show_value(item, column + 1, model.cell_value(row, column), model.column_info[column].type)
		if entity == model.selected_entity and table.get_selected() != item:
			item.select(0)
	frame_label.text = "frame %d" % model.frame
	_update_page_label()


func _on_detail_changed() -> void:
	var root := detail_tree.get_root()
	if root == null:
		root = detail_tree.create_item()
	var groups := root.get_children()
	# Rebuild only when the set of components changes, otherwise update the values in place.
	var names: Array = model.detail.map(func(entry): return entry[0])
	var same: bool = groups.size() == names.size()
	for i in mini(groups.size(), names.size()):
		same = same and groups[i].get_text(0) == names[i]
	if not same:
		for group in groups:
			group.free()
		groups = []
		for entry in model.detail:
			var group := detail_tree.create_item(root)
			group.set_text(0, entry[0])
			for field in model.fields_of(entry[0]):
				var item := detail_tree.create_item(group)
				item.set_text(0, field.name)
				item.set_metadata(0, {"component": entry[0], "field": field.name, "type": field.type})
			groups.append(group)
	for i in model.detail.size():
		var values: Array = model.detail[i][1]
		var fields := groups[i].get_children()
		for j in mini(fields.size(), values.size()):
			_show_value(fields[j], 1, values[j], fields[j].get_metadata(0).type)


func _update_page_label() -> void:
	var first: int = model.offset + 1 if model.total > 0 else 0
	var last: int = mini(model.offset + model.limit, model.total)
	page_label.text = "%d to %d of %d" % [first, last, model.total]
	prev_button.disabled = model.offset <= 0
	next_button.disabled = model.offset + model.limit >= model.total


#
# UI to model
#

func _on_component_toggled(popup: PopupMenu, id: int) -> void:
	var index := popup.get_item_index(id)
	popup.set_item_checked(index, not popup.is_item_checked(index))
	model.set_query(_checked(include_menu.get_popup()), _checked(exclude_menu.get_popup()))


func _checked(popup: PopupMenu) -> PackedStringArray:
	var out := PackedStringArray()
	for i in popup.item_count:
		if popup.is_item_checked(i):
			out.append(popup.get_item_text(i))
	return out


func _on_table_selected() -> void:
	var item := table.get_selected()
	if item != null and int(item.get_metadata(0)) != model.selected_entity:
		model.select(int(item.get_metadata(0)))


func _on_table_edited() -> void:
	var item := table.get_edited()
	var column := table.get_edited_column()
	var info: Dictionary = model.column_info[column - 1]
	_commit(int(item.get_metadata(0)), info.component, info.field, info.type, item, column)


func _on_detail_edited() -> void:
	var item := detail_tree.get_edited()
	var info: Dictionary = item.get_metadata(0)
	_commit(model.selected_entity, info.component, info.field, info.type, item, 1)


func _on_table_mouse_selected(_position: Vector2, button: int) -> void:
	if button != MOUSE_BUTTON_RIGHT or model.actions.is_empty():
		return
	_context_entity = int(table.get_selected().get_metadata(0))
	action_menu.clear()
	for i in model.actions.size():
		action_menu.add_item(model.actions[i], i)
	action_menu.position = DisplayServer.mouse_get_position()
	action_menu.popup()


func _commit(entity: int, component: String, field: String, type: int, item: TreeItem, column: int) -> void:
	var value: Variant
	if type == TYPE_BOOL:
		value = item.is_checked(column)
	else:
		value = _parse(item.get_text(column), type)
	if value == null:
		status_label.text = "Not a valid %s: %s" % [type_string(type), item.get_text(column)]
		return
	status_label.text = HINT
	model.edit(entity, component, field, value)


#
# Values
#

func _show_value(item: TreeItem, column: int, value: Variant, type: int) -> void:
	if type == TYPE_BOOL:
		item.set_cell_mode(column, TreeItem.CELL_MODE_CHECK)
		item.set_checked(column, bool(value))
	else:
		item.set_text(column, value if type == TYPE_STRING else var_to_str(value))
	item.set_editable(column, true)


func _parse(text: String, type: int) -> Variant:
	if type == TYPE_STRING:
		return text
	var value: Variant = str_to_var(text)
	if type == TYPE_FLOAT and typeof(value) == TYPE_INT:
		return float(value)
	if type == TYPE_INT and typeof(value) == TYPE_FLOAT:
		return int(value)
	return value if typeof(value) == type else null
