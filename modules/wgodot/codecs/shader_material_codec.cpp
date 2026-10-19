// wgodot-changes::file
#include "shader_material_codec.h"

#include "resource_codec.h"
#include "scene/resources/material.h"
#include "servers/rendering/storage/wgodot_shader_parameter_io.h"

namespace {
class ShaderMaterialCodec : public WResourceCodec {
public:
	bool owns_property(const StringName &p_name) const override { return false; }
	bool owns_native_property(const StringName &p_name) const override {
		return String(p_name).begins_with("shader_parameter/");
	}
	bool requires_native_factory() const override { return true; }
	bool owns_assignment(const String &p_name) const override { return false; }
	Error read(Resource *, VariantParser::Token &, VariantParser::Stream *, int &, String &, VariantParser::ResourceParser *) const override {
		return ERR_UNAVAILABLE; // The normal Resource property reader owns the text format.
	}
	Error write(const Resource *, String &r_text, VariantWriter::EncodeResourceFunc, void *) const override {
		r_text = String();
		return OK;
	}

	Error write_native(const Resource *p_resource, const String &p_target, NativeWriter &p_writer) const override {
		const ShaderMaterial *material = Object::cast_to<ShaderMaterial>(p_resource);
		ERR_FAIL_NULL_V(material, ERR_INVALID_DATA);
		if (material->get_shader().is_null()) {
			return OK;
		}
		ERR_FAIL_COND_V(material->get_shader()->get_parameter_layout().is_null(), ERR_INVALID_DATA);
		const WShaderParameters &parameters = material->get_shader_parameters();
		ERR_FAIL_COND_V(parameters.layout.is_null(), ERR_INVALID_DATA);
		p_writer.include("scene/resources/material.h");
		String default_slots;
		for (int slot = 0; slot < parameters.layout->entries.size(); slot++) {
			if (!parameters.buffer.overridden[slot]) {
				continue;
			}
			const auto &entry = parameters.layout->entries[slot];
			if (entry.texture >= 0) {
				String value;
				if (entry.array) {
					value = p_writer.symbol();
					p_writer.line("Vector<Ref<Texture>> " + value + ";");
					const int count = parameters.buffer.texture_counts[slot];
					p_writer.line(value + ".resize(" + itos(count) + ");");
					for (int index = 0; index < count; index++) {
						const Ref<Texture> texture = material->get_shader_texture_at(slot, index);
						if (texture.is_valid()) {
							const String reference = p_writer.value(texture);
							p_writer.line(value + ".write[" + itos(index) + "] = " + reference + ";");
						}
					}
				} else {
					value = "Ref<Texture>(" + p_writer.value(material->get_shader_texture_at(slot)) + ")";
				}
				p_writer.line(p_target + "->set_shader_parameter_at(" + itos(slot) + ", " + value + ");");
				continue;
			}
			const auto &defaults = parameters.layout->defaults;
			const size_t bytes = entry.stride * entry.count * sizeof(uint32_t);
			const bool same_default = parameters.buffer.colors[slot] == defaults.colors[slot] &&
					memcmp(parameters.buffer.words.ptr() + entry.offset, defaults.words.ptr() + entry.offset, bytes) == 0 &&
					memcmp(parameters.buffer.linear_words.ptr() + entry.offset, defaults.linear_words.ptr() + entry.offset, bytes) == 0;
			if (same_default) {
				if (!default_slots.is_empty()) {
					default_slots += ", ";
				}
				default_slots += itos(slot);
				continue;
			}
			// Variant is only the export-time codec boundary. The expression and
			// instantiated setter in the generated factory are concrete C++ types.
			const String value = p_writer.value(WShaderParameterIO::read(parameters, slot));
			p_writer.line(p_target + "->set_shader_parameter_at(" + itos(slot) + ", " + value + ");");
		}
		if (!default_slots.is_empty()) {
			// An explicitly authored default must survive a later shader change.
			p_writer.line(p_target + "->mark_shader_parameter_overrides({ " + default_slots + " });");
		}
		return OK;
	}
};

ShaderMaterialCodec shader_material_codec;
} // namespace

void register_shader_material_codec() {
	WResourceCodec::register_codec("ShaderMaterial", &shader_material_codec);
}

void unregister_shader_material_codec() {
	WResourceCodec::unregister_codec("ShaderMaterial");
}
