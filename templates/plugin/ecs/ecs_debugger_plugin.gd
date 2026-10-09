@tool
extends EditorDebuggerPlugin

# Transport of the ECS panel. Receives the "gdn" messages of the running game and sends the
# model's requests back. It follows the most recently started game when several are running.

var model
var active_session := -1


func _has_capture(capture: String) -> bool:
	return capture == model.CAPTURE


func _capture(message: String, data: Array, session_id: int) -> bool:
	if session_id != active_session:
		_adopt(session_id)
	model.handle(message, data)
	return true


func _setup_session(session_id: int) -> void:
	var session := get_session(session_id)
	session.started.connect(_adopt.bind(session_id))
	session.stopped.connect(_on_stopped.bind(session_id))


func send(message: String, data: Array) -> void:
	if active_session < 0:
		return
	var session := get_session(active_session)
	if session != null and session.is_active():
		session.send_message(message, data)


func _adopt(session_id: int) -> void:
	if session_id == active_session and model.connected:
		return
	active_session = session_id
	model.session_started()


func _on_stopped(session_id: int) -> void:
	if session_id == active_session:
		model.session_stopped()
