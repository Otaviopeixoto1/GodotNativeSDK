#pragma once

#include "ecs/world.h"

#include <godot_cpp/variant/callable.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>

#include <cstddef>
#include <tuple>
#include <utility>
#include <vector>

//
// Macros
// 
// NATIVE_SYSTEM defines a struct with static methods. A body(entt::entity, ...) method is required and
// contains the main native-side logic of the system.
//
// Each System also defines a <System_Name>Chunk class. Chunks give access to (packed) component fieled arrays
// for all components inside the system. These packed arrays are refered as columns (see meta_bind.h). Accessing them
// is done through a call to <System_Name>.chunks(). Each chunk then will have one property per component field.
// When accessed a copy will be made from the native components into the PackedArrays.
// 
// Example Usage:
//		NATIVE_SYSTEM(MoveSystem, GDNativeSDK::ECS::Read<Velocity>, GDNativeSDK::ECS::Write<CircleGeometry>) {
//			static void body(entt::entity, const Velocity &v, CircleGeometry &c) { c.center += v.value; }
//		};
//
//		// GDScript:
//
//		# Iterating through chunks:
//		var move := world.system(MoveSystem) as MoveSystem
//		for chunk: MoveSystemChunk in move.chunks(1024):   # annotate the loop variable
//			# Each access is a copy so do it once and iterate over the result
//			var centers := chunk.circle_geometry_center  # center column PackedVector2Array
//			var velocities := chunk.velocity_value		 # velocity column PackedVector2Array
//			for i in centers.size():
//				centers[i] += velocities[i]
//
//			# Also avoid directly assigning individual values, each assignement is also a copy
//			# Always assign the whole column!
//			chunk.circle_geometry_center = centers		
//

// Declares a system and its Godot classes <Name> and <Name>Chunk.
// It allows for the definition of a body() function for processing entities
#define NATIVE_SYSTEM(m_name, ...)                                    \
	struct m_name;                                                    \
	namespace GDNativeSDK::ECSProxies {                               \
	class m_name : public ::GDNativeSDK::ECS::ECSSystem {             \
		GDCLASS(m_name, ::GDNativeSDK::ECS::ECSSystem)                \
                                                                      \
	protected:                                                        \
		static void _bind_methods() {}                                \
	};                                                                \
	class m_name##Chunk : public ::GDNativeSDK::ECS::ECSChunk {       \
		GDCLASS(m_name##Chunk, ::GDNativeSDK::ECS::ECSChunk)          \
                                                                      \
	protected:                                                        \
		static void _bind_methods() {}                                \
	};                                                                \
	}                                                                 \
	namespace GDNativeSDK::ECS {                                      \
	template <>                                                       \
	struct system_traits<::m_name> {                                  \
		using handle = ::GDNativeSDK::ECSProxies::m_name;             \
		using chunk = ::GDNativeSDK::ECSProxies::m_name##Chunk;       \
	};                                                                \
	}                                                                 \
	struct m_name : ::GDNativeSDK::ECS::System<::m_name, __VA_ARGS__>



namespace GDNativeSDK::ECS {
	
//
// Templates
//

template <typename S>
struct system_traits;

class ECSChunk;
class ECSSystem;

// A Type-Erased System that gets persistently stored
struct SystemDescriptor {
	godot::StringName godot_class;
	godot::StringName chunk_class;

	// One slot per Read component, then one per Write component.
	std::vector<entt::id_type> slots;
	std::vector<bool> writable;

	// Gather matching entities and their components within the the input arrays
	void (*gather)(entt::registry &, std::vector<entt::entity> &, std::vector<std::vector<void *>> &) = nullptr;

	// Null when the system has no native body.
	std::size_t (*run_native)(entt::registry &) = nullptr;

