// wgodot-changes::file

#include "../gltf_document.h"
#include "editor_scene_importer_gltf.h"

#include "core/io/file_access.h"
#include "core/io/json.h"
#include "core/io/resource_loader.h"
#include "core/io/wgodot_resource_trace.h"
#include "core/os/os.h"
#include "editor/editor_node.h"
#include "editor/gui/progress_dialog.h"

namespace {

void wait_for_prepared_scene(WorkerThreadPool::TaskID p_task) {
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	ProgressDialog *progress = ProgressDialog::get_singleton();
	if (Thread::is_main_thread() && !EditorNode::is_cmdline_mode() && progress) {
		// Parsing runs on workers, so service the UI while waiting for their result.
		// Foreground import tasks retain the editor's resource-editing input guard.
		while (!pool->is_task_completed(p_task)) {
			progress->refresh_worker_progress();
			OS::get_singleton()->delay_usec(1000);
		}
	}
	pool->wait_for_task_completion(p_task);
}

bool can_prepare_glb(const String &p_path, const HashMap<StringName, Variant> &p_options) {
	if (p_path.get_extension().to_lower() != "glb") {
		return false;
	}
	for (const Ref<GLTFDocumentExtension> &extension : GLTFDocument::get_all_gltf_document_extensions()) {
		if (extension->get_script_instance() || !extension->supports_threaded_import()) {
			return false;
		}
	}

	// Texture extraction modifies the editor filesystem and can synchronously
	// import dependencies. Only self-contained input is prepared off-thread.
	Ref<FileAccess> file = FileAccess::open(p_path, FileAccess::READ);
	if (file.is_null() || file->get_length() < 20 || file->get_32() != 0x46546C67 || file->get_32() != 2) {
		return false;
	}
	file->get_32(); // File length; the normal glTF parser validates the full input.
	const uint32_t json_length = file->get_32();
	if (file->get_32() != 0x4E4F534A || json_length > file->get_length() - 20) {
		return false;
	}
	const Vector<uint8_t> bytes = file->get_buffer(json_length);
	JSON json;
	if (json.parse(String::utf8((const char *)bytes.ptr(), bytes.size())) != OK || json.get_data().get_type() != Variant::DICTIONARY) {
		return false;
	}
	const Dictionary document = json.get_data();
	const Array buffers = document.get("buffers", Array());
	for (const Variant &value : buffers) {
		const Dictionary buffer = value;
		if (buffer.has("uri")) {
			return false;
		}
	}
	const Array images = document.get("images", Array());
	const int image_handling = p_options["gltf/embedded_image_handling"];
	if (!images.is_empty() && image_handling == GLTFState::HANDLE_BINARY_IMAGE_MODE_EXTRACT_TEXTURES) {
		return false;
	}
	for (const Variant &value : images) {
		const Dictionary image = value;
		if (image.has("uri")) {
			return false;
		}
	}
	return true;
}

} // namespace

void EditorSceneFormatImporterGLTF::_prepare_scene(void *p_data) {
	PreparedScene *prepared = static_cast<PreparedScene *>(p_data);
	ResourceLoader::set_is_import_thread(true);
	WGodotResourceTrace trace("import.gltf_prepare", prepared->path);
	prepared->scene = prepared->importer->_import_scene(prepared->path, prepared->flags, prepared->options, &prepared->missing_dependencies, &prepared->error);
	ResourceLoader::set_is_import_thread(false);
}

void EditorSceneFormatImporterGLTF::prepare_import_scene(const String &p_path, uint32_t p_flags, const HashMap<StringName, Variant> &p_options) {
	if (prepared_scenes.has(p_path) || !can_prepare_glb(p_path, p_options)) {
		return;
	}
	PreparedScene *prepared = memnew(PreparedScene);
	prepared->path = p_path;
	prepared->flags = p_flags;
	prepared->options = p_options;
	prepared->importer = this;
	prepared_scenes.insert(p_path, prepared);
	prepared->task = WorkerThreadPool::get_singleton()->add_native_task(&_prepare_scene, prepared, false, "Prepare glTF: " + p_path);
}

Node *EditorSceneFormatImporterGLTF::import_scene(const String &p_path, uint32_t p_flags, const HashMap<StringName, Variant> &p_options, List<String> *r_missing_deps, Error *r_err) {
	PreparedScene **entry = prepared_scenes.getptr(p_path);
	if (!entry) {
		return _import_scene(p_path, p_flags, p_options, r_missing_deps, r_err);
	}
	PreparedScene *prepared = *entry;
	wait_for_prepared_scene(prepared->task);
	Node *scene = prepared->scene;
	if (r_err) {
		*r_err = prepared->error;
	}
	if (r_missing_deps) {
		for (const String &dependency : prepared->missing_dependencies) {
			r_missing_deps->push_back(dependency);
		}
	}
	prepared_scenes.erase(p_path);
	memdelete(prepared);
	return scene;
}

void EditorSceneFormatImporterGLTF::finish_prepared_scenes() {
	// Validation may reject a source before import_scene consumes its result.
	for (const KeyValue<String, PreparedScene *> &entry : prepared_scenes) {
		PreparedScene *prepared = entry.value;
		wait_for_prepared_scene(prepared->task);
		if (prepared->scene) {
			memdelete(prepared->scene);
		}
		memdelete(prepared);
	}
	prepared_scenes.clear();
}
