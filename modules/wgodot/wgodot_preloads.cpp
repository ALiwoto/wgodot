// wgodot-changes::file
#include "wgodot_preloads.h"

#include "core/config/engine.h"
#include "core/io/resource_loader.h"
#include "core/object/class_db.h"
#include "core/os/thread.h"

WGodotPreloads *WGodotPreloads::singleton = nullptr;
const WGodotPreloads::Definition *WGodotPreloads::definitions = nullptr;
int WGodotPreloads::definition_count = 0;

void WGodotPreloads::initialize() {
	GDREGISTER_ABSTRACT_CLASS(WGodotPreloads);
	Engine::get_singleton()->add_singleton(Engine::Singleton("WGodotPreloads", memnew(WGodotPreloads)));
}

void WGodotPreloads::deinitialize() {
	Engine::get_singleton()->remove_singleton("WGodotPreloads");
	memdelete(singleton);
}

void WGodotPreloads::set_manifest(const Definition *p_definitions, int p_count) {
	definitions = p_definitions;
	definition_count = p_count;
}

Error WGodotPreloads::load_synchronous(int p_index) {
	MutexLock lock(mutex);
	Entry &entry = entries.write[p_index];
	if (entry.state == READY) {
		return OK;
	}
	ERR_FAIL_COND_V_MSG(entry.state == LOADING, ERR_CYCLIC_LINK, "Cyclic preload initialization: " + entry.path);
	entry.state = LOADING;
	Error error = OK;
	lock.temp_unlock();
	Ref<Resource> resource = ResourceLoader::load(entry.path, entry.type, ResourceLoader::CACHE_MODE_REUSE, &error);
	lock.temp_relock();
	entry.resource = resource;
	entry.error = error;
	if (entry.resource.is_null() && entry.error == OK) {
		entry.error = ERR_CANT_OPEN;
	}
	entry.state = entry.error == OK ? READY : FAILED;
	if (entry.error != OK) {
		ERR_PRINT(vformat("Could not preload %s: %s", entry.path, error_names[entry.error]));
	}
	return entry.error;
}

Error WGodotPreloads::start() {
	entries.resize(definition_count);
	for (int i = 0; i < definition_count; ++i) {
		Entry &entry = entries.write[i];
		entry.path = String::utf8(definitions[i].path);
		entry.type = String::utf8(definitions[i].type);
		entry.asynchronous = definitions[i].asynchronous;
		if (entry.asynchronous) {
			++total_count;
		}
	}
	loading_synchronous = true;
	for (int i = 0; i < entries.size(); ++i) {
		if (!entries[i].asynchronous) {
			Error error = load_synchronous(i);
			if (error != OK) {
				loading_synchronous = false;
				return error;
			}
		}
	}
	loading_synchronous = false;
	for (int i = 0; i < entries.size(); ++i) {
		Entry &entry = entries.write[i];
		if (!entry.asynchronous) {
			continue;
		}
		{
			MutexLock lock(mutex);
			entry.state = LOADING;
		}
		Error error = ResourceLoader::load_threaded_request(entry.path, entry.type);
		MutexLock lock(mutex);
		entry.error = error;
		if (entry.error != OK) {
			entry.state = FAILED;
			++failed_count;
		} else {
			entry.requested = true;
			entry.state = LOADING;
		}
	}
	poll();
	return OK;
}

void WGodotPreloads::collect(int p_index) {
	// Only the main thread collects requests. Publish a finished reference under
	// the mutex; generated code never observes a worker's unfinished resource.
	Error error = OK;
	Ref<Resource> resource = ResourceLoader::load_threaded_get(entries[p_index].path, &error);
	MutexLock lock(mutex);
	Entry &entry = entries.write[p_index];
	entry.requested = false;
	entry.error = resource.is_null() && error == OK ? ERR_CANT_OPEN : error;
	entry.resource = resource;
	entry.state = entry.error == OK ? READY : FAILED;
	if (entry.state == READY) {
		++loaded_count;
	} else {
		++failed_count;
		ERR_PRINT(vformat("Could not async_preload %s: %s", entry.path, error_names[entry.error]));
	}
}

