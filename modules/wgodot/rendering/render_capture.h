// wgodot-changes::file
#pragma once

#include "modules/modules_enabled.gen.h"

#ifdef TOOLS_ENABLED
#include "capture_payload.h"
#include "core/io/config_file.h"
#include "core/io/file_access.h"
#include "core/object/ref_counted.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/os/condition_variable.h"
#include "core/os/semaphore.h"
#include "core/os/thread.h"
#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "core/templates/list.h"
#include "core/templates/safe_refcount.h"
#include "core/variant/typed_array.h"
#include "scene/resources/material.h"

class RenderDataRD;
class GeometryInstance3D;
class Node;
class Node3D;
class MeshInstance3D;
class Skeleton3D;
class ParticleInstance3D;
class Viewport;
class Image;
class WCapturePaths;
class WSceneSnapshot;
class Mesh;

// Opt-in, lossless, pre-UI measurements of the actual renderer. No node-type
// enumeration: private particle instances and every submitted surface count.
class WRenderCapture : public RefCounted {
	GDCLASS(WRenderCapture, RefCounted);
	struct PaletteBatch : public RefCounted {
		GDSOFTCLASS(PaletteBatch, RefCounted);

	public:
		HashSet<RID> skins;
		Ref<ConfigFile> result;
		Semaphore completed;

		void collect(Node *p_node, Viewport *p_viewport);
		static Ref<PaletteBatch> create(const TypedArray<Node> &p_roots);
		static void read(Ref<PaletteBatch> p_batch);
	};
	struct Plane {
		String name;
		int width = 0;
		int height = 0;
		int format = 0;
		PackedByteArray bytes;
		int shuffle_stride = 0;
		String file;
		uint64_t entry = 0;
		WCapturePayload::Tiles tiles;
		WCapturePayload::Packet packet;
		int64_t byte_count = 0;
		int depth = 1;
		int layer = 0;
		PackedInt64Array mip_offsets;
		PackedInt64Array mip_sizes;
		Error error = OK;
		bool buffer = false;
		bool texture_input = false;
		uint64_t read_usec = 0;
		uint64_t write_usec = 0;
		uint64_t file_usec = 0;
		bool reused = false;
		bool needs_write = false;
		int alias = -1;
	};
	struct ReadbackFrame : public RefCounted {
		GDSOFTCLASS(ReadbackFrame, RefCounted);

	public:
		Vector<Plane> planes;
		Ref<ConfigFile> metadata;
		int index = 0;
		int pending = 0;
		uint64_t readbacks_finished_usec = 0;
		WRenderCapture *owner = nullptr;
		SafeFlag written;
		uint64_t readback_bytes = 0;
		uint64_t write_bytes = 0;
	};
	struct OutputPayload {
		PackedByteArray bytes;
		String file;
		int shuffle_stride = 0;
		uint64_t entry = 0;
		WCapturePayload::Tiles tiles;
	};
	struct MaterialSnapshot {
		Ref<Shader> shader;
		uint64_t revision = 0;
		WShaderParameterBuffer parameters;
		Ref<ShaderMaterial> frozen;
	};
	HashMap<ObjectID, MaterialSnapshot> material_snapshots;
	HashMap<String, OutputPayload> output_payloads;
	WCapturePayload::Writer payload_writer;
	Thread spool_thread;
	BinaryMutex spool_mutex;
	ConditionVariable spool_changed;
	List<Ref<ReadbackFrame>> spool_pending;
	Vector<Ref<ReadbackFrame>> metadata_frames;
	bool spool_closing = false;
	uint64_t spool_budget_bytes = 0;
	SafeNumeric<uint64_t> spool_pending_bytes;
	SafeNumeric<uint64_t> spool_peak_bytes;
	SafeNumeric<uint64_t> pending_readback_bytes;
	SafeNumeric<uint64_t> retained_payload_bytes;
	SafeNumeric<uint64_t> queue_peak_bytes;
	uint64_t next_payload_entry = 1;
	HashMap<RID, String> particle_sources;
	static HashMap<RID, Ref<WRenderCapture>> captures;
	RID viewport;
	RID target;
	String directory;
	bool passes = false;
	int64_t sample_frame = -1;
	double sample_seconds = 0;
	double sample_render_time = -1;
	double sample_time_step = 0;
	int frame_index = 0;
	Ref<ReadbackFrame> readback_frame;
	List<Ref<ReadbackFrame>> readback_frames;
	HashMap<RID, RID> readback_textures;
	Ref<PaletteBatch> palette_request;
	Ref<ConfigFile> frame_palettes;
	mutable Mutex palette_mutex;
	// Planning preserves frame reuse order. A single spool owns the archive;
	// disk waits never occupy the CPU pool or let later writes overtake earlier ones.
	Ref<ReadbackFrame> planning_frame;
	WorkerThreadPool::TaskID planning_task = WorkerThreadPool::INVALID_TASK_ID;
	List<Ref<ReadbackFrame>> writers;
	uint64_t storage_wait_usec = 0;
	uint64_t storage_wait_max_usec = 0;
	uint64_t storage_wait_count = 0;
	uint64_t sampling_wait_usec = 0;
	uint64_t sampling_wait_max_usec = 0;
	uint64_t sampling_wait_count = 0;
	SafeFlag completed;
	bool ending = false; // Render-thread owned; excludes this capture from new draws.
	bool finalizing = false;
	Thread finish_thread;
	SafeNumeric<int> failure;
	SafeNumeric<int> submitted_frame_count;

