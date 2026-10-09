#include "ecs/ecs.h"

namespace GDNativeSDK::ECS {

void register_ecs_types() {
	GDREGISTER_ABSTRACT_CLASS(ECSComponent);
	GDREGISTER_ABSTRACT_CLASS(ECSChunk);
	GDREGISTER_ABSTRACT_CLASS(ECSSystem);
	GDREGISTER_CLASS(ECSChunkIterator);
	GDREGISTER_CLASS(ECSWorld);
}

} //namespace GDNativeSDK::ECS
