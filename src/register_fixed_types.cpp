#include "register_fixed_types.h"
#include "game_object_base.h"
#include <godot_cpp/core/class_db.hpp>

namespace GDNativeSDK { //TODO Rename namespace

void register_fixed_types(godot::ModuleInitializationLevel p_level) {
	// GameObjectBase extends Node2D, so it cannot be registered before the scene level.
	if (p_level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_ABSTRACT_CLASS(GameObjectBase);
	}
}

void unregister_fixed_types(godot::ModuleInitializationLevel p_level) {
	// godot-cpp unregisters the classes of a level when it tears that level down.
}

} // namespace GDNativeSDK
