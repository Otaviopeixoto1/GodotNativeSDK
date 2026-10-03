#include "register_types.h"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "example_class.h"
#include "systems.h"
#include "register_fixed_types.h"

using namespace godot;



void initialize_gdextension_types(ModuleInitializationLevel p_level)
{
	// The SDK classes must be registered before project classes that extend them.
	GDNativeSDK::register_fixed_types(p_level);

	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	GDREGISTER_CLASS(ExampleClass);
	//use GDNativeSDK::ECS::register_component<T>(); to register all components
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level)
{
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		// Project teardown goes here, before the SDK's.
	}
 
	GDNativeSDK::unregister_fixed_types(p_level);
}

extern "C"
{
	GDExtensionBool GDE_EXPORT godot_native_sdk_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization)
	{
		GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
		init_obj.register_initializer(initialize_gdextension_types);
		init_obj.register_terminator(uninitialize_gdextension_types);
		init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

		return init_obj.init();
	}
}
