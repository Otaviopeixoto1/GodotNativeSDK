#pragma once

#include "ecs/world.h"

#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>

#include <cstdint>
#include <vector>

//
// Inspector core of the ECS debugger
//
// Answers queries, reads component values and applies edits. It only knows worlds, components and their
// reflection (see meta_bind.h), never the transport, so the same functions can serve another frontend later.
// Values travel as packed columns: one array per field, aligned with the entity list.
//

namespace GDNativeSDK::ECS {

// A query from the editor table, already resolved to registered components.
struct InspectorQuery {
	std::vector<const ComponentDesc *> include;
	std::vector<const ComponentDesc *> exclude;
	int64_t offset = 0;
	int64_t limit = 100;
};

// Upper bound for rows per message. The debugger connection drops messages that are too large.
constexpr int64_t INSPECTOR_MAX_ROWS = 500;

// Resolves component class names into a query. Fills error and returns false on an unknown name.
bool inspector_resolve_query(const godot::PackedStringArray &include, const godot::PackedStringArray &exclude, int64_t offset, int64_t limit, InspectorQuery &r_query, godot::String &r_error);

// Every registered component with its fields, as plain data for the editor.
// Format: Array of Dictionary { name: String, fields: Array of Dictionary { name: String, type: int } }
godot::Array inspector_describe_components();

// Runs the query on a world and reads one page.
// Format: [ total: int, offset: int, entities: PackedInt64Array, columns: Array ]
// Columns follow the include order, and inside each component its field order from the schema.
godot::Array inspector_read_rows(ECSWorld &world, const InspectorQuery &query);

// Reads every registered component of one entity.
// Format: Array of [ component: String, values: Array ] sorted by component name, empty when the entity is gone.
godot::Array inspector_read_entity(ECSWorld &world, entt::entity entity);

// Writes one field of one component. Fills error and returns false when it cannot be applied.
bool inspector_write_field(ECSWorld &world, entt::entity entity, const godot::StringName &component, const godot::StringName &field, const godot::Variant &value, godot::String &r_error);

} // namespace GDNativeSDK::ECS
