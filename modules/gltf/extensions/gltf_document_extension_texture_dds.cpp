// wgodot-changes::file
#include "gltf_document_extension_texture_dds.h"

Error GLTFDocumentExtensionTextureDDS::import_preflight(Ref<GLTFState> p_state, const Vector<String> &p_extensions) {
	return p_extensions.has("MSFT_texture_dds") ? OK : ERR_SKIP;
}

Vector<String> GLTFDocumentExtensionTextureDDS::get_supported_extensions() {
	return { "MSFT_texture_dds" };
}

Error GLTFDocumentExtensionTextureDDS::parse_texture_json(Ref<GLTFState> p_state, const Dictionary &p_texture_json, Ref<GLTFTexture> r_gltf_texture) {
	const Dictionary extensions = p_texture_json.get("extensions", Dictionary());
	if (!extensions.has("MSFT_texture_dds")) {
		return OK;
	}
	const Dictionary dds = extensions["MSFT_texture_dds"];
	ERR_FAIL_COND_V(!dds.has("source"), ERR_PARSE_ERROR);
	const int index = dds["source"];
	const Array images = p_state->get_json().get("images", Array());
	ERR_FAIL_INDEX_V(index, images.size(), ERR_PARSE_ERROR);
	const Dictionary image = images[index];
	const String uri = image.get("uri", String());
	ERR_FAIL_COND_V_MSG(uri.is_empty() || uri.begins_with("data:") || uri.get_extension().to_lower() != "dds", ERR_UNAVAILABLE, "DDS textures must reference an external .dds resource.");
	r_gltf_texture->set_src_image(index);
	return OK;
}