void WGodotPreloads::poll() {
	if (loaded_count + failed_count == total_count) {
		return;
	}
	for (int i = 0; i < entries.size(); ++i) {
		if (entries[i].requested && ResourceLoader::load_threaded_get_status(entries[i].path) != ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			collect(i);
		}
	}
}

void WGodotPreloads::finish() {
	for (int i = 0; i < entries.size(); ++i) {
		if (entries[i].requested) {
			collect(i);
		}
	}
}

Ref<Resource> WGodotPreloads::get_resource(int p_index) {
	MutexLock lock(mutex);
	ERR_FAIL_INDEX_V(p_index, entries.size(), Ref<Resource>());
	// A custom resource constructor may refer to another synchronous preload.
	if (loading_synchronous && Thread::is_main_thread() && !entries[p_index].asynchronous && entries[p_index].state == PENDING) {
		lock.temp_unlock();
		Error error = load_synchronous(p_index);
		lock.temp_relock();
		if (error != OK) {
			return Ref<Resource>();
		}
	}
	const Entry &entry = entries[p_index];
	ERR_FAIL_COND_V_MSG(entry.state != READY, Ref<Resource>(), vformat("Preloaded resource accessed %s: %s", entry.state == FAILED ? "after loading failed" : "before loading completed", entry.path));
	return entry.resource;
}

bool WGodotPreloads::require_resources(std::initializer_list<int> p_indices) {
	for (int index : p_indices) {
		if (get_resource(index).is_null()) {
			return false;
		}
	}
	return true;
}

bool WGodotPreloads::is_finished() const {
	return get_pending_count() == 0;
}
bool WGodotPreloads::has_failed() const {
	return get_failed_count() != 0;
}
int WGodotPreloads::get_total_count() const {
	MutexLock lock(mutex);
	return total_count;
}
int WGodotPreloads::get_loaded_count() const {
	MutexLock lock(mutex);
	return loaded_count;
}
int WGodotPreloads::get_failed_count() const {
	MutexLock lock(mutex);
	return failed_count;
}
int WGodotPreloads::get_pending_count() const {
	MutexLock lock(mutex);
	return total_count - loaded_count - failed_count;
}
double WGodotPreloads::get_progress() const {
	MutexLock lock(mutex);
	return total_count == 0 ? 1.0 : double(loaded_count + failed_count) / total_count;
}

TypedArray<Dictionary> WGodotPreloads::get_failures() const {
	MutexLock lock(mutex);
	TypedArray<Dictionary> failures;
	for (const Entry &entry : entries) {
		if (entry.asynchronous && entry.state == FAILED) {
			Dictionary failure;
			failure["path"] = entry.path;
			failure["type"] = entry.type;
			failure["error"] = entry.error;
			failures.push_back(failure);
		}
	}
	return failures;
}

void WGodotPreloads::_bind_methods() {
	ClassDB::bind_method(D_METHOD("is_finished"), &WGodotPreloads::is_finished);
	ClassDB::bind_method(D_METHOD("has_failed"), &WGodotPreloads::has_failed);
	ClassDB::bind_method(D_METHOD("get_total_count"), &WGodotPreloads::get_total_count);
	ClassDB::bind_method(D_METHOD("get_loaded_count"), &WGodotPreloads::get_loaded_count);
	ClassDB::bind_method(D_METHOD("get_failed_count"), &WGodotPreloads::get_failed_count);
	ClassDB::bind_method(D_METHOD("get_pending_count"), &WGodotPreloads::get_pending_count);
	ClassDB::bind_method(D_METHOD("get_progress"), &WGodotPreloads::get_progress);
	ClassDB::bind_method(D_METHOD("get_failures"), &WGodotPreloads::get_failures);
}

WGodotPreloads::WGodotPreloads() { singleton = this; }
WGodotPreloads::~WGodotPreloads() { singleton = nullptr; }
