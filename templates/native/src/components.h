#pragma once
#include "ecs/world.h"

//
// Todo: register all Components (or their include files) here !
//


struct ExampleComponent
{
	float val;
};
GDN_COMPONENT(ExampleComponent, GDN_FIELD(val))
