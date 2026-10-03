#pragma once

#include <godot_cpp/core/class_db.hpp>

namespace GDNativeSDK {

// Registers the classes that live inside the SDK library.
// The project register_types.cpp calls these at every initialization level, and the SDK
// decides which level each of its classes belongs to. Call register before the project
// registers classes that extend SDK ones, and unregister after the project's own cleanup.
void register_fixed_types(godot::ModuleInitializationLevel p_level);
void unregister_fixed_types(godot::ModuleInitializationLevel p_level);

} // namespace GDNativeSDK
