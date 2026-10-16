// wgodot-changes::file
/**************************************************************************/
/*  gdscript_check_cli.h                                                  */
/**************************************************************************/

#pragma once

#include "export/export_project.h"
#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/variant/dictionary.h"

namespace WGodotGDScriptCheckCLI {

class ProjectCheck : public RefCounted {
	WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
	Dictionary result;
	WGodotGDScriptExportTransform::ExportProject project;
	SafeFlag cancelled;
	static void run(void *p_userdata);

public:
	void start();
	Dictionary poll();
	~ProjectCheck();
};

} // namespace WGodotGDScriptCheckCLI
