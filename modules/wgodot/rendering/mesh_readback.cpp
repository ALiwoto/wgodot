// wgodot-changes::file
#include "mesh_readback.h"
#ifdef TOOLS_ENABLED
#include "device_capture/recorder.h"
#include "gpu_profile.h"
#include "core/object/callable_mp.h"
#include "core/io/marshalls.h"
#include "servers/rendering/rendering_server.h"

Ref<WMeshBoundsReadback> WMeshBoundsReadback::create(const Ref<Mesh> &p_mesh) {
	ERR_FAIL_COND_V(p_mesh.is_null(), Ref<WMeshBoundsReadback>());
	Ref<WMeshBoundsReadback> request;
	request.instantiate();
	request->mesh = p_mesh;
	RenderingServer::get_singleton()->call_on_render_thread(callable_mp_static(begin).bind(request, p_mesh->get_rid()));
	return request;
}

void WMeshBoundsReadback::finish_part() {
	if (--pending == 0) {
		completed.set();
	}
}

void WMeshBoundsReadback::received(const PackedByteArray &p_bytes, int p_surface, bool p_indices, Ref<WMeshBoundsReadback> p_owner) {
	Surface &surface = surfaces.write[p_surface];
	(p_indices ? surface.indices : surface.vertices) = p_bytes;
	if (p_bytes.is_empty()) {
		error = ERR_CANT_ACQUIRE_RESOURCE;
	}
	finish_part();
}

void WMeshBoundsReadback::begin(Ref<WMeshBoundsReadback> p_request, RID p_mesh) {
	WGodotGPUProfile::Scope scope("capture.queue_mesh_readback");
	WGPUCapture::Recorder::DiagnosticScope diagnostic_commands;
	RendererRD::MeshStorage *storage = RendererRD::MeshStorage::get_singleton();
	RD *device = RD::get_singleton();
	if (!storage || !device) {
		p_request->error = ERR_UNAVAILABLE;
		p_request->finish_part();
		return;
	}
	for (int i = 0; i < storage->mesh_get_surface_count(p_mesh); i++) {
		const auto descriptor = storage->mesh_surface_geometry_readback(p_mesh, i);
		if ((descriptor.primitive != RSE::PRIMITIVE_TRIANGLES && descriptor.primitive != RSE::PRIMITIVE_TRIANGLE_STRIP) ||
				(descriptor.format & RSE::ARRAY_FLAG_USES_EMPTY_VERTEX_ARRAY) || descriptor.vertex_count == 0) {
			continue;
		}
		Surface surface;
		surface.descriptor = descriptor;
		const int index = p_request->surfaces.size();
		p_request->surfaces.push_back(surface);
		for (bool indices : { false, true }) {
			if (indices && descriptor.index_count == 0) {
				continue;
			}
			p_request->pending++;
			const Error error = device->buffer_get_data_async(indices ? descriptor.indices : descriptor.vertices,
					callable_mp(p_request.ptr(), &WMeshBoundsReadback::received).bind(index, indices, p_request));
			if (error != OK) {
				p_request->error = error;
				p_request->finish_part();
			}
		}
	}
	// Hold one sentinel until every callback has been enqueued.
	p_request->finish_part();
}

Error WMeshBoundsReadback::decode() {
	WGodotGPUProfile::Scope scope("capture.decode_mesh_bounds");
	bool initialized = false;
	for (const Surface &surface : surfaces) {
		const auto &d = surface.descriptor;
		const bool indexed = d.index_count > 0;
		const uint32_t count = indexed ? d.index_count : d.vertex_count;
		ERR_FAIL_COND_V(count < 3 || (d.primitive == RSE::PRIMITIVE_TRIANGLES && count % 3), ERR_INVALID_DATA);
		uint32_t offsets[RSE::ARRAY_MAX];
		uint32_t vertex_stride, normal_stride, attribute_stride, skin_stride;
		RenderingServer::get_singleton()->mesh_surface_make_offsets_from_format(d.format, d.vertex_count, d.index_count,
				offsets, vertex_stride, normal_stride, attribute_stride, skin_stride);
		const bool two_d = d.format & RSE::ARRAY_FLAG_USE_2D_VERTICES;
		const bool compressed = !two_d && (d.format & RSE::ARRAY_FLAG_COMPRESS_ATTRIBUTES);
		const uint32_t position_bytes = two_d ? 8 : (compressed ? 6 : 12);
		const uint32_t index_stride = RenderingServer::get_singleton()->mesh_surface_get_format_index_stride(BitField<RSE::ArrayFormat>(d.format), d.vertex_count);
		ERR_FAIL_COND_V(indexed && index_stride != 2 && index_stride != 4, ERR_INVALID_DATA);
		ERR_FAIL_COND_V(uint64_t(vertex_stride) * (d.vertex_count - 1) + offsets[RSE::ARRAY_VERTEX] + position_bytes > uint64_t(surface.vertices.size()), ERR_INVALID_DATA);
		ERR_FAIL_COND_V(indexed && uint64_t(index_stride) * count > uint64_t(surface.indices.size()), ERR_INVALID_DATA);
		const uint8_t *indices = surface.indices.ptr();
		const uint8_t *vertices = surface.vertices.ptr();
		for (uint32_t i = 0; i < count; i++) {
			uint32_t vertex = i;
			if (indexed) {
				vertex = index_stride == 2 ? decode_uint16(indices + i * 2) : decode_uint32(indices + i * 4);
			}
			ERR_FAIL_UNSIGNED_INDEX_V(vertex, d.vertex_count, ERR_INVALID_DATA);
			const uint8_t *position = vertices + uint64_t(vertex) * vertex_stride + offsets[RSE::ARRAY_VERTEX];
			Vector3 value;
			if (compressed) {
				value = Vector3(float(decode_uint16(position)) / 65535.0, float(decode_uint16(position + 2)) / 65535.0, float(decode_uint16(position + 4)) / 65535.0);
				value = value * d.aabb.size + d.aabb.position;
			} else {
				value = Vector3(decode_float(position), decode_float(position + 4), two_d ? 0.0f : decode_float(position + 8));
			}
			if (initialized) {
				bounds.expand_to(value);
			} else {
				bounds = AABB(value, Vector3());
				initialized = true;
			}
		}
	}
	return OK;
}

Error WMeshBoundsReadback::get_bounds(AABB &r_bounds) {
	ERR_FAIL_COND_V(!ready(), ERR_BUSY);
	if (!decoded) {
		if (error == OK) {
			error = decode();
		}
		surfaces.clear();
		mesh.unref();
		decoded = true;
	}
	r_bounds = bounds;
	return error;
}
#endif
