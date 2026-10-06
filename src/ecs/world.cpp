#include "ecs/ecs.h"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <deque>

namespace GDNativeSDK::ECS {

namespace {
// Persistent storage for component descriptors
// TODO: Convert to std::unordered_map !
std::deque<ComponentDesc> components;
} // namespace



void add_component_desc(const ComponentDesc &desc) {
	components.push_back(desc);
}

const ComponentDesc *find_component(entt::id_type id) {
	// TODO: Use a std::map from entt::id_type to the ComponentDesc instead of std::dequeue
	for (const ComponentDesc &d : components) {
		if (d.id == id) {
			return &d;
		}
	}
	return nullptr;
}

const ComponentDesc *find_component(const godot::StringName &godot_class) {
	// TODO: Use a std::map from entt::id_type to the ComponentDesc instead of std::dequeue
	for (const ComponentDesc &d : components) {
		if (d.godot_class == godot_class) {
			return &d;
		}
	}
	return nullptr;
}

godot::StringName resolve_class_token(const godot::Variant &token) {
	switch (token.get_type()) {
		case godot::Variant::STRING_NAME:
		case godot::Variant::STRING:
			return token;
		case godot::Variant::OBJECT: {
			godot::Object *object = token;
			ERR_FAIL_NULL_V_MSG(object, godot::StringName(), "null component or system");
			if (godot::Object::cast_to<ECSComponent>(object) || godot::Object::cast_to<ECSSystem>(object)) {
				return object->get_class();
			}
			// The class itself, as in world.system(Move): it only offers new(), so ask an instance.
			godot::Variant instance = object->call("new");
			godot::Object *created = instance;
			ERR_FAIL_NULL_V_MSG(created, godot::StringName(), "not a component or system class");
			return created->get_class();
		}
		default:
			ERR_FAIL_V_MSG(godot::StringName(), "expected a component or system class, instance or name");
	}
}

//
// ECSComponent
//
void *ECSComponent::data() {
	if (world.is_null()) {
		return own.data();
	}
	auto *storage = world->registry.storage(component);
	ERR_FAIL_COND_V_MSG(storage == nullptr || !storage->contains(entity), nullptr, "the entity no longer has this component");
	return storage->value(entity);
}

int64_t ECSComponent::get_entity() const {
	return world.is_valid() ? ECSWorld::from_entity(entity) : -1;
}

void ECSComponent::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("is_attached"), &ECSComponent::is_attached);
	godot::ClassDB::bind_method(godot::D_METHOD("get_entity"), &ECSComponent::get_entity);
}

//
// ECSWorld
//
ECSWorld::ECSWorld() {
	// Create every storage up front, so looking one up by id never finds nothing.
	for (const ComponentDesc &d : components) {
		d.touch(registry);
	}
}

int64_t ECSWorld::create() {
	return from_entity(registry.create());
}

void ECSWorld::destroy(int64_t entity) {
	ERR_FAIL_COND_MSG(!is_alive(entity), "entity is not alive");
	registry.destroy(to_entity(entity));
	structure_version++;
}

bool ECSWorld::is_alive(int64_t entity) const {
	return registry.valid(to_entity(entity));
}

void ECSWorld::add(int64_t entity, const godot::Ref<ECSComponent> &value) {
	ERR_FAIL_COND_MSG(!is_alive(entity), "entity is not alive");
	ERR_FAIL_COND_MSG(value.is_null(), "null component");
	const ComponentDesc *desc = find_component(value->component);
	ERR_FAIL_NULL_MSG(desc, "component is not registered");
	void *src = value->data();
	ERR_FAIL_NULL(src);
	desc->emplace_copy(registry, to_entity(entity), src);
	structure_version++;
}

void ECSWorld::remove(int64_t entity, const godot::Variant &component) {
	ERR_FAIL_COND_MSG(!is_alive(entity), "entity is not alive");
	const ComponentDesc *desc = find_component(resolve_class_token(component));
	ERR_FAIL_NULL_MSG(desc, "component is not registered");
	if (registry.storage(desc->id)->remove(to_entity(entity))) {
		structure_version++;
	}
}

bool ECSWorld::has(int64_t entity, const godot::Variant &component) {
	const ComponentDesc *desc = find_component(resolve_class_token(component));
	ERR_FAIL_NULL_V_MSG(desc, false, "component is not registered");
	return is_alive(entity) && registry.storage(desc->id)->contains(to_entity(entity));
}

godot::Ref<ECSComponent> ECSWorld::component(int64_t entity, const godot::Variant &component) {
	const ComponentDesc *desc = find_component(resolve_class_token(component));
	ERR_FAIL_NULL_V_MSG(desc, godot::Ref<ECSComponent>(), "component is not registered");
	ERR_FAIL_COND_V_MSG(!has(entity, component), godot::Ref<ECSComponent>(), "entity does not have this component");
	godot::Ref<ECSComponent> proxy = desc->make_proxy();
	proxy->world = godot::Ref<ECSWorld>(this);
	proxy->entity = to_entity(entity);
	return proxy;
}

godot::Ref<ECSSystem> ECSWorld::system(const godot::Variant &system) {
	const SystemDescriptor *ops = find_system(resolve_class_token(system));
	ERR_FAIL_NULL_V_MSG(ops, godot::Ref<ECSSystem>(), "system is not registered");
	godot::Ref<ECSSystem> handle = ops->make_handle();
	handle->world = godot::Ref<ECSWorld>(this);
	handle->ops = ops;
	return handle;
}

void ECSWorld::_bind_methods() {
	godot::ClassDB::bind_method(godot::D_METHOD("create"), &ECSWorld::create);
	godot::ClassDB::bind_method(godot::D_METHOD("destroy", "entity"), &ECSWorld::destroy);
	godot::ClassDB::bind_method(godot::D_METHOD("is_alive", "entity"), &ECSWorld::is_alive);
	godot::ClassDB::bind_method(godot::D_METHOD("add", "entity", "value"), &ECSWorld::add);
	godot::ClassDB::bind_method(godot::D_METHOD("remove", "entity", "component"), &ECSWorld::remove);
	godot::ClassDB::bind_method(godot::D_METHOD("has", "entity", "component"), &ECSWorld::has);
	godot::ClassDB::bind_method(godot::D_METHOD("component", "entity", "component"), &ECSWorld::component);
	godot::ClassDB::bind_method(godot::D_METHOD("system", "system"), &ECSWorld::system);
}

void register_ecs_types() {
	GDREGISTER_ABSTRACT_CLASS(ECSComponent);
	GDREGISTER_ABSTRACT_CLASS(ECSChunk);
	GDREGISTER_ABSTRACT_CLASS(ECSSystem);
	GDREGISTER_CLASS(ECSChunkIterator);
	GDREGISTER_CLASS(ECSWorld);
}

} // namespace GDNativeSDK::ECS
