/**************************************************************************/
/*  editor_scene_importer_gltf.h                                          */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "editor/import/3d/resource_importer_scene.h"
// wgodot-changes::begin
#include "core/object/worker_thread_pool.h"
// wgodot-changes::end

class Animation;
class Node;

class EditorSceneFormatImporterGLTF : public EditorSceneFormatImporter {
	GDCLASS(EditorSceneFormatImporterGLTF, EditorSceneFormatImporter);

	// wgodot-changes::begin
	struct PreparedScene {
		String path;
		uint32_t flags = 0;
		HashMap<StringName, Variant> options;
		List<String> missing_dependencies;
		Error error = OK;
		Node *scene = nullptr;
		WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
		EditorSceneFormatImporterGLTF *importer = nullptr;
	};
	HashMap<String, PreparedScene *> prepared_scenes;
	static void _prepare_scene(void *p_data);
	Node *_import_scene(const String &p_path, uint32_t p_flags, const HashMap<StringName, Variant> &p_options, List<String> *r_missing_deps, Error *r_err);
	// wgodot-changes::end

public:
	// wgodot-changes::begin
	void prepare_import_scene(const String &p_path, uint32_t p_flags, const HashMap<StringName, Variant> &p_options) override;
	void finish_prepared_scenes() override;
	// wgodot-changes::end
	virtual void get_extensions(List<String> *r_extensions) const override;
	virtual Node *import_scene(const String &p_path, uint32_t p_flags,
			const HashMap<StringName, Variant> &p_options,
			List<String> *r_missing_deps, Error *r_err = nullptr) override;
	virtual void get_import_options(const String &p_path,
			List<ResourceImporter::ImportOption> *r_options) override;
	virtual void handle_compatibility_options(HashMap<StringName, Variant> &p_import_params) const override;
	virtual Variant get_option_visibility(const String &p_path, const String &p_scene_import_type,
			const String &p_option, const HashMap<StringName, Variant> &p_options) override;
};
