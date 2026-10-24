// wgodot-changes::file
#pragma once

#ifdef TOOLS_ENABLED
#include "core/io/marshalls.h"
#include "servers/rendering/rendering_device.h"
#include <type_traits>

namespace WGPUCapture {
enum class Command : uint32_t {
#define GPU_COMMAND(id, name, fields) id,
#include "commands.inc"
#undef GPU_COMMAND
	COUNT
};

// These tags describe the wire encoding only. Runtime arguments remain native
// C++ values, selected by overloads; there is no Variant or property lookup.
enum class Value : uint8_t { BOOL, SIGNED, UNSIGNED, FLOAT, DOUBLE, RID, STRING, BYTES, ARRAY, STRUCT };
enum class Structure : uint32_t {
#define GPU_STRUCTURE(id, fields) id,
#include "structures.inc"
#undef GPU_STRUCTURE
	COUNT
};
enum class BufferKind : uint8_t { STORAGE, UNIFORM, VERTEX, INDEX, TEXEL };

struct Bytes {
	const void *data = nullptr;
	uint32_t size = 0;
};

class Encoder {
	Vector<uint8_t> owned;
	void raw(const void *p_data, uint32_t p_size);
	void tag(Value p_type);

public:
	Vector<uint8_t> &bytes;
	Encoder() : bytes(owned) {}
	explicit Encoder(Vector<uint8_t> &p_destination) : bytes(p_destination) {}
	void u32(uint32_t p_value);
	void u64(uint64_t p_value);
	void put(bool p_value);
	void put(float p_value);
	void put(double p_value);
	void put(RID p_value);
	void put(const String &p_value);
	void put(const char *p_value) { put(String::utf8(p_value)); }
	void put(Bytes p_value);
	void put(const Vector<uint8_t> &p_value) { put(Bytes{ p_value.ptr(), uint32_t(p_value.size()) }); }
	void put(Span<uint8_t> p_value) { put(Bytes{ p_value.ptr(), uint32_t(p_value.size()) }); }
	template <typename T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>, int> = 0>
	void put(T p_value) {
		tag(std::is_signed_v<T> ? Value::SIGNED : Value::UNSIGNED);
		u64(uint64_t(p_value));
	}
	template <typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
	void put(T p_value) { put(static_cast<std::underlying_type_t<T>>(p_value)); }
	template <typename T>
	void put(BitField<T> p_value) { put(uint64_t(p_value)); }
	template <typename T>
	void sequence(const T &p_values) {
		tag(Value::ARRAY);
		u32(p_values.size());
		for (uint32_t index = 0; index < p_values.size(); index++) {
			put(p_values[index]);
		}
	}
	template <typename T>
	void put(const Vector<T> &p_values) { sequence(p_values); }
	template <typename T>
	void put(const LocalVector<T> &p_values) { sequence(p_values); }
	template <typename T>
	void put(Span<T> p_values) { sequence(p_values); }
	template <typename T>
	void put(const VectorView<T> &p_values) { sequence(p_values); }
	template <typename... T>
	void structure(Structure p_type, const T &...p_fields) {
		tag(Value::STRUCT);
		u32(uint32_t(p_type));
		u32(sizeof...(T));
		(put(p_fields), ...);
	}
	void put(const RenderingDevice::TextureFormat &v);
	void put(const RenderingDevice::TextureView &v);
	void put(const RenderingDevice::SamplerState &v);
	void put(const RenderingDevice::VertexAttribute &v);
	void put(const RenderingDevice::FramebufferPass &v);
	void put(const RenderingDevice::AttachmentFormat &v);
	void put(const RenderingDevice::Uniform &v);
	void put(const RenderingDevice::PipelineRasterizationState &v);
	void put(const RenderingDevice::PipelineMultisampleState &v);
	void put(const RenderingDevice::PipelineDepthStencilState &v);
	void put(const RenderingDevice::PipelineDepthStencilState::StencilOperationState &v);
	void put(const RenderingDevice::PipelineColorBlendState &v);
	void put(const RenderingDevice::PipelineColorBlendState::Attachment &v);
	void put(const RenderingDevice::PipelineSpecializationConstant &v);
	void put(const Vector2 &v) { structure(Structure::VECTOR2, v.x, v.y); }
	void put(const Vector2i &v) { structure(Structure::VECTOR2I, v.x, v.y); }
	void put(const Vector3 &v) { structure(Structure::VECTOR3, v.x, v.y, v.z); }
	void put(const Rect2 &v) { structure(Structure::RECT2, v.position, v.size); }
	void put(const Rect2i &v) { structure(Structure::RECT2I, v.position, v.size); }
	void put(const Color &v) { structure(Structure::COLOR, v.r, v.g, v.b, v.a); }
};
}
#endif
