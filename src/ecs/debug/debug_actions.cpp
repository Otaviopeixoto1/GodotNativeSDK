#include "ecs/debug/debug_actions.h"

#include <algorithm>
#include <unordered_map>
#include <vector>

namespace GDNativeSDK::ECS {

namespace {
// Both registries start empty. The debugger works without any handler.
std::vector<DebugSelectionHandler> selection_handlers;
std::unordered_map<godot::StringName, DebugActionHandler> actions;
} // namespace

void add_debug_selection_handler(const DebugSelectionHandler &handler) {
	if (handler) {
		selection_handlers.push_back(handler);
	}
}

void add_debug_action(const godot::StringName &name, const DebugActionHandler &handler) {
	ERR_FAIL_COND_MSG(!handler, "ECS::DEBUG: empty action handler");
	ERR_FAIL_COND_MSG(actions.count(name) > 0, godot::String("ECS::DEBUG: action registered twice: ") + godot::String(name));
	actions.emplace(name, handler);
}

void notify_debug_selection(ECSWorld *world, entt::entity entity) {
	for (const DebugSelectionHandler &handler : selection_handlers) {
		handler(world, entity);
	}
}

bool run_debug_action(const godot::StringName &name, ECSWorld &world, entt::entity entity, const godot::Array &args) {
	auto it = actions.find(name);
	if (it == actions.end()) {
		return false;
	}
	it->second(world, entity, args);
	return true;
}

godot::PackedStringArray debug_action_names() {
	// Sorted, so the editor lists them in a stable order.
	std::vector<godot::String> names;
	for (const auto &[name, handler] : actions) {
		names.push_back(godot::String(name));
	}
	std::sort(names.begin(), names.end(), [](const godot::String &a, const godot::String &b) { return a < b; });
	godot::PackedStringArray out;
	for (const godot::String &name : names) {
		out.push_back(name);
	}
	return out;
}

} // namespace GDNativeSDK::ECS
