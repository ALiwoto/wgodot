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

	while (!p_group->completed.is_set()) {
		Task *pending = nullptr;
		bool had_pump_task = false;
		{
			MutexLock lock(task_mutex);
			if (thread_index >= 0) {
				had_pump_task = threads[thread_index].has_pump_task;
			}
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

		if (thread_index >= 0) {
			_process_task(pending);
			MutexLock lock(task_mutex);
			threads[thread_index].has_pump_task = had_pump_task;
		} else {
			// A render/main-thread waiter can hold a shader lock that every
			// loading worker needs. Join only this group, without borrowing
			// a worker's task state or running unrelated resource loads.
			wgodot_execute_group_task(pending);
			MutexLock lock(task_mutex);
			if (pending->low_priority) {
				low_priority_threads_used--;
				if (_try_promote_low_priority_task()) {
					_notify_threads(nullptr, 1, 0);
				}
			}
			task_allocator.free(pending);
		}
	}
#endif
}

void WorkerThreadPool::wgodot_execute_group_task(Task *p_task) {
	// Handling a group
	bool do_post = false;

	while (true) {
		uint32_t work_index = p_task->group->index.postincrement();

		if (work_index >= p_task->group->max) {
			break;
		}
		if (p_task->native_group_func) {
			p_task->native_group_func(p_task->native_func_userdata, work_index);
		} else if (p_task->template_userdata) {
			p_task->template_userdata->callback_indexed(work_index);
		} else {
			p_task->callable.call(work_index);
		}

		// This is the only way to ensure posting is done when all tasks are really complete.
		uint32_t completed_amount = p_task->group->completed_index.increment();

		if (completed_amount == p_task->group->max) {
			do_post = true;
		}
	}

	if (do_post && p_task->template_userdata) {
		memdelete(p_task->template_userdata); // This is no longer needed at this point, so get rid of it.
	}

	if (do_post) {
		p_task->group->done_semaphore.post();
		p_task->group->completed.set_to(true);
	}
	uint32_t max_users = p_task->group->tasks_used + 1; // Add 1 because the thread waiting for it is also user. Read before to avoid another thread freeing task after increment.
	uint32_t finished_users = p_task->group->finished.increment();

	if (finished_users == max_users) {
		// Get rid of the group, because nobody else is using it.
		MutexLock task_lock(task_mutex);
		group_allocator.free(p_task->group);
	}
}
