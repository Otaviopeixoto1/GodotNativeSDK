#include "ecs/ecs.h"
#include "ecs/meta_bind.h"

#include <godot_cpp/core/property_info.hpp>

#include <deque>

namespace GDNativeSDK::ECS {

namespace {
// The C API takes pointers to names. We must keep every string alive for the lifetime of the library...
std::deque<godot::StringName> names;
std::deque<godot::String> strings;

godot::StringName &keep(const godot::String &s) {
	names.push_back(godot::StringName(s));
	return names.back();
}

GDExtensionPropertyInfo info(GDExtensionVariantType type, godot::StringName &name) {
	godot::StringName &class_name = keep("");
	strings.push_back(godot::String());
	return { type, name._native_ptr(), class_name._native_ptr(), 0, strings.back()._native_ptr(), static_cast<uint32_t>(godot::PROPERTY_USAGE_DEFAULT) };
}

void register_getter(const godot::StringName &klass, godot::StringName &name, GDExtensionClassMethodCall call, GDExtensionClassMethodPtrCall ptr, void *userdata, GDExtensionVariantType type, GDExtensionClassMethodArgumentMetadata meta) {
	GDExtensionPropertyInfo ret = info(type, keep(""));
	GDExtensionClassMethodInfo method{};
	method.name = name._native_ptr();
	method.method_userdata = userdata;
	method.call_func = call;
	method.ptrcall_func = ptr;
	method.method_flags = GDEXTENSION_METHOD_FLAGS_DEFAULT;
	method.has_return_value = true;
	method.return_value_info = &ret;
	method.return_value_metadata = meta;
	godot::internal::gdextension_interface_classdb_register_extension_class_method(godot::internal::library, klass._native_ptr(), &method);
}

void register_setter(const godot::StringName &klass, godot::StringName &name, godot::StringName &arg_name, GDExtensionClassMethodCall call, GDExtensionClassMethodPtrCall ptr, void *userdata, GDExtensionVariantType type, GDExtensionClassMethodArgumentMetadata meta) {
	GDExtensionPropertyInfo arg = info(type, arg_name);
	GDExtensionClassMethodArgumentMetadata arg_meta = meta;
	GDExtensionClassMethodInfo method{};
	method.name = name._native_ptr();
	method.method_userdata = userdata;
	method.call_func = call;
	method.ptrcall_func = ptr;
	method.method_flags = GDEXTENSION_METHOD_FLAGS_DEFAULT;
	method.argument_count = 1;
	method.arguments_info = &arg;
	method.arguments_metadata = &arg_meta;
	godot::internal::gdextension_interface_classdb_register_extension_class_method(godot::internal::library, klass._native_ptr(), &method);
}

void register_property(const godot::StringName &klass, godot::StringName &name, GDExtensionVariantType type, godot::StringName &setter, godot::StringName &getter) {
	GDExtensionPropertyInfo property = info(type, name);
	godot::internal::gdextension_interface_classdb_register_extension_class_property(godot::internal::library, klass._native_ptr(), &property, setter._native_ptr(), getter._native_ptr());
}

godot::String to_string(std::string_view sv) {
	return godot::String::utf8(sv.data(), static_cast<int>(sv.size()));
}

} // namespace


godot::String snake_case(std::string_view name) {
	std::string out;
	for (std::size_t i = 0; i < name.size(); i++) {
		char c = name[i];
		bool upper = c >= 'A' && c <= 'Z';
		if (upper && i > 0) {
			char prev = name[i - 1];
			bool prev_lower = (prev >= 'a' && prev <= 'z') || (prev >= '0' && prev <= '9');
			bool next_lower = i + 1 < name.size() && name[i + 1] >= 'a' && name[i + 1] <= 'z';
			bool prev_upper = prev >= 'A' && prev <= 'Z';
			if (prev_lower || (prev_upper && next_lower)) {
				out.push_back('_');
			}
		}
		out.push_back(upper ? static_cast<char>(c - 'A' + 'a') : c);
	}
	return godot::String::utf8(out.c_str());
}

void bind_value_properties(const godot::StringName &godot_class, const entt::meta_type &type) {
	for (auto [id, data] : type.data()) {
		const FieldOps *ops = data.custom();
		ERR_CONTINUE_MSG(ops == nullptr, "field reflected without GDNativeSDK::ECS::reflect");
		godot::String field = to_string(data.name());
		godot::StringName &prop = keep(field);
		godot::StringName &getter = keep("get_" + field);
		godot::StringName &setter = keep("set_" + field);
		register_getter(godot_class, getter, ops->value_get_call, ops->value_get_ptr, nullptr, ops->value_type, ops->value_meta);
		register_setter(godot_class, setter, prop, ops->value_set_call, ops->value_set_ptr, nullptr, ops->value_type, ops->value_meta);
		register_property(godot_class, prop, ops->value_type, setter, getter);
	}
}

void bind_column_properties(const godot::StringName &chunk_class, const entt::meta_type &type, const godot::String &prefix, std::size_t slot, bool writable) {
	// The "slot" is packed as the "method_userdata" value.
	void *userdata = reinterpret_cast<void *>(static_cast<uintptr_t>(slot));
	for (auto [id, data] : type.data()) {
		const FieldOps *ops = data.custom();
		ERR_CONTINUE_MSG(ops == nullptr, "field reflected without GDNativeSDK::ECS::reflect");
		godot::String column = prefix + godot::String("_") + to_string(data.name());
		godot::StringName &prop = keep(column);
		godot::StringName &getter = keep("get_" + column);
		godot::StringName &setter = keep(writable ? "set_" + column : godot::String());
		register_getter(chunk_class, getter, ops->column_get_call, ops->column_get_ptr, userdata, ops->column_type, GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE);
		if (writable) {
			register_setter(chunk_class, setter, prop, ops->column_set_call, ops->column_set_ptr, userdata, ops->column_type, GDEXTENSION_METHOD_ARGUMENT_METADATA_NONE);
		}
		register_property(chunk_class, prop, ops->column_type, setter, getter);
	}
}

void *proxy_data(GDExtensionClassInstancePtr instance) {
	return static_cast<ECSComponent *>(reinterpret_cast<godot::Wrapped *>(instance))->data();
}

void *const *chunk_component_ptrs(GDExtensionClassInstancePtr instance, void *userdata, std::size_t &count, bool write) {
	ECSChunk *chunk = static_cast<ECSChunk *>(reinterpret_cast<godot::Wrapped *>(instance));
	void *const *compPtrs = chunk->component_ptrs(static_cast<std::size_t>(reinterpret_cast<uintptr_t>(userdata)), write);
	count = compPtrs ? chunk->count : 0;
	return compPtrs;
}

} // namespace GDNativeSDK::ECS
