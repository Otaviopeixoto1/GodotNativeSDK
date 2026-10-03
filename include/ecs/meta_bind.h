#pragma once

//
// Reflection for ECS components: entt::meta holds the names, and every reflected field carries typed
// functions created from its member pointer. Those functions are registered as Godot methods and
// properties at init, so GDScript sees real typed members without hand written getters.
//
// Components and Systems are registered through macros (see world.h and system.h)
//

#include <godot_cpp/core/method_ptrcall.hpp>
#include <godot_cpp/core/type_info.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/color.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/packed_color_array.hpp>
#include <godot_cpp/variant/packed_float32_array.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>
#include <godot_cpp/variant/packed_int64_array.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/packed_vector2_array.hpp>
#include <godot_cpp/variant/packed_vector3_array.hpp>
#include <godot_cpp/variant/packed_vector4_array.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/string_name.hpp>
#include <godot_cpp/variant/variant.hpp>
#include <godot_cpp/variant/vector2.hpp>
#include <godot_cpp/variant/vector3.hpp>

#include <entt/entt.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace GDNativeSDK::ECS {

// The packed arrays for fiels when a whole column is copied out.
template <typename F>
struct Column {
	using type = godot::Array;
	static constexpr bool packed = false;
};
template <>
struct Column<bool> {
	using type = godot::PackedByteArray;
	static constexpr bool packed = false;
};
template <>
struct Column<uint8_t> {
	using type = godot::PackedByteArray;
	static constexpr bool packed = true;
};
template <>
struct Column<int32_t> {
	using type = godot::PackedInt32Array;
	static constexpr bool packed = true;
};
template <>
struct Column<int64_t> {
	using type = godot::PackedInt64Array;
	static constexpr bool packed = true;
};
template <>
struct Column<float> {
	using type = godot::PackedFloat32Array;
	static constexpr bool packed = true;
};
template <>
struct Column<double> {
	using type = godot::PackedFloat64Array;
	static constexpr bool packed = true;
};
template <>
struct Column<godot::String> {
	using type = godot::PackedStringArray;
	static constexpr bool packed = true;
};
template <>
struct Column<godot::Vector2> {
	using type = godot::PackedVector2Array;
	static constexpr bool packed = true;
};
template <>
struct Column<godot::Vector3> {
	using type = godot::PackedVector3Array;
	static constexpr bool packed = true;
};
template <>
struct Column<godot::Vector4> {
	using type = godot::PackedVector4Array;
	static constexpr bool packed = true;
};
template <>
struct Column<godot::Color> {
	using type = godot::PackedColorArray;
	static constexpr bool packed = true;
};

//
// GDExtension helpers
//

// Retuns Component pointer behind a component proxy instance, nullptr when it cannot be resolved.
void *proxy_data(GDExtensionClassInstancePtr instance);

// Returns Component pointer of one column of a chunk instance, nullptr when the chunk is stale or the
// column is read only and a write was requested. Defined in system.cpp
void *const *chunk_column(GDExtensionClassInstancePtr instance, void *userdata, std::size_t &count, bool write);


template <typename>
struct member_traits;
template <typename C, typename F>
struct member_traits<F C::*> {
	using klass = C;
	using field = F;
};

// Everything Godot needs to expose one field, attached to the field metadata.
struct FieldOps {
	GDExtensionVariantType value_type;
	GDExtensionClassMethodArgumentMetadata value_meta;
	GDExtensionVariantType column_type;
	GDExtensionClassMethodCall value_get_call;
	GDExtensionClassMethodCall value_set_call;
	GDExtensionClassMethodCall column_get_call;
	GDExtensionClassMethodCall column_set_call;
	GDExtensionClassMethodPtrCall value_get_ptr;
	GDExtensionClassMethodPtrCall value_set_ptr;
	GDExtensionClassMethodPtrCall column_get_ptr;
	GDExtensionClassMethodPtrCall column_set_ptr;
};

template <auto Member>
struct FieldThunks {
	using C = typename member_traits<decltype(Member)>::klass;
	using F = typename member_traits<decltype(Member)>::field;
	using Col = typename Column<F>::type;

	static void ok(GDExtensionCallError *err) { err->error = GDEXTENSION_CALL_OK; }
	static void copy_out(const godot::Variant &v, GDExtensionVariantPtr r_ret) { godot::internal::gdextension_interface_variant_new_copy(r_ret, v._native_ptr()); }
	static C *comp(GDExtensionClassInstancePtr inst) { return static_cast<C *>(proxy_data(inst)); }

	// Single value on a component proxy.
	static void value_get_call(void *, GDExtensionClassInstancePtr inst, const GDExtensionConstVariantPtr *, GDExtensionInt, GDExtensionVariantPtr r_ret, GDExtensionCallError *err) {
		C *c = comp(inst);
		copy_out(c ? godot::Variant(c->*Member) : godot::Variant(), r_ret);
		ok(err);
	}
	static void value_set_call(void *, GDExtensionClassInstancePtr inst, const GDExtensionConstVariantPtr *args, GDExtensionInt argc, GDExtensionVariantPtr, GDExtensionCallError *err) {
		if (argc < 1) {
			err->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
			err->expected = 1;
			return;
		}
		if (C *c = comp(inst)) {
			c->*Member = static_cast<F>(*reinterpret_cast<const godot::Variant *>(args[0]));
		}
		ok(err);
	}
	static void value_get_ptr(void *, GDExtensionClassInstancePtr inst, const GDExtensionConstTypePtr *, GDExtensionTypePtr r_ret) {
		C *c = comp(inst);
		godot::PtrToArg<F>::encode(c ? c->*Member : F{}, r_ret);
	}
	static void value_set_ptr(void *, GDExtensionClassInstancePtr inst, const GDExtensionConstTypePtr *args, GDExtensionTypePtr) {
		if (C *c = comp(inst)) {
			c->*Member = godot::PtrToArg<F>::convert(args[0]);
		}
	}