	void _begin();
	static void _begin_capture(Ref<WRenderCapture> p_capture);
	void _end();
	void _finish_readbacks();
	static void _finish_cpu(void *p_userdata);
	void _finalize_files();
	static void _complete_end(Ref<WRenderCapture> p_capture);
	static void _end_all();
	void _queue_skeleton_palettes(Ref<PaletteBatch> p_batch);
	void _capture_skeleton_palettes();
	void _set_sample(int64_t p_frame, double p_seconds, double p_render_time, double p_time_step, bool p_detailed_inputs);
	void _track_particles(RID p_particles, const String &p_path);
	void _read_particles(const Ref<ConfigFile> &p_frame);
	void _read_texture(const String &p_name, RID p_texture);
	void _texture_readback(const PackedByteArray &p_bytes, Ref<ReadbackFrame> p_frame, int p_index);
	void _pump_frames(bool p_wait);
	void _limit_queued_bytes();
	uint64_t _queued_bytes() const;
	uint64_t _collect_readbacks();
	void _write_plane(ReadbackFrame *p_frame, uint32_t p_index);
	void _plan_write(ReadbackFrame *p_frame);
	void _compare_plane(uint32_t p_index, Plane *p_planes) const;
	static void _plan_write_task(void *p_userdata);
	static void _prepare_plane_task(void *p_userdata, uint32_t p_index);
	void _store_plane(Plane &p_plane);
	void _drain_write(bool p_all = true);
	void _write_frame(ReadbackFrame *p_frame);
	static void _spool_entry(void *p_userdata);
	void _spool_frames();
	static WRenderCapture *_find(const RenderDataRD *p_data);

protected:
	static void _bind_methods();

public:
	// Shutdown only: retire captures before the renderer, journal and pool disappear.
	static void finish_all();
	static Error finish_device_capture();
	static Error store_image(const Ref<Image> &p_image, const String &p_path);
	// Worker task only. Resolves GPU data asynchronously and yields the CPU
	// worker while the copy is pending; conversion runs on the calling worker.
	static Ref<Image> read_texture_image(RID p_texture);
	// Scene teardown only: keep GPU callbacks retiring while joining a writer.
	static void finish_write_task(WorkerThreadPool::TaskID p_task);
	// The caller transfers exclusive, immutable snapshot ownership for the
	// duration of this call. Serialization is parallel and memory-bounded.
	static Error save_snapshot(const Ref<WSceneSnapshot> &p_snapshot, const String &p_path);
	static PackedInt32Array save_snapshots(const TypedArray<WSceneSnapshot> &p_snapshots, const PackedStringArray &p_paths);
	static TypedArray<Node> expand_snapshot_children(Node *p_root, Node *p_parent, const Ref<WSceneSnapshot> &p_output, const Ref<WCapturePaths> &p_paths, bool p_world);
	// Opt-in capture helpers. Frozen resources are reused only for unchanged
	// state of the same live material; no asset-content hashing is involved.
	Ref<ShaderMaterial> snapshot_material(const Ref<ShaderMaterial> &p_material);
	static Ref<ConfigFile> skeleton_palettes(const TypedArray<Node> &p_roots);
	// Collect on the main thread before requesting a render. Read at that
	// render's capture boundary; the submission counter publishes the result.
	void queue_skeleton_palettes(const TypedArray<Node> &p_roots);
	Ref<ConfigFile> get_skeleton_palettes() const;
	static void instance_uniforms(GeometryInstance3D *p_instance, const Ref<WSceneSnapshot> &p_output, const String &p_section, const String &p_prefix = String());
	static Transform3D render_transform(Node3D *p_node);
	static bool render_visible(Node3D *p_node);
	static void write_skeleton(Skeleton3D *p_skeleton, const Ref<WSceneSnapshot> &p_output, const String &p_section);
	static void write_skeletons(Node *p_root, Node *p_node, const String &p_frame, const Ref<WSceneSnapshot> &p_output);
	static void write_skeleton_list(Node *p_root, const TypedArray<Skeleton3D> &p_skeletons, const String &p_frame, const Ref<WSceneSnapshot> &p_output);
	static void write_spatial(Node3D *p_node, const Ref<WSceneSnapshot> &p_output, const String &p_section);
	static void write_transforms(const TypedArray<Node3D> &p_nodes, const Ref<WSceneSnapshot> &p_output, const String &p_section);
	static void write_mesh(MeshInstance3D *p_node, const Ref<WSceneSnapshot> &p_output, const String &p_section, const Ref<ConfigFile> &p_palettes);
#ifdef MODULE_PARTICLE_EFFECTS_ENABLED
	static void write_particle_transforms(const TypedArray<ParticleInstance3D> &p_particles, const Ref<WSceneSnapshot> &p_output, const String &p_section);
	static void write_particle(ParticleInstance3D *p_particle, const Ref<WSceneSnapshot> &p_output, const String &p_section);
#endif
	static Ref<WRenderCapture> begin(RID p_viewport, const String &p_directory, bool p_passes = false);
	void set_sample(int64_t p_frame, double p_seconds, double p_render_time = -1, double p_time_step = 0, bool p_detailed_inputs = false);
	// Register once on scene membership changes; an empty path unregisters.
	void track_particles(RID p_particles, const String &p_path);
	// GPU copies have been queued at their pass boundaries. The sampler can
	// advance; immutable readbacks and file publication complete independently.
	int get_submitted_frame_count() const { return submitted_frame_count.get(); }
	void end();
	bool is_completed() const { return completed.is_set(); }
	int get_error() const { return failure.get(); }
	static void stage(const RenderDataRD *p_data, const String &p_name, bool p_multisample = false);
	static void texture(const RenderDataRD *p_data, const String &p_name, RID p_texture);
	static bool prepare_frame(const RenderDataRD *p_data);
	static void finish_frame(const RenderDataRD *p_data);
};
#endif
