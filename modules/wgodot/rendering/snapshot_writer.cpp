// wgodot-changes::file
#include "render_capture.h"
#include "scene_snapshot.h"
#include "gpu_profile.h"

#ifdef TOOLS_ENABLED
#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/string/string_builder.h"

namespace {
struct SnapshotSections {
	static constexpr uint32_t SECTIONS_PER_CHUNK = 32;
	const ConfigFile *snapshot = nullptr;
	Vector<String> sections;
	uint32_t first = 0;
	CharString *results = nullptr;

	static void encode(void *p_userdata, uint32_t p_index) {
		SnapshotSections &work = *static_cast<SnapshotSections *>(p_userdata);
		const uint32_t first = work.first + p_index * SECTIONS_PER_CHUNK;
		const uint32_t end = MIN(first + SECTIONS_PER_CHUNK, uint32_t(work.sections.size()));
		StringBuilder text;
		for (uint32_t i = first; i < end; i++) {
			if (i > 0) {
				text.append("\n");
			}
			// Encode immutable fields directly, with Godot's escaping/precision.
			// Do not build a second ConfigFile and reinsert every captured field.
			work.snapshot->encode_section_to_text(work.sections[i], text);
		}
		work.results[p_index] = text.as_string().utf8();
	}
};

Error write_snapshot(const Ref<WSceneSnapshot> &p_snapshot, const String &p_path, bool p_parallel) {
	WGodotGPUProfile::Scope profile_scope("capture.serialize_snapshot");
	ERR_FAIL_COND_V(p_snapshot.is_null(), ERR_INVALID_PARAMETER);
	Ref<ConfigFile> values;
	{
		WGodotGPUProfile::Scope materialize_scope("capture.materialize_snapshot");
		values = p_snapshot->materialize();
	}
	ERR_FAIL_COND_V(values.is_null(), ERR_CANT_ACQUIRE_RESOURCE);
	SnapshotSections work;
	work.snapshot = values.ptr();
	for (const String &section : p_snapshot->get_sections()) {
		// ConfigFile's nil assignment can remove the last field of a section.
		if (values->has_section(section)) {
			work.sections.push_back(section);
		}
	}
	WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
	// Retain one bounded chunk per available worker, not a second encoded
	// copy of the entire recording. The ordered writes preserve ConfigFile's
	// format exactly, including escaped section names and Variant precision.
	Vector<CharString> results;
	results.resize(p_parallel ? MAX(1, pool->get_thread_count()) : 1);
	work.results = results.ptrw();
	const String temporary = p_path + ".tmp";
	Error error = OK;
	Ref<FileAccess> file = FileAccess::open(temporary, FileAccess::WRITE, &error);
	if (error != OK) {
		return error;
	}
	while (work.first < uint32_t(work.sections.size()) && error == OK) {
		const uint32_t remaining = uint32_t(work.sections.size()) - work.first;
		const uint32_t count = MIN(uint32_t(results.size()), (remaining + SnapshotSections::SECTIONS_PER_CHUNK - 1) / SnapshotSections::SECTIONS_PER_CHUNK);
		if (p_parallel && count > 1) {
			WorkerThreadPool::GroupID task = pool->add_native_group_task(
					&SnapshotSections::encode, &work, count, -1, true, "Serialize capture sections");
			pool->wait_for_group_task_completion(task);
		} else {
			SnapshotSections::encode(&work, 0);
		}
		for (uint32_t i = 0; i < count; i++) {
			CharString &result = work.results[i];
			if (!file->store_buffer(reinterpret_cast<const uint8_t *>(result.get_data()), result.length())) {
				error = ERR_FILE_CANT_WRITE;
				break;
			}
			result = CharString();
		}
		work.first += count * SnapshotSections::SECTIONS_PER_CHUNK;
	}
	file->flush();
	if (error == OK) {
		error = file->get_error();
	}
	file.unref();
	if (error == OK) {
		error = DirAccess::rename_absolute(temporary, p_path);
	}
	if (error != OK) {
		DirAccess::remove_absolute(temporary);
	}
	return error;
}

struct SnapshotBatch {
	Vector<Ref<WSceneSnapshot>> snapshots;
	PackedStringArray paths;
	int32_t *errors = nullptr;
	static void write(void *p_userdata, uint32_t p_index) {
		SnapshotBatch &batch = *static_cast<SnapshotBatch *>(p_userdata);
		// Parallelize whole snapshots; nested per-section task groups would
		// add scheduling barriers and retain needless intermediate work.
		batch.errors[p_index] = write_snapshot(batch.snapshots[p_index], batch.paths[p_index], false);
	}
};
}

Error WRenderCapture::save_snapshot(const Ref<WSceneSnapshot> &p_snapshot, const String &p_path) {
	return write_snapshot(p_snapshot, p_path, true);
}

PackedInt32Array WRenderCapture::save_snapshots(const TypedArray<WSceneSnapshot> &p_snapshots, const PackedStringArray &p_paths) {
	ERR_FAIL_COND_V(p_snapshots.size() != p_paths.size(), PackedInt32Array());
	SnapshotBatch batch;
	batch.paths = p_paths;
	HashSet<ObjectID> identities;
	HashSet<String> destinations;
	for (int i = 0; i < p_snapshots.size(); i++) {
		Ref<WSceneSnapshot> snapshot = p_snapshots[i];
		ERR_FAIL_COND_V_MSG(snapshot.is_null() || identities.has(snapshot->get_instance_id()) || destinations.has(p_paths[i]), PackedInt32Array(), "Parallel snapshot writes require exclusive snapshots and distinct paths.");
		identities.insert(snapshot->get_instance_id());
		destinations.insert(p_paths[i]);
		batch.snapshots.push_back(snapshot);
	}
	PackedInt32Array errors;
	errors.resize(batch.snapshots.size());
	batch.errors = errors.ptrw();
	if (!batch.snapshots.is_empty()) {
		WorkerThreadPool *pool = WorkerThreadPool::get_singleton();
		WorkerThreadPool::GroupID task = pool->add_native_group_task(&SnapshotBatch::write, &batch, batch.snapshots.size(), -1, true, "Write captured scene snapshots");
		pool->wait_for_group_task_completion(task);
	}
	return errors;
}
#endif
