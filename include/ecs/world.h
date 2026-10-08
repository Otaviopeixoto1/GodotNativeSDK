#pragma once

#include "ecs/meta_bind.h"

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/core/class_db.hpp>

#include <entt/entt.hpp>

#include <cstdint>
#include <type_traits>
#include <utility>

//
// Macros
//
// GDN_COMPONENT defines a default-constructable proxy Component class based on an existing struct.
// The proxy class is used to bind to GDScript and provide basic accessor logic to its *Variant-compatible*
// fields. A component_traits struct is also defined for the give struct in order to link the runtime reflection
// code (entt::meta).
// 
// Example Usage:
//		struct CircleGeometry {
//			Vector2 center;
//			float radius;
//		};
//		GDN_COMPONENT(CircleGeometry, GDN_FIELD(center) GDN_FIELD(radius))



// Declares the proxy class used for binding an existing struct component to GDScript.
// IMPORTANT: The struct must be declared at global scope.
#define GDN_COMPONENT(m_type, m_fields)                                 \
	namespace GDNativeSDK::ECSProxies {                                 \
	class m_type : public ::GDNativeSDK::ECS::ECSComponent {            \
		GDCLASS(m_type, ::GDNativeSDK::ECS::ECSComponent)               \
                                                                        \
	public:                                                             \
		m_type() {                                                      \
			own = ::entt::any{ std::in_place_type<::m_type> };          \
			component = ::entt::type_hash<::m_type>::value();           \
		}                                                               \
                                                                        \
	protected:                                                          \
		static void _bind_methods() {}                                  \
	};                                                                  \
	}                                                                   \
	namespace GDNativeSDK::ECS {                                        \
	template <>                                                         \
	struct component_traits<::m_type> {                                 \
		using proxy = ::GDNativeSDK::ECSProxies::m_type;                \
		static void reflect() {                                         \
			using Self = ::m_type;                                      \
			::GDNativeSDK::ECS::reflect<::m_type>(#m_type) m_fields;    \
		}                                                               \
	};                                                                  \
	}

// Use inside GDN_COMPONENT to declare an field from the struct component
#define GDN_FIELD(m_name) .field<&Self::m_name>(#m_name)



namespace GDNativeSDK::ECS {

//
// Templates
//

template <typename T>
struct component_traits;

class ECSComponent;

// A Type-Erased Component that gets persistently stored
struct ComponentDesc {
	entt::id_type id = 0;
	godot::StringName godot_class;
	entt::meta_type meta;

	// Function called when copying a component's content
	void (*emplace_copy)(entt::registry &, entt::entity, const void *src) = nullptr;

	//Function called for initializing entt storage for the component. This guarantees that storages are never empty
	void (*touch)(entt::registry &) = nullptr;

	//Function called for creating an instance of the proxy class (GDScript side representation of the component)
	godot::Ref<ECSComponent> (*make_proxy)() = nullptr;
};

// Component descriptor registration and access to the persistent storage
void add_component_desc(const ComponentDesc &desc);
const ComponentDesc *find_component(entt::id_type id);
const ComponentDesc *find_component(const godot::StringName &godot_class);

// What GDScript passes to name a component or system: the class itself, an instance, or a name.
godot::StringName resolve_class_token(const godot::Variant &token);

// Called at during type registration (MODULE_INITIALIZATION_LEVEL_SCENE), before regustering systems that use the component.
template <typename T>
void register_component() {
	using traits = component_traits<T>;
	using Proxy = typename traits::proxy;

	// Register Components to entt::meta (see the GDN_COMPONENT macro above)
	traits::reflect();
	godot::ClassDB::register_class<Proxy>();

	ComponentDesc desc;
	desc.id = entt::type_hash<T>::value();
	desc.godot_class = Proxy::get_class_static();
	desc.meta = entt::resolve<T>();
	desc.emplace_copy = [](entt::registry &r, entt::entity e, const void *src) {
		if constexpr (std::is_empty_v<T>) {
			r.emplace_or_replace<T>(e);
		} else {
			r.emplace_or_replace<T>(e, *static_cast<const T *>(src));
		}
	};
	desc.touch = [](entt::registry &r) { (void)r.storage<T>(); };
	desc.make_proxy = []() -> godot::Ref<ECSComponent> {
		godot::Ref<Proxy> p;
		p.instantiate();
		return p;
	};
	bind_value_properties(desc.godot_class, desc.meta);
	add_component_desc(desc);
}


class ECSWorld;
class ECSSystem;

// Base of every generated proxy Component class. Detached from world it owns a value, attached it resolves the
// component in the registry on every access. It never holds a pointer that can go stale.
class ECSComponent : public godot::RefCounted {
	GDCLASS(ECSComponent, godot::RefCounted)

public:
	// The internal instance of a real component struct as an entt::any object.
	// This value is valid if this ECSComponent is not bound to any ECSWorld, otherwise
	// The actual valid component is the one inside the ECSWorld's entt::registry
	entt::any own;
	// Type Id of the real component struct
	entt::id_type component = 0;

	godot::Ref<ECSWorld> world;
	entt::entity entity = entt::null;

	void *data();
	bool is_attached() const { return world.is_valid(); }
	int64_t get_entity() const;

protected:
	static void _bind_methods();
};


//
// A class used to bridge the ECS implementation between the native and the GDScript code.
//
class ECSWorld : public godot::RefCounted {
	GDCLASS(ECSWorld, godot::RefCounted)

public:
	entt::registry registry;
	// Bumped by every change that can move components in memory. Chunks check it before use.
	uint64_t structure_version = 0;

	ECSWorld();

	// Creates an entity managed by this ECSWorld
	int64_t create();
	void destroy(int64_t entity);
	bool is_alive(int64_t entity) const;

	//
	// Component accessors. These provide an API for the GDSCript side
	// GDScript usage: var live := world.component(e, CircleGeometry) as CircleGeometry
	//
	// Example Usage:
	//		var c := CircleGeometry.new()
	//		c.radius = 2.0 world.add(e, c)
	//

	void add(int64_t entity, const godot::Ref<ECSComponent> &value);
	void remove(int64_t entity, const godot::Variant &component);
	bool has(int64_t entity, const godot::Variant &component);

	// GDScript API for retrieving a component associated with an entity through a class token. In this case it can be a live Component instance, a Component class, instance or string name.
	// GDScript usage: var live := world.component(e, CircleGeometry) as CircleGeometry
	godot::Ref<ECSComponent> component(int64_t entity, const godot::Variant &component);

	// GDScript API for retrieving a system through a class token. In this case it can be a live System instance, a system class, instance or string name.
	// GDScript usage: var move := world.system(MoveSystem) as MoveSystem
	godot::Ref<ECSSystem> system(const godot::Variant &system);

	static entt::entity to_entity(int64_t e) { return entt::entity(static_cast<uint32_t>(e)); }
	static int64_t from_entity(entt::entity e) { return static_cast<int64_t>(entt::to_integral(e)); }

protected:
	static void _bind_methods();
};

} // namespace GDNativeSDK::ECS
