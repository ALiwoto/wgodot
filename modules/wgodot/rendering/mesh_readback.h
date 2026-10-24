// wgodot-changes::file
#pragma once
#ifdef TOOLS_ENABLED
#include "core/object/ref_counted.h"
#include "core/templates/safe_refcount.h"
#include "scene/resources/mesh.h"
#include "servers/rendering/renderer_rd/storage_rd/mesh_storage.h"

// One immutable geometry sample, shared by instances in the same snapshot.
// The render thread owns collection. After ready(), a writer owns decoding.
class WMeshBoundsReadback : public RefCounted {
	GDSOFTCLASS(WMeshBoundsReadback, RefCounted);
	struct Surface {
		RendererRD::MeshStorage::SurfaceGeometryReadback descriptor;
		PackedByteArray vertices, indices;
	};
	Vector<Surface> surfaces;
	Ref<Mesh> mesh;
	SafeFlag completed;
	uint32_t pending = 1;
	Error error = OK;
	bool decoded = false;
	AABB bounds;
	void finish_part();
	void received(const PackedByteArray &p_bytes, int p_surface, bool p_indices, Ref<WMeshBoundsReadback> p_owner);
	static void begin(Ref<WMeshBoundsReadback> p_request, RID p_mesh);
	Error decode();

public:
	static Ref<WMeshBoundsReadback> create(const Ref<Mesh> &p_mesh);
	bool ready() const { return completed.is_set(); }
	Error get_bounds(AABB &r_bounds);
};
#endif
