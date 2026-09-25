#include "register_fixed_types.h"
#include "game_object_base.h"
#include <godot_cpp/core/class_db.hpp>

namespace my_native { //TODO Rename namespace

void register_fixed_types() {
	GDREGISTER_ABSTRACT_CLASS(GameObjectBase);
}

void unregister_fixed_types() {
	
}

} // namespace my_native
