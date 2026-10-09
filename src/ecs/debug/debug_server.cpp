#include "ecs/debug/debug_server.h"
#include "ecs/debug/debug_actions.h"
#include "ecs/debug/debug_inspector.h"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/engine_debugger.hpp>
#include <godot_cpp/classes/main_loop.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/callable_method_pointer.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include <algorithm>
#include <vector>

//
// Message layouts
//
// Editor to game, the capture receives the name without the "gdn:" prefix:
//		hello   []                                                   asks for gdn:hello and gdn:worlds again
//		view    [world, include, exclude, offset, limit, rate_hz]    starts or changes the table query
//		stop    []                                                   stops all sampling until the next view or select
//		set     [world, entity, component, field, value]             edits one field
//		select  [world, entity]                                      entity is negative to clear the selection
//		action  [world, entity, name, args]                          runs a registered debug action
//
// Game to editor:
//		gdn:hello   [protocol, components, actions]                  see inspector_describe_components
//		gdn:worlds  [ids, names]                                     PackedInt64Array and PackedStringArray
//		gdn:rows    [world, frame, total, offset, entities, columns] see inspector_read_rows
//		gdn:detail  [world, entity, components]                      see inspector_read_entity
//		gdn:error   [text]
//

namespace GDNativeSDK::ECS {

namespace {

constexpr const char *CAPTURE = "gdn";

struct WorldEntry {
	int64_t id = 0;
	ECSWorld *world = nullptr;
};

struct DebugServer {
	std::vector<WorldEntry> worlds;
	int64_t next_world_id = 1;

	godot::Callable capture_callable;
	godot::Callable frame_callable;
	bool frame_connected = false;

	// Samples are only taken while the editor panel is visible and asks for them.
	bool streaming = false;

	// The current table query.
	bool view_active = false;
	int64_t view_world = 0;
	InspectorQuery query;
	uint64_t interval_usec = 100000;
	uint64_t last_sample_usec = 0;

