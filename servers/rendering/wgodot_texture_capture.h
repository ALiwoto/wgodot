// wgodot-changes::file
#pragma once

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)

#include "core/templates/hash_map.h"
#include "core/templates/hash_set.h"
#include "servers/rendering/renderer_canvas_render.h"
#include "servers/rendering/rendering_server_types.h"

namespace WGodotTextureCapture {

struct Usage {
	uint64_t commands = 0;
	uint64_t frames = 0;
	uint64_t last_frame = UINT64_MAX;
	Vector2 max_screen_size;
	Vector2 source_size;
	double max_texel_scale = 0.0;
	bool uncertain = false;
	bool items_truncated = false;
	HashSet<RID> items;
};

struct Result {
	HashMap<RID, Usage> textures;
	int frames = 0;
	bool truncated = false;
};

void begin(const List<RenderingServerTypes::TextureInfo> &p_textures, const HashMap<RID, RID> &p_canvas_textures, RID p_root_texture, int p_frames);
bool is_active();
void record_items(RendererCanvasRender::Item *p_items, const Rect2 &p_clip, RID p_target_texture);
void end_render_frame();
bool is_complete();
Result finish();
void cancel();

} // namespace WGodotTextureCapture

#endif // TOOLS_ENABLED && DEBUG_ENABLED
