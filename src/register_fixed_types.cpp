#include "register_fixed_types.h"
#include "ecs/ecs.h"
#include "ecs/debug/debug_server.h"
#include <godot_cpp/core/class_db.hpp>

namespace GDNativeSDK { //TODO Rename namespace

void register_fixed_types(godot::ModuleInitializationLevel p_level) {
	if (p_level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		ECS::register_ecs_types();
	}
}

void unregister_fixed_types(godot::ModuleInitializationLevel p_level) {
	if (p_level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		// Finish the ECS debug server
		ECS::shutdown_debug_server();
	}
}

} // namespace GDNativeSDK