	// The entity selected in the editor, its components are sent with every sample.
	int64_t selected_world = 0;
	entt::entity selected = entt::null;
};

DebugServer *server = nullptr;

godot::SceneTree *scene_tree() {
	return godot::Object::cast_to<godot::SceneTree>(godot::Engine::get_singleton()->get_main_loop());
}

ECSWorld *find_world(int64_t id) {
	for (const WorldEntry &entry : server->worlds) {
		if (entry.id == id) {
			return entry.world;
		}
	}
	return nullptr;
}

void send(const char *name, const godot::Array &data) {
	godot::EngineDebugger::get_singleton()->send_message(godot::String(CAPTURE) + ":" + name, data);
}

void send_error(const godot::String &text) {
	godot::Array data;
	data.push_back(text);
	send("error", data);
}

void send_hello() {
	godot::Array data;
	data.push_back(DEBUG_PROTOCOL_VERSION);
	data.push_back(inspector_describe_components());
	data.push_back(debug_action_names());
	send("hello", data);
}

void send_worlds() {
	godot::PackedInt64Array ids;
	godot::PackedStringArray names;
	for (const WorldEntry &entry : server->worlds) {
		ids.push_back(entry.id);
		names.push_back("World " + godot::String::num_int64(entry.id));
	}
	godot::Array data;
	data.push_back(ids);
	data.push_back(names);
	send("worlds", data);
}

void send_detail() {
	if (server->selected == entt::null) {
		return;
	}
	ECSWorld *world = find_world(server->selected_world);
	godot::Array data;
	data.push_back(server->selected_world);
	data.push_back(ECSWorld::from_entity(server->selected));
	data.push_back(world ? inspector_read_entity(*world, server->selected) : godot::Array());
	send("detail", data);
}

// Reads the current page and the selected entity, and sends both.
void sample() {
	server->last_sample_usec = godot::Time::get_singleton()->get_ticks_usec();
	if (server->view_active) {
		if (ECSWorld *world = find_world(server->view_world)) {
			godot::Array page = inspector_read_rows(*world, server->query);
			godot::Array data;
			data.push_back(server->view_world);
			data.push_back(static_cast<int64_t>(godot::Engine::get_singleton()->get_process_frames()));
			data.append_array(page);
			send("rows", data);
		}
	}
	send_detail();
}

// Connected to SceneTree.process_frame. It fires every frame, also while the game is suspended from the
// Game view, before nodes process. A sample therefore shows the state the previous frame left behind.
void on_process_frame() {
	if (server == nullptr || !server->streaming) {
		return;
	}
	const uint64_t now = godot::Time::get_singleton()->get_ticks_usec();
	if (now - server->last_sample_usec >= server->interval_usec) {
		sample();
	}
}

void connect_frame() {
	if (server->frame_connected) {
		return;
	}
	if (godot::SceneTree *tree = scene_tree()) {
		tree->connect("process_frame", server->frame_callable);
		server->frame_connected = true;
	}
}

entt::entity entity_arg(const godot::Variant &value) {
	return ECSWorld::to_entity(static_cast<int64_t>(value));
}

void handle_view(const godot::Array &data) {
	ERR_FAIL_COND_MSG(data.size() < 6, "ECS::DEBUG: malformed view message");
	InspectorQuery query;
	godot::String error;
	if (!inspector_resolve_query(data[1], data[2], data[3], data[4], query, error)) {
		send_error(error);
		return;
	}
	const double rate = std::clamp(static_cast<double>(data[5]), 1.0, 60.0);
	server->query = query;
	server->view_world = data[0];
	server->view_active = true;
	server->streaming = true;
	server->interval_usec = static_cast<uint64_t>(1000000.0 / rate);
	connect_frame();
	sample();
}

void handle_set(const godot::Array &data) {
	ERR_FAIL_COND_MSG(data.size() < 5, "ECS::DEBUG: malformed set message");
	ECSWorld *world = find_world(data[0]);
	if (world == nullptr) {
		send_error("The world no longer exists.");
		return;
	}
	godot::String error;
	if (!inspector_write_field(*world, entity_arg(data[1]), data[2], data[3], data[4], error)) {
		send_error(error);
	}
	sample();
}

void handle_select(const godot::Array &data) {
	ERR_FAIL_COND_MSG(data.size() < 2, "ECS::DEBUG: malformed select message");
	ECSWorld *world = find_world(data[0]);
	if (world == nullptr || static_cast<int64_t>(data[1]) < 0) {
		server->selected_world = 0;
		server->selected = entt::null;
		notify_debug_selection(nullptr, entt::null);
		return;
	}
	server->selected_world = data[0];
	server->selected = entity_arg(data[1]);
	server->streaming = true;
	connect_frame();
	notify_debug_selection(world, server->selected);
	send_detail();
}

void handle_action(const godot::Array &data) {
	ERR_FAIL_COND_MSG(data.size() < 4, "ECS::DEBUG: malformed action message");
	ECSWorld *world = find_world(data[0]);
	if (world == nullptr) {
		send_error("The world no longer exists.");
		return;
	}
	const godot::StringName name = data[2];
	if (!run_debug_action(name, *world, entity_arg(data[1]), data[3])) {
		send_error("Unknown debug action: " + godot::String(name));
	}
}

// Called by the engine debugger on the main thread, once per frame, for every "gdn:" message.
bool on_capture(const godot::String &message, const godot::Array &data) {
	if (server == nullptr) {
		return false;
	}
	if (message == "view") {
		handle_view(data);
	} else if (message == "stop") {
		server->streaming = false;
	} else if (message == "set") {
		handle_set(data);
	} else if (message == "select") {
		handle_select(data);
	} else if (message == "action") {
		handle_action(data);
	} else if (message == "hello") {
		send_hello();
		send_worlds();
	} else {
		return false;
	}
	return true;
}

} // namespace

//
// Public methods
//
void debug_world_created(ECSWorld *world) {
	if (server == nullptr) {
		godot::EngineDebugger *debugger = godot::EngineDebugger::get_singleton();
		if (debugger == nullptr || !debugger->is_active()) {
			return;
		}
		server = new DebugServer();
		server->capture_callable = callable_mp_static(&on_capture);
		server->frame_callable = callable_mp_static(&on_process_frame);
		debugger->register_message_capture(CAPTURE, server->capture_callable);
		send_hello();
	}
	server->worlds.push_back({ server->next_world_id++, world });
	send_worlds();
}

void debug_world_destroyed(ECSWorld *world) {
	if (server == nullptr) {
		return;
	}
	for (auto it = server->worlds.begin(); it != server->worlds.end(); ++it) {
		if (it->world != world) {
			continue;
		}
		if (server->view_world == it->id) {
			server->view_active = false;
		}
		if (server->selected_world == it->id) {
			server->selected_world = 0;
			server->selected = entt::null;
		}
		server->worlds.erase(it);
		send_worlds();
		return;
	}
}

void shutdown_debug_server() {
	if (server == nullptr) {
		return;
	}
	if (godot::EngineDebugger *debugger = godot::EngineDebugger::get_singleton()) {
		if (debugger->has_capture(CAPTURE)) {
			debugger->unregister_message_capture(CAPTURE);
		}
	}
	// The scene tree is usually gone already when the library unloads at exit.
	if (server->frame_connected) {
		if (godot::SceneTree *tree = scene_tree()) {
			if (tree->is_connected("process_frame", server->frame_callable)) {
				tree->disconnect("process_frame", server->frame_callable);
			}
		}
	}
	delete server;
	server = nullptr;
}

} // namespace GDNativeSDK::ECS
