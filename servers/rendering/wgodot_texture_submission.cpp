// wgodot-changes::file
#include "rendering_server_default.h"

namespace {

Ref<Image> snapshot_image(const Ref<Image> &p_image) {
	// Retaining an Image reference does not retain its pixel contents. Font
	// atlases, procedural textures and callers may mutate it after submission.
	// Image duplication shares copy-on-write pixel storage until that mutation.
	return p_image.is_valid() ? Ref<Image>(p_image->duplicate()) : Ref<Image>();
}

Vector<Ref<Image>> snapshot_images(const Vector<Ref<Image>> &p_images) {
	Vector<Ref<Image>> snapshots;
	snapshots.resize(p_images.size());
	for (int i = 0; i < p_images.size(); ++i) {
		snapshots.write[i] = snapshot_image(p_images[i]);
	}
	return snapshots;
}

} // namespace

RID RenderingServerDefault::texture_2d_create(const Ref<Image> &p_image) {
	const RID texture = RSG::texture_storage->texture_allocate();
	if (is_on_render_thread() || RSG::rasterizer->can_create_resources_async()) {
		RSG::texture_storage->texture_2d_initialize(texture, p_image);
	} else {
		command_queue.push(RSG::texture_storage, &RendererTextureStorage::texture_2d_initialize, texture, snapshot_image(p_image));
	}
	return texture;
}

RID RenderingServerDefault::texture_2d_layered_create(const Vector<Ref<Image>> &p_layers, RSE::TextureLayeredType p_type) {
	const RID texture = RSG::texture_storage->texture_allocate();
	if (is_on_render_thread() || RSG::rasterizer->can_create_resources_async()) {
		RSG::texture_storage->texture_2d_layered_initialize(texture, p_layers, p_type);
	} else {
		command_queue.push(RSG::texture_storage, &RendererTextureStorage::texture_2d_layered_initialize, texture, snapshot_images(p_layers), p_type);
	}
	return texture;
}

RID RenderingServerDefault::texture_3d_create(Image::Format p_format, int p_width, int p_height, int p_depth, bool p_mipmaps, const Vector<Ref<Image>> &p_data) {
	const RID texture = RSG::texture_storage->texture_allocate();
	if (is_on_render_thread() || RSG::rasterizer->can_create_resources_async()) {
		RSG::texture_storage->texture_3d_initialize(texture, p_format, p_width, p_height, p_depth, p_mipmaps, p_data);
	} else {
		command_queue.push(RSG::texture_storage, &RendererTextureStorage::texture_3d_initialize, texture, p_format, p_width, p_height, p_depth, p_mipmaps, snapshot_images(p_data));
	}
	return texture;
}

void RenderingServerDefault::texture_2d_update(RID p_texture, const Ref<Image> &p_image, int p_layer) {
	redraw_request();
	if (!is_on_render_thread()) {
		command_queue.push(RSG::texture_storage, &RendererTextureStorage::texture_2d_update, p_texture, snapshot_image(p_image), p_layer);
	} else {
		command_queue.flush_if_pending();
		RSG::texture_storage->texture_2d_update(p_texture, p_image, p_layer);
	}
}

void RenderingServerDefault::texture_3d_update(RID p_texture, const Vector<Ref<Image>> &p_data) {
	redraw_request();
	if (!is_on_render_thread()) {
		command_queue.push(RSG::texture_storage, &RendererTextureStorage::texture_3d_update, p_texture, snapshot_images(p_data));
	} else {
		command_queue.flush_if_pending();
		RSG::texture_storage->texture_3d_update(p_texture, p_data);
	}
}
