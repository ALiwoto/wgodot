// wgodot-changes::file
#pragma once

#include "core/templates/local_vector.h"
#include "servers/rendering/rendering_device_driver.h"

#include "modules/modules_enabled.gen.h"
#if defined(TOOLS_ENABLED) && defined(MODULE_WGODOT_ENABLED)
#include "modules/wgodot/rendering/gpu_profile.h"
#endif

// A completed download can contain many regions in the same staging buffer.
// Keep its mapping until the CPU copy is complete, then release every mapping
// before invoking the caller's callback (which may submit more device work).
class RenderingDeviceReadbackMappings {
	struct Mapping {
		RenderingDeviceDriver::BufferID buffer;
		uint8_t *data = nullptr;
	};
	RenderingDeviceDriver *driver;
	LocalVector<Mapping> mappings;

public:
	explicit RenderingDeviceReadbackMappings(RenderingDeviceDriver *p_driver) :
			driver(p_driver) {}
	RenderingDeviceReadbackMappings(const RenderingDeviceReadbackMappings &) = delete;
	RenderingDeviceReadbackMappings &operator=(const RenderingDeviceReadbackMappings &) = delete;

	uint8_t *get(RenderingDeviceDriver::BufferID p_buffer) {
		for (const Mapping &mapping : mappings) {
			if (mapping.buffer == p_buffer) {
				return mapping.data;
			}
		}
#if defined(TOOLS_ENABLED) && defined(MODULE_WGODOT_ENABLED)
		WGodotGPUProfile::Scope profile_scope("device.map_readback_buffer");
#endif
		uint8_t *data = driver->buffer_map(p_buffer);
		if (data) {
			mappings.push_back({ p_buffer, data });
		}
		return data;
	}

	~RenderingDeviceReadbackMappings() {
#if defined(TOOLS_ENABLED) && defined(MODULE_WGODOT_ENABLED)
		WGodotGPUProfile::Scope profile_scope("device.unmap_readback_buffers");
#endif
		for (const Mapping &mapping : mappings) {
			driver->buffer_unmap(mapping.buffer);
		}
	}
};
