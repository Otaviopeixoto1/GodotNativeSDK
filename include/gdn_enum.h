#pragma once
#include <entt/entt.hpp>

#include <optional>
#include <string_view>
#include <type_traits>

//
// Macros
//
// GDN_ENUM: reflects the values of an enum with entt::meta:
//
//     enum class Stage : tick::StageId { Input, Decide, Late };
//     GDN_ENUM(Stage, GDN_ENUM_VALUE(Input) GDN_ENUM_VALUE(Decide) GDN_ENUM_VALUE(Late))
//
// This is entt's own way of reflecting enums ("Named constants and enums" in entt's meta.md): every
// listed value becomes a constant meta data member of the enum's meta type. Its identifier is the
// hashed enumerator name and its name is the enumerator name (a string literal, never copied).
// entt::meta is the only storage: there is no second table.
//
//     GDNativeSDK::enum_value_name(Stage::Decide)            // "Decide"
//     GDNativeSDK::enum_value_from_name<Stage>("Decide")     // Stage::Decide (std::optional)
//     GDNativeSDK::enum_type_name<Stage>()                   // "Stage" (the name given to GDN_ENUM)
//     GDNativeSDK::enum_meta<Stage>().data("Decide"_hs)      // plain entt access
//
// Name lookups to a specific value use entt lookup by identifier (meta_type::data(id)).
// Value lookups to a name walks the enum values; entt has no lookup by value.
//
// Registration happens the first time any of the functions below is used for that enum, and again if
// entt::meta_reset() removed it, so it does not depend on static initialization order. Like all
// entt::meta registration it is not thread-safe !
//
//

// Reflects an enum: GDN_ENUM(game::Stage, GDN_ENUM_VALUE(Input) GDN_ENUM_VALUE(Decide) ...).
// Use in global scope, right after the enum. List every value.
#define GDN_ENUM(m_enum, m_values)                                    \
	namespace GDNativeSDK {                                           \
	template <>                                                       \
	struct enum_traits<::m_enum> {                                    \
		static void reflect() {                                       \
			using Enum = ::m_enum;                                    \
			::GDNativeSDK::EnumReflector<::m_enum>(#m_enum) m_values; \
		}                                                             \
	};                                                                \
	}

// One value inside GDN_ENUM
#define GDN_ENUM_VALUE(m_value) .value<Enum::m_value>(#m_value)



namespace GDNativeSDK {

// Specialized by GDN_ENUM macro. Not defined for other enums.
template <typename E>
struct enum_traits;

// An enum reflected with GDN_ENUM
template <typename E>
concept ReflectedEnum = std::is_enum_v<E> && requires { enum_traits<E>::reflect(); };

// Registers an enum and its values in entt::meta. Used by GDN_ENUM.
template <typename E>
class EnumReflector {
public:
	explicit EnumReflector(const char *enum_name);

	// Same as entt's data<my_enum::a_value>("a_value"): identifier = hashed name, name = the literal.
	template <E Value>
	EnumReflector &value(const char *name);

private:
	entt::meta_factory<E> factory_{};
};

template <ReflectedEnum E>
entt::meta_type enum_meta();

// The name given to GDN_ENUM ("game::Stage").
template <ReflectedEnum E>
std::string_view enum_type_name();

// The enum "field" name, empty when the value is not listed in GDN_ENUM.
// The view points to a string literal, so it can be stored.
template <ReflectedEnum E>
std::string_view enum_value_name(E value);

// The value with that enumerator name, nullopt when there is none.
template <ReflectedEnum E>
std::optional<E> enum_value_from_name(std::string_view name);



//
// Template definitions
//

template <typename E>
EnumReflector<E>::EnumReflector(const char *enum_name) {
	factory_.type(enum_name);
}

template <typename E>
template <E Value>
EnumReflector<E> &EnumReflector<E>::value(const char *name) {
	factory_.template data<Value>(name);
	return *this;
}

template <ReflectedEnum E>
entt::meta_type enum_meta() {
	// Check if enum was registered to entt::meta before:
	if (entt::resolve<E>().name().empty()) {
		// Register enum for the first time
		enum_traits<E>::reflect();
	}
	return entt::resolve<E>();
}

template <ReflectedEnum E>
std::string_view enum_type_name() {
	return enum_meta<E>().name();
}

template <ReflectedEnum E>
std::string_view enum_value_name(E value) {
	for (auto [id, data] : enum_meta<E>().data()) {
		// get({}) reads the constant; small values never allocate (meta_any small buffer).
		const entt::meta_any any = data.get({});
		if (const E *v = any.template try_cast<const E>(); v != nullptr && *v == value) {
			return data.name();
		}
	}
	return {};
}

template <ReflectedEnum E>
std::optional<E> enum_value_from_name(std::string_view name) {
	const entt::meta_data data = enum_meta<E>().data(entt::hashed_string::value(name.data(), name.size()), false);

	// Name check to rules out a different enums whose name hashes to the same identifier.
	if (!data || data.name() != name) {
		return std::nullopt;
	}
	const entt::meta_any any = data.get({});
	const E *v = any.template try_cast<const E>();
	return v != nullptr ? std::optional<E>(*v) : std::nullopt;
}

} // namespace GDNativeSDK
