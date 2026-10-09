#pragma once

#include "ecs/meta_bind.h"
#include "ecs/system.h"
#include "ecs/world.h"
#include "ecs/debug/debug_actions.h"

namespace GDNativeSDK::ECS {

// Registers the SDK ECS classes. register_fixed_types should call this.
void register_ecs_types();

} // namespace GDNativeSDK::ECS 
