#pragma once
#include <godot_cpp/variant/utility_functions.hpp>

#include "ecs/system.h"
#include "components.h"
//
// Todo: register all Systems (or their include files) here !
//

NATIVE_SYSTEM(ExampleSystem, GDNativeSDK::ECS::Read<ExampleComponent>) {
	static void body(entt::entity entity, const ExampleComponent &e) {
		godot::UtilityFunctions::print("entity ", GDNativeSDK::ECS::ECSWorld::from_entity(entity), " val ", e.val);
	}
};