	//
	// Chunked data accessors
	//
	godot::Ref<ECSChunk> (*make_chunk)() = nullptr;
	godot::Ref<ECSSystem> (*make_handle)() = nullptr;
};

// System descriptor registration to the persistent storage
void add_system_ops(const SystemDescriptor &ops);
const SystemDescriptor *find_system(const godot::StringName &godot_class);

// Called at during type registration (MODULE_INITIALIZATION_LEVEL_SCENE), after registering components.
// This will also register the respective Chunk class associated with the system and its column member accessors for the GDScript API
// (i.e. all <SystemName>Chunk.<component_name>_field are registered here as well)
template <typename S>
void register_system() {
	using traits = system_traits<S>;
	using Handle = typename traits::handle;
	using Chunk = typename traits::chunk;

	godot::ClassDB::register_class<Handle>();
	godot::ClassDB::register_class<Chunk>();

	SystemDescriptor ops = S::make_ops();
	ops.godot_class = Handle::get_class_static();
	ops.chunk_class = Chunk::get_class_static();
	ops.make_chunk = []() -> godot::Ref<ECSChunk> {
		godot::Ref<Chunk> c;
		c.instantiate();
		return c;
	};
	ops.make_handle = []() -> godot::Ref<ECSSystem> {
		godot::Ref<Handle> h;
		h.instantiate();
		return h;
	};

	//
	// For each Component in this system, bind Component fields as Column properties of a Chunk class 
	// Each Column takes the form of an array (in some cases PackedArray)
	//
	for (std::size_t slot = 0; slot < ops.slots.size(); slot++) {
		const ComponentDesc *desc = find_component(ops.slots[slot]);
		ERR_CONTINUE_MSG(desc == nullptr, godot::String("Register every component before the system ") + ops.godot_class);
		bind_column_properties(ops.chunk_class, desc->meta, snake_case(desc->meta.name()), slot, ops.writable[slot]);
	}
	add_system_ops(ops);
}

//
// System Operations
//

template <typename... T>
struct Read {}; // Declares Read access
template <typename... T>
struct Write {}; // Declares Write access
template <typename... T>
struct With {}; // Used for requiring declaring the presence of a component without storage (tag)
template <typename... T>
struct Exclude {}; // Used for excluding a component


template <typename Derived, typename R, typename W = Write<>, typename H = With<>, typename X = Exclude<>>
struct System;

template <typename Derived, typename... Rs, typename... Ws, typename... Hs, typename... Xs>
struct System<Derived, Read<Rs...>, Write<Ws...>, With<Hs...>, Exclude<Xs...>> {
	static_assert(sizeof...(Rs) + sizeof...(Ws) > 0, "A system needs at least one Read or Write component");
	static_assert(((!std::is_empty_v<Rs>) && ...), "Read takes components with data, put tags in With<> or Exclude<>");
	static_assert(((!std::is_empty_v<Ws>) && ...), "Write takes components with data, put tags in With<> or Exclude<>");
	static_assert((std::is_empty_v<Hs> && ...), "With takes tag components without data");

	// Returns the current view for the current system
	static auto view(entt::registry &r) {
		if constexpr (sizeof...(Xs) == 0) {
			return r.view<const Rs..., Ws..., Hs...>();
		} else {
			return r.view<const Rs..., Ws..., Hs...>(entt::exclude<Xs...>);
		}
	}

	static constexpr bool body_declared = requires { &Derived::body; };
	static constexpr bool has_body = requires(entt::entity e, const Rs &...rs, Ws &...ws) { Derived::body(e, rs..., ws...); };


	//
	// Iteration methods
	// 

	// The main loop shared by all system looping functions.
	// Returns the number of elements looped over
	template <typename Fn>
	static std::size_t each(entt::registry &r, Fn &&fn) {
		std::size_t n = 0;
		for (auto &&tup : view(r).each()) {
			//
			// std::apply(): view(r).each() yields one tuple per entity: the entity, then one reference per component, such as (entity, const Velocity&, CircleGeometry&).
			// In normal code you unpack it with structured bindings, for (auto [e, v, c] : view.each()). But the template is generic: it might see two components or five.
			// In C++20, structured bindings can't unpack into a variable number of names. A form like auto [e, ...c] only arrives in C++26.
			//
			std::apply([&](entt::entity e, auto &...c) {
				fn(e, c...);
				++n;
			},
			tup);
		}
		return n;
	}

