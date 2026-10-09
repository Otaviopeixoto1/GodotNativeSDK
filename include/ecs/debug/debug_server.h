#pragma once

#include "ecs/world.h"

//
// Debug server of the ECS debugger. Its a simple self-contained singleton. Not accessible outside of native code.
// The main DebugServer is implemented in debug_server.cpp and is not registered to godot ClassDB.
//
// The functions defined in this file link every ECSWorld of a running game to the editor ECS panel through the debugger connection.
// The server instance is created when the first ECSWorld is registered and only when the game was started from the editor.
// (Release builds and exported games never create it)
//
// Messages use the "gdn" capture prefix:
//
//		editor to game:  gdn:hello  gdn:view  gdn:stop  gdn:set  gdn:select  gdn:action
//		game to editor:  gdn:hello  gdn:worlds  gdn:rows  gdn:detail  gdn:error
//
// The layout of each message is described in debug_server.cpp and mirrored by the editor plugin.
//

namespace GDNativeSDK::ECS {

// Bump when a message layout changes, the editor plugin compares it.
constexpr int64_t DEBUG_PROTOCOL_VERSION = 1;

// Called by ECSWorld when it is created and destroyed.
void debug_world_created(ECSWorld *world);
void debug_world_destroyed(ECSWorld *world);

// Called by unregister_fixed_types at the scene level, before the library unloads.
void shutdown_debug_server();

} // namespace GDNativeSDK::ECS
