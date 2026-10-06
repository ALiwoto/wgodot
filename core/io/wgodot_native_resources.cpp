// wgodot-changes::file
#include "wgodot_native_resources.h"

#include "core/io/resource_loader.h"
#include "core/io/wgodot_resource_paths.h"
#include "core/object/class_db.h"

namespace WGodotNativeResources {
namespace {
class NativeLoader : public ResourceFormatLoader {
	const Definition *definitions;
	int count;

	const Definition *find(const String &p_path) const {
		const int64_t id = WGodotResourcePaths::from_path(p_path);
		for (int i = 0; i < count; i++) {
			if (definitions[i].id == id) {
				return &definitions[i];
			}
		}
		return nullptr;
	}

public:
	NativeLoader(const Definition *p_definitions, int p_count) : definitions(p_definitions), count(p_count) {}
	bool recognize_path(const String &p_path, const String &p_type) const override {
		const Definition *definition = find(p_path);
		return definition && (p_type.is_empty() || ClassDB::is_parent_class(StringName(definition->type), StringName(p_type)));
	}
	bool handles_type(const String &p_type) const override { return true; }
	bool exists(const String &p_path) const override { return find(p_path) != nullptr; }
	String get_resource_type(const String &p_path) const override {
		const Definition *definition = find(p_path);
		return definition ? String(definition->type) : String();
	}
	Ref<Resource> load(const String &p_path, const String &p_original_path, Error *r_error, bool p_use_sub_threads, float *r_progress, CacheMode p_cache_mode) override {
		const Definition *definition = find(p_path);
		ERR_FAIL_NULL_V(definition, Ref<Resource>());
		Ref<Resource> resource = definition->create(p_cache_mode);
		if (r_error) {
			*r_error = resource.is_valid() ? OK : ERR_CANT_CREATE;
		}
		// ResourceLoader owns cache registration and replacement of existing objects.
		return resource;
	}
};

Ref<NativeLoader> loader;
} // namespace

void initialize(const Definition *p_definitions, int p_count) {
	loader = Ref<NativeLoader>(memnew(NativeLoader(p_definitions, p_count)));
	ResourceLoader::add_resource_format_loader(loader, true);
}

void clear() {
	if (loader.is_valid()) {
		ResourceLoader::remove_resource_format_loader(loader);
		loader.unref();
	}
}
} // namespace WGodotNativeResources
