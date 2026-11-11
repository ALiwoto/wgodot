// wgodot-changes::file
#pragma once

#include "wgodot_resource_syntax.h"
#include "wgodot_resource_types.h"

#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_set.h"
#include "core/variant/dictionary.h"

#include <memory>

class WGodotResourceCheck : public RefCounted {
	struct Entry {
		String digest;
		WGodotResourceSyntax::Result syntax;
	};
	struct Item {
		String path;
		Entry entry;
		bool changed = false;
		bool parsed = false;
	};

	// Accessed on the editor thread only. Workers own snapshots, never this cache.
	static HashMap<String, Entry> cache;
	static String context;

	Vector<Item> items;
	String cache_path;
	String class_identity;
	String cache_context;
	bool inventory_changed = false;
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	SafeFlag cancelled;
	Dictionary result;
	WGodotGDScriptExportTransform::ExportProject project;
	std::unique_ptr<const WGodotResourceTypes> types;
	static void run(void *p_userdata);
	void check_item(uint32_t p_index, Item **p_pending);
	void save_cache() const;
	void start_paths(const Vector<String> &p_paths);

public:
	static void invalidate(const HashSet<String> &p_paths, bool p_all);
	void start(const WGodotGDScriptExportTransform::ExportProject &p_project);
	void start(const Vector<String> &p_paths, const WGodotGDScriptExportTransform::ExportProject &p_project);
	void start(const Vector<String> &p_paths, const WGodotResourceTypes &p_types);
	Dictionary poll();
	~WGodotResourceCheck();
};
