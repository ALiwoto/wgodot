// wgodot-changes::file
#pragma once

#include "core/io/resource_loader.h"

class CallQueue;

class WGodotResourceLoaderLifetime {
	static uint32_t active_runs;

	static void finish_run(ResourceLoader::LoadToken *p_token);
	static void release_pool_task(WorkerThreadPool::TaskID p_task_id);

public:
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
