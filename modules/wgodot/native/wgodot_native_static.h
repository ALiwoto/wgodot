// wgodot-changes::file
#pragma once

#include "core/os/mutex.h"
#include "core/templates/vector.h"

#include <atomic>

namespace WGodotNative {

class StaticRegistry {
	Mutex mutex;
	Vector<void (*)()> cleanup;

public:
	static StaticRegistry &get() {
		static StaticRegistry registry;
		return registry;
	}
	const Mutex &get_mutex() const { return mutex; }
	void add(void (*p_cleanup)()) { cleanup.push_back(p_cleanup); }
	void clear() {
		MutexLock lock(mutex);
		while (!cleanup.is_empty()) {
			auto release = cleanup[cleanup.size() - 1];
			cleanup.resize(cleanup.size() - 1);
			release();
		}
	}
};

// Fields are constructed once; an optional callback can then populate them.
// Recursive calls from that callback see the constructed fields; other threads
// wait until initialization finishes. The fast path is one acquire load.
// Module shutdown destroys the fields before the engine has stopped.
template <class Fields>
class StaticStorage {
	inline static std::atomic<Fields *> ready{ nullptr };
	inline static Fields *initializing = nullptr;

	static void clear() {
		Fields *fields = initializing;
		initializing = nullptr;
		ready.store(nullptr, std::memory_order_release);
		memdelete(fields);
	}

public:
	static bool is_ready() { return ready.load(std::memory_order_acquire) != nullptr; }
	static Fields &get(void (*p_initialize)(Fields &) = nullptr) {
		if (Fields *fields = ready.load(std::memory_order_acquire)) {
			return *fields;
		}
		auto &registry = StaticRegistry::get();
		MutexLock lock(registry.get_mutex());
		if (!initializing) {
			initializing = memnew(Fields);
			registry.add(&clear);
			if (p_initialize) {
				p_initialize(*initializing);
			}
			ready.store(initializing, std::memory_order_release);
		}
		return *initializing;
	}
};

} // namespace WGodotNative
