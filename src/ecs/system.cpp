#include "ecs/ecs.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <algorithm>
#include <unordered_map>

namespace GDNativeSDK::ECS {

namespace {
// Persistent storage for system descriptors. This is searched every time when querying world for Systems with the GDScript API
std::unordered_map<godot::StringName, SystemDescriptor> systems;
} // namespace



void add_system_ops(const SystemDescriptor &ops) {
	auto [it, inserted] = systems.emplace(ops.godot_class, ops);
	ERR_FAIL_COND_MSG(!inserted, "system registered twice");
}

const SystemDescriptor *find_system(const godot::StringName &godot_class) {
	auto it = systems.find(godot_class);
	return it == systems.end() ? nullptr : &it->second;
}

//
// ECSChunk
//
godot::PackedInt64Array ECSChunk::get_entities() const {
	godot::PackedInt64Array out;
	if (entities == nullptr) {
		return out;
	}
	out.resize(static_cast<int64_t>(count));
	int64_t *w = out.ptrw();
	for (std::size_t i = 0; i < count; i++) {
		w[i] = ECSWorld::from_entity((*entities)[start + i]);
	}
	return out;
}

void *const *ECSChunk::component_ptrs(std::size_t slot, bool write) {
	ERR_FAIL_COND_V_MSG(world.is_null() || ops == nullptr || components == nullptr, nullptr, "chunk is not active");
	ERR_FAIL_COND_V_MSG(world->structure_version != version, nullptr, "entities or components were added or removed since this chunk was gathered");
	ERR_FAIL_COND_V(slot >= components->size(), nullptr);
	ERR_FAIL_COND_V_MSG(write && !ops->writable[slot], nullptr, "this column belongs to a Read component and is read only");
	return (*components)[slot].data() + start;
}

void ECSChunk::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("size"), &ECSChunk::size);
	godot::ClassDB::bind_method(godot::D_METHOD("get_entities"), &ECSChunk::get_entities);
	ADD_PROPERTY(godot::PropertyInfo(godot::Variant::PACKED_INT64_ARRAY, "entities"), "", "get_entities");
}



//
// ECSChunkIterator
//
bool ECSChunkIterator::begin() {
	ERR_FAIL_COND_V(world.is_null() || ops == nullptr, false);
	ops->gather(world->registry, entities, components);
	start = 0;
	if (entities.empty()) {
		return false;
	}
	if (chunk.is_null()) {
		chunk = ops->make_chunk();
	}
	chunk->world = world;
	chunk->ops = ops;
	chunk->version = world->structure_version;
	chunk->entities = &entities;
	chunk->components = &components;
	chunk->start = 0;
	chunk->count = std::min(chunk_size, entities.size());
	return true;
}

bool ECSChunkIterator::next() {
	start += chunk_size;
	if (start >= entities.size()) {
		return false;
	}
	chunk->start = start;
	chunk->count = std::min(chunk_size, entities.size() - start);
	return true;
}

bool ECSChunkIterator::_iter_init(const godot::Array &) {
	return begin();
}

bool ECSChunkIterator::_iter_next(const godot::Array &) {
	return next();
}

godot::Variant ECSChunkIterator::_iter_get(const godot::Variant &) {
	return chunk;
}

void ECSChunkIterator::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("_iter_init", "iter"), &ECSChunkIterator::_iter_init);
	godot::ClassDB::bind_method(godot::D_METHOD("_iter_next", "iter"), &ECSChunkIterator::_iter_next);
	godot::ClassDB::bind_method(godot::D_METHOD("_iter_get", "iter"), &ECSChunkIterator::_iter_get);
}

//
// ECSSystem
//
int64_t ECSSystem::run_native() {
	ERR_FAIL_COND_V(world.is_null() || ops == nullptr, 0);
	ERR_FAIL_NULL_V_MSG(ops->run_native, 0, godot::String(ops->godot_class) + " has no native body");
	return static_cast<int64_t>(ops->run_native(world->registry));
}

godot::Ref<ECSChunkIterator> ECSSystem::chunks(int64_t chunk_size) {
	godot::Ref<ECSChunkIterator> it;
	it.instantiate();
	it->world = world;
	it->ops = ops;
	it->chunk_size = static_cast<std::size_t>(std::max<int64_t>(1, chunk_size));
	return it;
}

int64_t ECSSystem::each_chunk(const godot::Callable &callback, int64_t chunk_size) {
	ERR_FAIL_COND_V_MSG(!callback.is_valid(), 0, "invalid callback");
	godot::Ref<ECSChunkIterator> it = chunks(chunk_size);
	int64_t calls = 0;
	for (bool more = it->begin(); more; more = it->next()) {
		callback.call(it->chunk);
		calls++;
	}
	return calls;
}

int64_t ECSSystem::count() {
	ERR_FAIL_COND_V(world.is_null() || ops == nullptr, 0);
	std::vector<entt::entity> entities;
	std::vector<std::vector<void *>> components;
	ops->gather(world->registry, entities, components);
	return static_cast<int64_t>(entities.size());
}

void ECSSystem::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("run_native"), &ECSSystem::run_native);
	godot::ClassDB::bind_method(godot::D_METHOD("chunks", "chunk_size"), &ECSSystem::chunks, DEFVAL(1024));
	godot::ClassDB::bind_method(godot::D_METHOD("each_chunk", "callback", "chunk_size"), &ECSSystem::each_chunk, DEFVAL(1024));
	godot::ClassDB::bind_method(godot::D_METHOD("count"), &ECSSystem::count);
}

} // namespace GDNativeSDK::ECS
