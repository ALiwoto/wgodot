// wgodot-changes::file
/**************************************************************************/
/*  gdscript_check_cli.h                                                  */
/**************************************************************************/

#pragma once

#include "export/export_project.h"
#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/variant/dictionary.h"
#include "modules/wgodot/editor/wgodot_check_budget.h"

#include <memory>

namespace WGodotGDScriptCheckCLI {

class ProjectCheck : public RefCounted {
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	Dictionary result;
	WGodotGDScriptExportTransform::ExportProject project;
	SafeFlag cancelled;
	std::shared_ptr<WGodotCheckBudget> budget;
	static void run(void *p_userdata);

public:
	const WGodotGDScriptExportTransform::ExportProject &get_project() const { return project; }
	void start(std::shared_ptr<WGodotCheckBudget> p_budget = {});
	void start(const Vector<String> &p_paths, std::shared_ptr<WGodotCheckBudget> p_budget = {});
	Dictionary poll();
	~ProjectCheck();
};

} // namespace WGodotGDScriptCheckCLI
