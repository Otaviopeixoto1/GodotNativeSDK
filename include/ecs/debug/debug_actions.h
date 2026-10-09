#pragma once

#include "ecs/world.h"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string_name.hpp>

#include <functional>

//
// Extension points of the ECS debugger
//
// The editor ECS panel can select an entity or trigger a named action on it. Nothing happens by default:
// project or SDK code registers handlers here to react, for example to show a visual effect in the game.
// Handlers run on the main thread, only while the game runs from the editor with the debugger active.
//
// Example Usage:
//		GDNativeSDK::ECS::add_debug_action("Flash", [](GDNativeSDK::ECS::ECSWorld &world, entt::entity entity, const godot::Array &args) {
//			// start an effect for this entity
//		});
//

namespace GDNativeSDK::ECS {

// Called when the selection in the editor changes. The world is nullptr when the selection was cleared.
using DebugSelectionHandler = std::function<void(ECSWorld *world, entt::entity entity)>;

// Called when the editor triggers a named action on an entity.
using DebugActionHandler = std::function<void(ECSWorld &world, entt::entity entity, const godot::Array &args)>;

// Register handlers during type registration or at any later point on the main thread.
void add_debug_selection_handler(const DebugSelectionHandler &handler);
void add_debug_action(const godot::StringName &name, const DebugActionHandler &handler);

//
// Used by the debug server
//

void notify_debug_selection(ECSWorld *world, entt::entity entity);

// Returns false when no action with that name exists.
bool run_debug_action(const godot::StringName &name, ECSWorld &world, entt::entity entity, const godot::Array &args);

// Names of every registered action, sent to the editor so it can list them.
godot::PackedStringArray debug_action_names();

} // namespace GDNativeSDK::ECS
