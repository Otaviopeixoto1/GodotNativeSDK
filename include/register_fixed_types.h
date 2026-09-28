#pragma once

namespace GDNativeSDK {

// Registers every class that lives inside the SDK library.
// The project register_types.cpp calls this before registering
// its own project specific classes.
void register_fixed_types();
void unregister_fixed_types();

} // namespace GDNativeSDK
