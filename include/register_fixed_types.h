#pragma once

namespace my_native {

// Registers every class that lives inside the static library.
// The project register_types.cpp calls this before registering
// its own project specific classes.
void register_fixed_types();
void unregister_fixed_types();

} // namespace my_native
