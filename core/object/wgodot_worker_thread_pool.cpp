// wgodot-changes::file

#include "worker_thread_pool.h"

#include "core/os/os.h"

void WorkerThreadPool::wgodot_delay_usec(uint32_t p_usec) {
	// Resource dependency polling must lift the same permitted locks as a
	// collaborative wait, or the dependency can block on the caller's cache lock.
	if (this == singleton) {
		_unlock_unlockable_mutexes();
	}
	OS::get_singleton()->delay_usec(p_usec);
	if (this == singleton) {
		_lock_unlockable_mutexes();
	}
}

void WorkerThreadPool::wgodot_process_pending_group_tasks(Group *p_group) {
#ifdef THREADS_ENABLED
	const int thread_index = get_thread_index();
	if (thread_index == -1) {
		return;
	}

	while (!p_group->completed.is_set()) {
		Task *pending = nullptr;
		bool had_pump_task = false;
		{
			MutexLock lock(task_mutex);
			had_pump_task = threads[thread_index].has_pump_task;
			// Only join the requested group. Running unrelated jobs here could
			// enter code that needs locks held by the waiting caller.
			for (SelfList<Task>::List *queue : { &task_queue, &low_priority_task_queue }) {
				for (SelfList<Task> *entry = queue->first(); entry; entry = entry->next()) {
					if (entry->self()->group != p_group) {
						continue;
					}
					pending = entry->self();
					queue->remove(entry);
					if (queue == &low_priority_task_queue) {
						// Match promotion bookkeeping; _process_task releases it.
						low_priority_threads_used++;
					}
					break;
				}
				if (pending) {
					break;
				}
			}
		}
		if (!pending) {
			// All remaining work is already running on other workers.
			return;
		}

		_process_task(pending);
		{
			MutexLock lock(task_mutex);
			threads[thread_index].has_pump_task = had_pump_task;
		}
	}
#endif
}