	// Gather all entities and components and fills the given input vectors
	static void gather(entt::registry &r, std::vector<entt::entity> &entities, std::vector<std::vector<void *>> &components) {
		//
		// TODO: This gather operation can be optimized. Components are meant to be (almost ?) contiguous in memory...
		//  Just get the starting pointer. Entity iteration order should be respected (???)
		//
		// --------------------> type-erase views instead to iterate the entities inside the column getters
		//
		entities.clear();
		components.assign(sizeof...(Rs) + sizeof...(Ws), {});
		each(r, [&](entt::entity e, auto &...c) {
			entities.push_back(e);
			std::size_t i = 0;
			(components[i++].push_back(const_cast<void *>(static_cast<const void *>(&c))), ...);
		});
	}

	static std::size_t run_native(entt::registry &r) {
		return each(r, [](entt::entity e, auto &...c) { Derived::body(e, c...); });
	}

	static SystemDescriptor make_ops() {
		static_assert(!body_declared || has_body, "body must be: static void body(entt::entity, const Read &..., Write &...)");
		SystemDescriptor ops;
		((ops.slots.push_back(entt::type_hash<Rs>::value()), ops.writable.push_back(false)), ...);
		((ops.slots.push_back(entt::type_hash<Ws>::value()), ops.writable.push_back(true)), ...);
		ops.gather = &gather;
		if constexpr (has_body) {
			ops.run_native = &run_native;
		}
		return ops;
	}
};


// A slice of a system entities. Generated System subclasses add one typed packed array 
// property per field of each Read and Write component, named <component>_<field>.
class ECSChunk : public godot::RefCounted {
	GDCLASS(ECSChunk, godot::RefCounted)

	//
	// TODO: FIX POTENTIAL LEAKS:
	// Chunks can point at freed memory. ECSChunk::entities and ECSChunk::components are raw pointers into vectors owned by ECSChunkIterator. If GDScript keeps a chunk after
	// its iterator is freed, the next column access reads freed memory and can crash. That happens if a script stores a chunk from a for loop in a variable, or keeps the chunk passed
	// to an each_chunk callback. The structure_version check doesn't catch this, because the world itself hasn't changed. Two possible fixes:
	// -Have the chunk hold a Ref to its iterator.
	// -Have the iterator clear chunk->components and chunk->entities in its destructor.
	//

public:
	godot::Ref<ECSWorld> world;
	const SystemDescriptor *ops = nullptr;
	uint64_t version = 0;
	const std::vector<entt::entity> *entities = nullptr;
	const std::vector<std::vector<void *>> *components = nullptr;
	std::size_t start = 0;
	std::size_t count = 0;

	int64_t size() const { return static_cast<int64_t>(count); }
	godot::PackedInt64Array get_entities() const;
	void *const *component_ptrs(std::size_t slot, bool write);

protected:
	static void _bind_methods();
};

// Iterator chunks in GDScript
class ECSChunkIterator : public godot::RefCounted {
	GDCLASS(ECSChunkIterator, godot::RefCounted)

public:
	godot::Ref<ECSWorld> world;
	const SystemDescriptor *ops = nullptr;
	std::size_t chunk_size = 1024;
	std::vector<entt::entity> entities;
	std::vector<std::vector<void *>> components;
	std::size_t start = 0;
	godot::Ref<ECSChunk> chunk;

	bool _iter_init(const godot::Array &iter);
	bool _iter_next(const godot::Array &iter);
	godot::Variant _iter_get(const godot::Variant &iter);
	bool begin();
	bool next();

protected:
	static void _bind_methods();
};



// GDScript-side API for accessing a system bound to a world.
// This class is constructed from the type-erased systems (SystemDescriptor) But it is meant to
// be inherited by the Proxy System classes defined in the NATIVE_SYSTEM macro to give a type-safe API 
class ECSSystem : public godot::RefCounted {
	GDCLASS(ECSSystem, godot::RefCounted)

public:
	godot::Ref<ECSWorld> world;
	const SystemDescriptor *ops = nullptr;

	int64_t run_native();

	// Iterates through entities and stores their ids and component references into Chunks of the specified size
	// Each Typed Chunk will have the accessors to their columns
	godot::Ref<ECSChunkIterator> chunks(int64_t chunk_size);
	int64_t each_chunk(const godot::Callable &callback, int64_t chunk_size);
	int64_t count();

protected:
	static void _bind_methods();
};

} // namespace GDNativeSDK::ECS

