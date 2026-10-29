// wgodot-changes::file
#pragma once

#include "gltf_document_extension.h"

// DDS stays an external, source-owned Texture2D. ResourceLoader retains its
// GPU blocks and authored mip chain; no extracted PNG or regenerated mipmaps.
class GLTFDocumentExtensionTextureDDS : public GLTFDocumentExtension {
	GDCLASS(GLTFDocumentExtensionTextureDDS, GLTFDocumentExtension);

public:
	bool supports_threaded_import() const override { return true; }
	Error import_preflight(Ref<GLTFState> p_state, const Vector<String> &p_extensions) override;
	Vector<String> get_supported_extensions() override;
	Error parse_texture_json(Ref<GLTFState> p_state, const Dictionary &p_texture_json, Ref<GLTFTexture> r_gltf_texture) override;
};
