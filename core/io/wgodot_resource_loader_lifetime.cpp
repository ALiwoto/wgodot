// wgodot-changes::file
#include "wgodot_resource_loader_lifetime.h"

#include "core/object/message_queue.h"
#include "core/os/safe_binary_mutex.h"

uint32_t WGodotResourceLoaderLifetime::active_runs = 0;

WGodotResourceLoaderLifetime::TaskScope::TaskScope(ResourceLoader::LoadToken *p_token) :
		token(p_token),
		previous_task(ResourceLoader::curr_load_task) {
}

WGodotResourceLoaderLifetime::TaskScope::~TaskScope() {
	if (owned_queue) {
		MessageQueue::set_thread_singleton_override(nullptr);
		memdelete(owned_queue);
	}
	ResourceLoader::curr_load_task = previous_task;
	finish_run(token);
}

void WGodotResourceLoaderLifetime::TaskScope::create_queue_override() {
	owned_queue = memnew(CallQueue);
	MessageQueue::set_thread_singleton_override(owned_queue);
}

void WGodotResourceLoaderLifetime::reference_run(ResourceLoader::LoadToken *p_token) {
	p_token->reference();
	active_runs++;
}

bool WGodotResourceLoaderLifetime::has_active_runs() {
	return active_runs != 0;
}

void WGodotResourceLoaderLifetime::finish_run(ResourceLoader::LoadToken *p_token) {
	MutexLock lock(ResourceLoader::thread_load_mutex);
	DEV_ASSERT(active_runs > 0);
	if (p_token->unreference()) {
		memdelete(p_token);
	}
	active_runs--;
}

void WGodotResourceLoaderLifetime::release_pool_task(WorkerThreadPool::TaskID p_task_id) {
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	MutexLock lock(pool->task_mutex);
	WorkerThreadPool::Task *task = pool->tasks[p_task_id];
	DEV_ASSERT(!task->group);

	// The load owns this task handle. After its final use, the pool can reclaim
	// it at completion without making a worker wait for itself or an ancestor.
	task->wgodot_release_on_completion = true;
	if (task->completed && task->waiting_pool == 0 && task->waiting_user == 0) {
		pool->tasks.erase(p_task_id);
		pool->task_allocator.free(task);
	}
}

void WGodotResourceLoaderLifetime::clear_token(ResourceLoader::LoadToken *p_token) {
	MutexLock lock(ResourceLoader::thread_load_mutex);
	DEV_ASSERT(p_token->user_rc == 0 && p_token->user_path.is_empty());

	if (!p_token->local_path.is_empty()) {
		if (p_token->task_if_unregistered) {
			memdelete(p_token->task_if_unregistered);
			p_token->task_if_unregistered = nullptr;
		} else {
			DEV_ASSERT(ResourceLoader::thread_load_tasks.has(p_token->local_path));
			DEV_ASSERT(ResourceLoader::thread_load_tasks[p_token->local_path].status == ResourceLoader::THREAD_LOAD_FAILED || ResourceLoader::thread_load_tasks[p_token->local_path].status == ResourceLoader::THREAD_LOAD_LOADED);
			ResourceLoader::thread_load_tasks.erase(p_token->local_path);
		}
		p_token->local_path.clear();
	}

	if (p_token->wgodot_owned_pool_task != WorkerThreadPool::INVALID_TASK_ID) {
		release_pool_task(p_token->wgodot_owned_pool_task);
		p_token->wgodot_owned_pool_task = WorkerThreadPool::INVALID_TASK_ID;
	}
}

void WGodotResourceLoaderLifetime::clear_remaining_tokens() {
	// Shutdown has drained all runs. Detach any externally held token before
	// removing its task, so its eventual destructor cannot access a stale entry.
	MutexLock lock(ResourceLoader::thread_load_mutex);
	DEV_ASSERT(!has_active_runs());
	while (ResourceLoader::thread_load_tasks.begin()) {
		clear_token(ResourceLoader::thread_load_tasks.begin()->value.load_token);
	}
}
