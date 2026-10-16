// wgodot-changes::file
#include "scene_preparation.h"

#include "core/object/worker_thread_pool.h"

namespace {

Vector<WorkerThreadPool::TaskID> disposal_tasks;
bool finishing_disposals = false;

void dispose_branches(void *p_data) {
	Vector<Node *> *roots = static_cast<Vector<Node *> *>(p_data);
	for (Node *root : *roots) {
		memdelete(root);
	}
	memdelete(roots);
}

} // namespace

void ScenePreparation::free_branch_async(Node *p_root) {
	ERR_FAIL_NULL(p_root);
	Vector<Node *> roots;
	roots.push_back(p_root);
	free_branches_async(roots);
}

void ScenePreparation::free_branches_async(const Vector<Node *> &p_roots) {
	for (Node *root : p_roots) {
		ERR_FAIL_NULL(root);
		ERR_FAIL_COND_MSG(root->is_inside_tree() || root->get_parent(), "Background disposal requires exclusive ownership of detached scene roots.");
	}
	// A preparation job already owns its worker. Dispose there directly,
	// including nested particle pools, instead of creating dependent tasks.
	if (!Thread::is_main_thread() || finishing_disposals) {
		for (Node *root : p_roots) {
			memdelete(root);
		}
		return;
	}
	if (p_roots.is_empty()) {
		return;
	}
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	for (int i = disposal_tasks.size() - 1; i >= 0; --i) {
		if (pool->is_task_completed(disposal_tasks[i])) {
			pool->wait_for_task_completion(disposal_tasks[i]);
			disposal_tasks.remove_at(i);
		}
	}
	Vector<Node *> *roots = memnew(Vector<Node *>(p_roots));
	disposal_tasks.push_back(pool->add_native_task(dispose_branches, roots, false, "Dispose detached scene resources"));
}

void ScenePreparation::finish_disposals() {
	finishing_disposals = true;
	for (WorkerThreadPool::TaskID task : disposal_tasks) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(task);
	}
	disposal_tasks.clear();
}