	static Col pack(void *const *p, std::size_t n) {
		Col out;
		out.resize(static_cast<int64_t>(n));
		if constexpr (std::is_same_v<F, bool>) {
			uint8_t *w = out.ptrw();
			for (std::size_t i = 0; i < n; i++)
				w[i] = (static_cast<const C *>(p[i])->*Member) ? 1 : 0;
		} else if constexpr (Column<F>::packed) {
			auto *w = out.ptrw();
			for (std::size_t i = 0; i < n; i++)
				w[i] = static_cast<const C *>(p[i])->*Member;
		} else {
			for (std::size_t i = 0; i < n; i++)
				out[static_cast<int64_t>(i)] = godot::Variant(static_cast<const C *>(p[i])->*Member);
		}
		return out;
	}
	static void unpack(void *const *p, std::size_t n, const Col &in) {
		std::size_t m = std::min(n, static_cast<std::size_t>(in.size()));
		if constexpr (std::is_same_v<F, bool>) {
			const uint8_t *r = in.ptr();
			for (std::size_t i = 0; i < m; i++)
				static_cast<C *>(p[i])->*Member = r[i] != 0;
		} else if constexpr (Column<F>::packed) {
			const auto *r = in.ptr();
			for (std::size_t i = 0; i < m; i++)
				static_cast<C *>(p[i])->*Member = r[i];
		} else {
			for (std::size_t i = 0; i < m; i++)
				static_cast<C *>(p[i])->*Member = static_cast<F>(in[static_cast<int64_t>(i)]);
		}
	}
	static void column_get_call(void *ud, GDExtensionClassInstancePtr inst, const GDExtensionConstVariantPtr *, GDExtensionInt, GDExtensionVariantPtr r_ret, GDExtensionCallError *err) {
		std::size_t n = 0;
		void *const *p = chunk_column(inst, ud, n, false);
		copy_out(godot::Variant(p ? pack(p, n) : Col()), r_ret);
		ok(err);
	}
	static void column_set_call(void *ud, GDExtensionClassInstancePtr inst, const GDExtensionConstVariantPtr *args, GDExtensionInt argc, GDExtensionVariantPtr, GDExtensionCallError *err) {
		if (argc < 1) {
			err->error = GDEXTENSION_CALL_ERROR_TOO_FEW_ARGUMENTS;
			err->expected = 1;
			return;
		}
		std::size_t n = 0;
		if (void *const *p = chunk_column(inst, ud, n, true)) {
			unpack(p, n, static_cast<Col>(*reinterpret_cast<const godot::Variant *>(args[0])));
		}
		ok(err);
	}
	static void column_get_ptr(void *ud, GDExtensionClassInstancePtr inst, const GDExtensionConstTypePtr *, GDExtensionTypePtr r_ret) {
		std::size_t n = 0;
		void *const *p = chunk_column(inst, ud, n, false);
		godot::PtrToArg<Col>::encode(p ? pack(p, n) : Col(), r_ret);
	}
	static void column_set_ptr(void *ud, GDExtensionClassInstancePtr inst, const GDExtensionConstTypePtr *args, GDExtensionTypePtr) {
		std::size_t n = 0;
		if (void *const *p = chunk_column(inst, ud, n, true)) {
			unpack(p, n, godot::PtrToArg<Col>::convert(args[0]));
		}
	}

	static FieldOps make() {
		return {
			static_cast<GDExtensionVariantType>(godot::GetTypeInfo<F>::VARIANT_TYPE),
			godot::GetTypeInfo<F>::METADATA,
			static_cast<GDExtensionVariantType>(godot::GetTypeInfo<Col>::VARIANT_TYPE),
			&value_get_call,
			&value_set_call,
			&column_get_call,
			&column_set_call,
			&value_get_ptr,
			&value_set_ptr,
			&column_get_ptr,
			&column_set_ptr,
		};
	}
};

template <typename T>
struct reflect {
	entt::meta_factory<T> factory{};

	explicit reflect(const char *name) { factory.type(name); }

	template <auto Member>
	reflect &field(const char *name) {
		static_assert(std::is_same_v<typename member_traits<decltype(Member)>::klass, T>, "field belongs to another type");
		// Add the FieldOps to entt::meta_factory as "custom" data:
		factory.template data<Member>(name).template custom<FieldOps>(FieldThunks<Member>::make());
		return *this;
	}
};

godot::String snake_case(std::string_view name);

// One typed property per reflected field, on a component proxy class.
void bind_value_properties(const godot::StringName &godot_class, const entt::meta_type &type);

// One typed packed array property per reflected field, on a chunk class, named <prefix>_<field>.
void bind_column_properties(const godot::StringName &chunk_class, const entt::meta_type &type, const godot::String &prefix, std::size_t slot, bool writable);



} // namespace GDNativeSDK::ECS
