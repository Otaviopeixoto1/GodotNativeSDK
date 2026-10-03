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
// Example Usage:
//  NATIVE_SYSTEM(Move, GDNativeSDK::ECS::Read<Velocity>, GDNativeSDK::ECS::Write<CircleGeometry>) {
//      static void body(entt::entity, const Velocity &v, CircleGeometry &c) { c.center += v.value; }
//  };

// Declares a system and its Godot classes <Name> and <Name>Chunk.
// It allows for the definition of body() and filter() functions
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

// What the world needs to know about a system without knowing its type.
struct SystemOps {
	godot::StringName godot_class;
	godot::StringName chunk_class;

	// One slot per Read component, then one per Write component.
	std::vector<entt::id_type> slots;
	std::vector<bool> writable;

	// Get matcing entities
	void (*gather)(entt::registry &, std::vector<entt::entity> &, std::vector<std::vector<void *>> &) = nullptr;

	// Null when the system has no native body.
	std::size_t (*run_native)(entt::registry &) = nullptr;

	godot::Ref<ECSChunk> (*make_chunk)() = nullptr;
	godot::Ref<ECSSystem> (*make_handle)() = nullptr;
};

// System descriptor registration to the persistent storage
void add_system_ops(const SystemOps &ops);
const SystemOps *find_system(const godot::StringName &godot_class);

// Called at during type registration (MODULE_INITIALIZATION_LEVEL_SCENE), after registering components.
template <typename S>
void register_system() {
	using traits = system_traits<S>;
	using Handle = typename traits::handle;
	using Chunk = typename traits::chunk;

	godot::ClassDB::register_class<Handle>();
	godot::ClassDB::register_class<Chunk>();

	SystemOps ops = S::make_ops();
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
struct Read {};
template <typename... T>
struct Write {};
template <typename... T>
struct With {};
template <typename... T>
struct Exclude {};


template <typename Derived, typename R, typename W = Write<>, typename H = With<>, typename X = Exclude<>>
struct System;

template <typename Derived, typename... Rs, typename... Ws, typename... Hs, typename... Xs>
struct System<Derived, Read<Rs...>, Write<Ws...>, With<Hs...>, Exclude<Xs...>> {
	static_assert(sizeof...(Rs) + sizeof...(Ws) > 0, "A system needs at least one Read or Write component");
	static_assert(((!std::is_empty_v<Rs>) && ...), "Read takes components with data, put tags in With<> or Exclude<>");
	static_assert(((!std::is_empty_v<Ws>) && ...), "Write takes components with data, put tags in With<> or Exclude<>");
	static_assert((std::is_empty_v<Hs> && ...), "With takes tag components without data");

	// Read components arrive as const, so a system cannot modify what it only declared as read.
	static auto view(entt::registry &r) {
		if constexpr (sizeof...(Xs) == 0) {
			return r.view<const Rs..., Ws..., Hs...>();
		} else {
			return r.view<const Rs..., Ws..., Hs...>(entt::exclude<Xs...>);
		}
	}

	//
	// TODO: Filter might not be necessary. We could do the same with tag components...
	//

	static constexpr bool filter_declared = requires { &Derived::filter; };
	static constexpr bool body_declared = requires { &Derived::body; };
	static constexpr bool has_filter = requires(const Rs &...rs, const Ws &...ws) { Derived::filter(rs..., ws...); };
	static constexpr bool has_body = requires(entt::entity e, const Rs &...rs, Ws &...ws) { Derived::body(e, rs..., ws...); };


	//
	// Iteration methods
	// 

	// The one loop every runner shares. Tags in With<> are not passed on, EnTT leaves them out.
	template <typename Fn>
	static std::size_t each(entt::registry &r, Fn &&fn) {
		static_assert(!filter_declared || has_filter, "filter must be: static bool filter(const Read &..., const Write &...)");
		std::size_t n = 0;
		for (auto &&tup : view(r).each()) {
			std::apply([&](entt::entity e, auto &...c) {
				if constexpr (has_filter) {
					if (!Derived::filter(c...)) {
						return;
					}
				}
				fn(e, c...);
				++n;
			},
			tup);
		}
		return n;
	}

	static void gather(entt::registry &r, std::vector<entt::entity> &entities, std::vector<std::vector<void *>> &slots) {
		entities.clear();
		slots.assign(sizeof...(Rs) + sizeof...(Ws), {});
		each(r, [&](entt::entity e, auto &...c) {
			entities.push_back(e);
			std::size_t i = 0;
			(slots[i++].push_back(const_cast<void *>(static_cast<const void *>(&c))), ...);
		});
	}

	static std::size_t run_native(entt::registry &r) {
		return each(r, [](entt::entity e, auto &...c) { Derived::body(e, c...); });
	}

	static SystemOps make_ops() {
		static_assert(!body_declared || has_body, "body must be: static void body(entt::entity, const Read &..., Write &...)");
		SystemOps ops;
		((ops.slots.push_back(entt::type_hash<Rs>::value()), ops.writable.push_back(false)), ...);
		((ops.slots.push_back(entt::type_hash<Ws>::value()), ops.writable.push_back(true)), ...);
		ops.gather = &gather;
		if constexpr (has_body) {
			ops.run_native = &run_native;
		}
		return ops;
	}
};


// A slice of a system entities. Generated subclasses add one typed packed array property per
// field of each Read and Write component, named <component>_<field>.
class ECSChunk : public godot::RefCounted {
	GDCLASS(ECSChunk, godot::RefCounted)

public:
	godot::Ref<ECSWorld> world;
	const SystemOps *ops = nullptr;
	uint64_t version = 0;
	const std::vector<entt::entity> *entities = nullptr;
	const std::vector<std::vector<void *>> *slots = nullptr;
	std::size_t start = 0;
	std::size_t count = 0;

	int64_t size() const { return static_cast<int64_t>(count); }
	godot::PackedInt64Array get_entities() const;
	void *const *column(std::size_t slot, bool write);

protected:
	static void _bind_methods();
};

// Iterator chunks in GDScript
class ECSChunkIterator : public godot::RefCounted {
	GDCLASS(ECSChunkIterator, godot::RefCounted)

public:
	godot::Ref<ECSWorld> world;
	const SystemOps *ops = nullptr;
	std::size_t chunk_size = 1024;
	std::vector<entt::entity> entities;
	std::vector<std::vector<void *>> slots;
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

// A system bound to a world. Generated subclasses only give it its class name.
class ECSSystem : public godot::RefCounted {
	GDCLASS(ECSSystem, godot::RefCounted)

public:
	godot::Ref<ECSWorld> world;
	const SystemOps *ops = nullptr;

	int64_t run_native();
	godot::Ref<ECSChunkIterator> chunks(int64_t chunk_size);
	int64_t each_chunk(const godot::Callable &callback, int64_t chunk_size);
	int64_t count();

protected:
	static void _bind_methods();
};

} // namespace GDNativeSDK::ECS

