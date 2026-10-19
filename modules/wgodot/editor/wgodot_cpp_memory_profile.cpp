// wgodot-changes::file
#include "wgodot_cpp_memory_profile.h"

#include "core/io/dir_access.h"
#include "core/io/resource.h"
#include "core/object/object.h"
#include "core/os/memory.h"
#include "core/os/os.h"

thread_local WGodotCppMemoryProfile *WGodotCppMemoryProfile::active = nullptr;

WGodotCppMemoryProfile::WGodotCppMemoryProfile(const String &p_directory) {
	previous = active;
	active = nullptr;
	if (p_directory.is_empty() || DirAccess::make_dir_recursive_absolute(p_directory) != OK) {
		return;
	}
	samples = FileAccess::open(p_directory.path_join("cpp-memory.csv"), FileAccess::WRITE);
	resources = FileAccess::open(p_directory.path_join("cpp-memory-resources.csv"), FileAccess::WRITE);
	if (!is_open()) {
		return;
	}
	started = OS::get_singleton()->get_ticks_msec();
	samples->store_csv_line({ "elapsed_ms", "phase", "source", "tracked_bytes", "tracked_peak_bytes", "cached_resources", "objects", "items" });
	resources->store_csv_line({ "phase", "class", "path" });
	active = this;
	sample("export-start");
	snapshot_resources("export-start");
}

WGodotCppMemoryProfile::~WGodotCppMemoryProfile() {
	if (active == this) {
		sample("export-scope-released");
		snapshot_resources("export-scope-released");
	}
	active = previous;
}

void WGodotCppMemoryProfile::sample(const char *p_phase, const String &p_source, int64_t p_items) {
	if (!active) {
		return;
	}
	const uint64_t used = Memory::get_mem_usage();
	const uint64_t peak = Memory::get_mem_max_usage();
	const int cached = ResourceCache::get_cached_resource_count();
	const int objects = ObjectDB::get_object_count();
	active->samples->store_csv_line({
		itos(OS::get_singleton()->get_ticks_msec() - active->started), p_phase, p_source,
		itos(used), itos(peak), itos(cached), itos(objects), itos(p_items),
	});
	active->samples->flush();
}

void WGodotCppMemoryProfile::snapshot_resources(const char *p_phase) {
	if (!active) {
		return;
	}
	List<Ref<Resource>> cached;
	ResourceCache::get_cached_resources(&cached);
	for (const Ref<Resource> &resource : cached) {
		active->resources->store_csv_line({ p_phase, resource->get_class(), resource->get_path() });
	}
	active->resources->flush();
}
