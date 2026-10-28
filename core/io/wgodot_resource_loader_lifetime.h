// wgodot-changes::file
#pragma once

#include "core/io/resource_loader.h"

class CallQueue;

class WGodotResourceLoaderLifetime {
	static uint32_t active_runs;

	static void finish_run(ResourceLoader::LoadToken *p_token);
	static void release_pool_task(WorkerThreadPool::TaskID p_task_id);

public:
	static void connect_changed_deferred(const Ref<Resource> &p_source, const Callable &p_callable, uint32_t p_flags);
	static void disconnect_changed_deferred(const Ref<Resource> &p_source, const Callable &p_callable);

	// A collaborative wait can execute unrelated pool work on a loading thread.
	// That work must have its own loader context, just as on a fresh worker.
	class WorkerScope {
		ResourceLoader::ThreadLoadTask *previous_task;
		int previous_nesting;
		bool previous_import_thread;
		HashMap<int, HashMap<String, Ref<Resource>>> previous_overrides;

	public:
		WorkerScope();
		~WorkerScope();
		WorkerScope(const WorkerScope &) = delete;
		WorkerScope &operator=(const WorkerScope &) = delete;
	};

	class TaskScope {
		ResourceLoader::LoadToken *token;
		ResourceLoader::ThreadLoadTask *previous_task;
		CallQueue *owned_queue = nullptr;

	public:
		explicit TaskScope(ResourceLoader::LoadToken *p_token);
		~TaskScope();
		TaskScope(const TaskScope &) = delete;
		TaskScope &operator=(const TaskScope &) = delete;

		void create_queue_override();
	};

	// Both operations require ResourceLoader::thread_load_mutex.
	static void reference_run(ResourceLoader::LoadToken *p_token);
	static bool has_active_runs();

	static void clear_token(ResourceLoader::LoadToken *p_token);
	static void clear_remaining_tokens();
};
