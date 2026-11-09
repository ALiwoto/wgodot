// wgodot-changes::file
#pragma once

#include "wgodot_resource_syntax.h"

#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

class WGodotResourceCheck : public RefCounted {
	struct Entry {
		String digest;
		WGodotResourceSyntax::Result syntax;
	};
	struct Item {
		String path;
		Entry entry;
		bool trusted = false;
		bool changed = false;
		bool parsed = false;
	};

	// Accessed on the editor thread only. Workers own snapshots, never this cache.
	static HashMap<String, Entry> cache;
	static HashSet<String> trusted;
	static String context;
	static uint64_t revision;

	Vector<Item> items;
	String cache_path;
	String class_identity;
	String cache_context;
	uint64_t start_revision = 0;
	bool inventory_changed = false;
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	SafeFlag cancelled;
	Dictionary result;
	static void run(void *p_userdata);
	void check_item(uint32_t p_index, Item **p_pending);
	void save_cache() const;

public:
	static void invalidate(const HashSet<String> &p_paths, bool p_all);
	void start();
	void start(const Vector<String> &p_paths);
	Dictionary poll();
	~WGodotResourceCheck();
};
