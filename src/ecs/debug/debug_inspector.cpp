#include "ecs/debug/debug_inspector.h"

#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include <algorithm>

namespace GDNativeSDK::ECS {

namespace {

godot::String to_string(std::string_view sv) {
	return godot::String::utf8(sv.data(), static_cast<int>(sv.size()));
}

// Sorts by entity index, so rows keep their place while components move around inside the storages.
void sort_entities(std::vector<entt::entity> &entities) {
	std::sort(entities.begin(), entities.end(), [](entt::entity a, entt::entity b) { return entt::to_entity(a) < entt::to_entity(b); });
}

// Every entity matching the query, in a stable order.
std::vector<entt::entity> match(ECSWorld &world, const InspectorQuery &query) {
	std::vector<entt::entity> out;
	if (query.include.empty()) {
		// No component asked for: list every live entity, then drop the excluded ones.
		for (auto [entity] : world.registry.storage<entt::entity>().each()) {
			bool excluded = false;
			for (const ComponentDesc *desc : query.exclude) {
				auto *storage = world.registry.storage(desc->id);
				if (storage != nullptr && storage->contains(entity)) {
					excluded = true;
					break;
				}
			}
			if (!excluded) {
				out.push_back(entity);
			}
		}
	} else {
		// The runtime view walks the smallest storage and checks the others, without knowing any type.
		entt::runtime_view view;
		for (const ComponentDesc *desc : query.include) {
			auto *storage = world.registry.storage(desc->id);
			if (storage == nullptr) {
				return out;
			}
			view.iterate(*storage);
		}
		for (const ComponentDesc *desc : query.exclude) {
			if (auto *storage = world.registry.storage(desc->id)) {
				view.exclude(*storage);
			}
		}
		for (entt::entity entity : view) {
			out.push_back(entity);
		}
	}
	sort_entities(out);
	return out;
}

} // namespace

bool inspector_resolve_query(const godot::PackedStringArray &include, const godot::PackedStringArray &exclude, int64_t offset, int64_t limit, InspectorQuery &r_query, godot::String &r_error) {
	r_query = InspectorQuery();
	for (int64_t i = 0; i < include.size(); i++) {
		const ComponentDesc *desc = find_component(godot::StringName(include[i]));
		if (desc == nullptr) {
			r_error = "Unknown component in query: " + include[i];
			return false;
		}
		r_query.include.push_back(desc);
	}
	for (int64_t i = 0; i < exclude.size(); i++) {
		const ComponentDesc *desc = find_component(godot::StringName(exclude[i]));
		if (desc == nullptr) {
			r_error = "Unknown component in query: " + exclude[i];
			return false;
		}
		r_query.exclude.push_back(desc);
	}
	r_query.offset = std::max<int64_t>(0, offset);
	r_query.limit = std::clamp<int64_t>(limit, 1, INSPECTOR_MAX_ROWS);
	return true;
}

godot::Array inspector_describe_components() {
	godot::Array out;
	for (const ComponentDesc *desc : all_components()) {
		godot::Array fields;
		for (auto [id, data] : desc->meta.data()) {
			const FieldOps *ops = data.custom();
			if (ops == nullptr) {
				continue;
			}
			godot::Dictionary field;
			field["name"] = to_string(data.name());
			field["type"] = static_cast<int64_t>(ops->value_type);
			fields.push_back(field);
		}
		godot::Dictionary component;
		component["name"] = godot::String(desc->godot_class);
		component["fields"] = fields;
		out.push_back(component);
	}
	return out;
}

godot::Array inspector_read_rows(ECSWorld &world, const InspectorQuery &query) {
	std::vector<entt::entity> matched = match(world, query);
	const int64_t total = static_cast<int64_t>(matched.size());
	const int64_t start = std::min(query.offset, total);
	const int64_t count = std::min(query.limit, total - start);

	godot::PackedInt64Array entities;
	entities.resize(count);
	for (int64_t i = 0; i < count; i++) {
		entities.set(i, ECSWorld::from_entity(matched[start + i]));
	}

	// One packed column per field, built from the component pointers of the page.
	godot::Array columns;
	std::vector<void *> pointers(static_cast<std::size_t>(count));
	for (const ComponentDesc *desc : query.include) {
		auto *storage = world.registry.storage(desc->id);
		for (int64_t i = 0; i < count; i++) {
			pointers[i] = storage->value(matched[start + i]);
		}
		for (auto [id, data] : desc->meta.data()) {
			const FieldOps *ops = data.custom();
			if (ops == nullptr) {
				continue;
			}
			columns.push_back(ops->pack_column(pointers.data(), pointers.size()));
		}
	}

	godot::Array out;
	out.push_back(total);
	out.push_back(start);
	out.push_back(entities);
	out.push_back(columns);
	return out;
}

godot::Array inspector_read_entity(ECSWorld &world, entt::entity entity) {
	godot::Array out;
	if (!world.registry.valid(entity)) {
		return out;
	}
	for (const ComponentDesc *desc : all_components()) {
		auto *storage = world.registry.storage(desc->id);
		if (storage == nullptr || !storage->contains(entity)) {
			continue;
		}
		// Tags have no fields, so only their presence is reported.
		godot::Array values;
		const void *component = storage->value(entity);
		for (auto [id, data] : desc->meta.data()) {
			const FieldOps *ops = data.custom();
			if (ops != nullptr && component != nullptr) {
				values.push_back(ops->get(component));
			}
		}
		godot::Array entry;
		entry.push_back(godot::String(desc->godot_class));
		entry.push_back(values);
		out.push_back(entry);
	}
	return out;
}

bool inspector_write_field(ECSWorld &world, entt::entity entity, const godot::StringName &component, const godot::StringName &field, const godot::Variant &value, godot::String &r_error) {
	const ComponentDesc *desc = find_component(component);
	if (desc == nullptr) {
		r_error = "Unknown component: " + godot::String(component);
		return false;
	}
	if (!world.registry.valid(entity)) {
		r_error = "The entity no longer exists.";
		return false;
	}
	auto *storage = world.registry.storage(desc->id);
	if (storage == nullptr || !storage->contains(entity)) {
		r_error = "The entity no longer has " + godot::String(component) + ".";
		return false;
	}
	for (auto [id, data] : desc->meta.data()) {
		const FieldOps *ops = data.custom();
		if (ops == nullptr || to_string(data.name()) != godot::String(field)) {
			continue;
		}
		if (!ops->set(storage->value(entity), value)) {
			r_error = "Value has the wrong type for " + godot::String(component) + "." + godot::String(field) + ".";
			return false;
		}
		return true;
	}
	r_error = "Unknown field: " + godot::String(component) + "." + godot::String(field);
	return false;
}

} // namespace GDNativeSDK::ECS
