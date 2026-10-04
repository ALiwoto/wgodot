// wgodot-changes::file
#include "wgodot_texture_capture.h"

#if defined(TOOLS_ENABLED) && defined(DEBUG_ENABLED)

#include "core/os/mutex.h"
#include "servers/rendering/renderer_canvas_cull.h"

#include <atomic>

// Store only an affine UV mapping, not copies of polygon vertex buffers.
void RendererCanvasRender::Polygon::debug_set_uv_mapping(const Vector<Vector2> &p_points, const Vector<Vector2> &p_uvs, bool p_skinned) {
	debug_uv_affine = false;
	if (p_skinned || p_points.size() < 3 || p_points.size() != p_uvs.size()) {
		return;
	}
	int second = 1;
	while (second < p_uvs.size() && p_uvs[second].is_equal_approx(p_uvs[0])) {
		second++;
	}
	if (second == p_uvs.size()) {
		return;
	}
	for (int third = second + 1; third < p_uvs.size(); third++) {
		Transform2D uv(p_uvs[second] - p_uvs[0], p_uvs[third] - p_uvs[0], p_uvs[0]);
		if (Math::is_zero_approx(uv.determinant())) {
			continue;
		}
		const Transform2D points(p_points[second] - p_points[0], p_points[third] - p_points[0], p_points[0]);
		debug_uv_to_local = points * uv.affine_inverse();
		if (!debug_uv_to_local.is_finite()) {
			return;
		}
		for (int i = 0; i < p_points.size(); i++) {
			if (debug_uv_to_local.xform(p_uvs[i]).distance_to(p_points[i]) > 0.01) {
				return;
			}
		}
		debug_uv_affine = true;
		return;
	}
}

namespace WGodotTextureCapture {
namespace {

constexpr int MAX_TEXTURES = 8192;
constexpr int MAX_ITEMS_PER_TEXTURE = 8;
Mutex capture_mutex;
std::atomic<bool> active = false;
HashMap<RID, RenderingServerTypes::TextureInfo> texture_info;
HashMap<RID, RID> canvas_textures;
RID root_texture;
Result result;
int requested_frames = 0;

// Largest singular value: a uniform downscale must preserve both texture axes,
// including skewed and rotated mappings. Cropping does not change texel density.
double texel_scale(const Transform2D &p_uv_to_screen, const Vector2 &p_texture_size) {
	const Vector2 x = p_uv_to_screen[0] / p_texture_size.x;
	const Vector2 y = p_uv_to_screen[1] / p_texture_size.y;
	const double a = x.length_squared();
	const double b = x.dot(y);
	const double c = y.length_squared();
	return Math::sqrt(MAX(0.0, (a + c + Math::sqrt((a - c) * (a - c) + 4.0 * b * b)) * 0.5));
}

const RenderingServerTypes::TextureInfo *resolve_texture(RID &r_texture, bool *r_proxy = nullptr) {
	for (int depth = 0; depth < 16; depth++) {
		if (const RID *diffuse = canvas_textures.getptr(r_texture)) {
			r_texture = *diffuse;
			continue;
		}
		const RenderingServerTypes::TextureInfo *info = texture_info.getptr(r_texture);
		if (!info || !info->debug_proxy_to.is_valid()) {
			return info;
		}
		if (r_proxy) {
			*r_proxy = true;
		}
		r_texture = info->debug_proxy_to;
	}
	return nullptr;
}

void record(RID p_texture, RID p_item, const Rect2 &p_screen_rect, const Rect2 &p_clip, const Transform2D &p_uv_to_screen, bool p_uncertain) {
	if (!p_texture.is_valid() || !p_screen_rect.intersects(p_clip)) {
		return;
	}
	// Proxies can change their target during capture (for example AnimatedTexture).
	// Their initial allocation is useful context, but cannot justify resizing.
	const RenderingServerTypes::TextureInfo *info = resolve_texture(p_texture, &p_uncertain);
	if (!result.textures.has(p_texture) && result.textures.size() >= MAX_TEXTURES) {
		result.truncated = true;
		return;
	}
	Usage &usage = result.textures[p_texture];
	if (info) {
		usage.source_size = Vector2(info->width, info->height);
	}
	usage.commands++;
	if (usage.last_frame != uint64_t(result.frames)) {
		usage.frames++;
		usage.last_frame = result.frames;
	}
	const Vector2 visible_size = p_screen_rect.intersection(p_clip).size;
	usage.max_screen_size = usage.max_screen_size.max(visible_size);
	usage.uncertain |= p_uncertain || !info || info->width == 0 || info->height == 0;
	if (usage.items.size() < MAX_ITEMS_PER_TEXTURE) {
		usage.items.insert(p_item);
	} else if (!usage.items.has(p_item)) {
		usage.items_truncated = true;
	}
	if (!p_uncertain && info && info->width > 0 && info->height > 0) {
		const double scale = texel_scale(p_uv_to_screen, Vector2(info->width, info->height));
		if (Math::is_finite(scale) && scale > 0.0) {
			usage.max_texel_scale = MAX(usage.max_texel_scale, scale);
		} else {
			usage.uncertain = true;
		}
	}
}

void record_rect(RID p_texture, RID p_item, const Rect2 &p_rect, Vector2 p_source_size, const Transform2D &p_transform, const Rect2 &p_clip, bool p_uncertain, bool p_transpose = false) {
	RID texture = p_texture;
	const RenderingServerTypes::TextureInfo *info = resolve_texture(texture);
	const Vector2 texture_size = info ? Vector2(info->width, info->height) : Vector2();
	if (p_source_size == Vector2()) {
		p_source_size = texture_size;
	}
	Transform2D uv_to_local;
	if (p_source_size.x != 0 && p_source_size.y != 0) {
		if (p_transpose) {
			uv_to_local = Transform2D(Vector2(0, p_rect.size.y * texture_size.x / p_source_size.x), Vector2(p_rect.size.x * texture_size.y / p_source_size.y, 0), p_rect.position);
		} else {
			uv_to_local = Transform2D(Vector2(p_rect.size.x * texture_size.x / p_source_size.x, 0), Vector2(0, p_rect.size.y * texture_size.y / p_source_size.y), p_rect.position);
		}
	} else {
		p_uncertain = true;
	}
	record(p_texture, p_item, p_transform.xform(p_rect.abs()), p_clip, p_transform * uv_to_local, p_uncertain);
}

} // namespace

void begin(const List<RenderingServerTypes::TextureInfo> &p_textures, const HashMap<RID, RID> &p_canvas_textures, RID p_root_texture, int p_frames) {
	MutexLock lock(capture_mutex);
	texture_info.clear();
	for (const RenderingServerTypes::TextureInfo &info : p_textures) {
		texture_info.insert(info.texture, info);
	}
	canvas_textures = p_canvas_textures;
	root_texture = p_root_texture;
	resolve_texture(root_texture);
	result = Result();
	requested_frames = p_frames;
	active.store(true, std::memory_order_release);
}

bool is_active() {
	return active.load(std::memory_order_acquire);
}

void record_items(RendererCanvasRender::Item *p_items, const Rect2 &p_clip, RID p_target_texture) {
	if (!is_active()) {
		return;
	}
	MutexLock lock(capture_mutex);
	if (!is_active()) {
		return;
	}
	using Item = RendererCanvasRender::Item;
	resolve_texture(p_target_texture);
	// A SubViewport can later be enlarged on screen; its own pixels alone do
	// not establish a safe source resolution. Keep its measurements advisory.
	const bool offscreen = !root_texture.is_valid() || p_target_texture != root_texture;
	const Rect2 viewport_clip(Vector2(), p_clip.size);
	for (Item *item = p_items; item; item = item->next) {
		if (item->final_modulate.a <= 0.0) {
			continue;
		}
		const RID owner = static_cast<RendererCanvasCull::Item *>(item)->self;
		bool uncertain = offscreen || item->material.is_valid() || (item->material_owner && item->material_owner->material.is_valid()) || (item->canvas_group_owner && item->canvas_group_owner->material.is_valid()) || item->skeleton.is_valid() || item->repeat_times > 1;
		Transform2D transform = item->final_transform;
		Rect2 item_clip = viewport_clip;
		if (item->final_clip_owner) {
			item_clip = p_clip.intersection(item->final_clip_owner->final_clip_rect);
			item_clip.position -= p_clip.position;
		}
		Rect2 clip = item_clip;
		for (Item::Command *command = item->commands; command; command = command->next) {
			switch (command->type) {
				case Item::Command::TYPE_TRANSFORM: {
					transform = item->final_transform * static_cast<Item::CommandTransform *>(command)->xform;
				} break;
				case Item::Command::TYPE_CLIP_IGNORE: {
					const bool ignore = static_cast<Item::CommandClipIgnore *>(command)->ignore;
					clip = ignore ? viewport_clip : item_clip;
				} break;
				case Item::Command::TYPE_RECT: {
					const auto *rect = static_cast<Item::CommandRect *>(command);
					if (rect->modulate.a <= 0.0) {
						break;
					}
					Vector2 source = (rect->flags & RendererCanvasRender::CANVAS_RECT_REGION) ? rect->source.size.abs() : Vector2();
					const bool specialized = rect->flags & (RendererCanvasRender::CANVAS_RECT_MSDF | RendererCanvasRender::CANVAS_RECT_LCD | RendererCanvasRender::CANVAS_RECT_IS_GROUP);
					record_rect(rect->texture, owner, rect->rect, source, transform, clip, uncertain || specialized, rect->flags & RendererCanvasRender::CANVAS_RECT_TRANSPOSE);
				} break;
				case Item::Command::TYPE_NINEPATCH: {
					const auto *patch = static_cast<Item::CommandNinePatch *>(command);
					if (patch->color.a <= 0.0) {
						break;
					}
					RID texture = patch->texture;
					const auto *info = resolve_texture(texture);
					const Vector2 source = patch->source.size != Vector2() ? patch->source.size : (info ? Vector2(info->width, info->height) : Vector2());
					const Vector2 destination = patch->rect.size;
					const double dx[] = { 0, patch->margin[0], destination.x - patch->margin[2], destination.x };
					const double dy[] = { 0, patch->margin[1], destination.y - patch->margin[3], destination.y };
					const double sx[] = { 0, patch->margin[0], source.x - patch->margin[2], source.x };
					const double sy[] = { 0, patch->margin[1], source.y - patch->margin[3], source.y };
					if (!info || dx[2] < dx[1] || dy[2] < dy[1] || sx[2] < sx[1] || sy[2] < sy[1]) {
						record_rect(patch->texture, owner, patch->rect, source, transform, clip, true);
						break;
					}
					bool mapping_uncertain = uncertain;
					bool visible = false;
					Rect2 visible_bounds;
					Transform2D largest_mapping;
					double largest_scale = 0.0;
					for (int y = 0; y < 3; y++) {
						for (int x = 0; x < 3; x++) {
							if (x == 1 && y == 1 && !patch->draw_center) {
								continue;
							}
							const Rect2 region(patch->rect.position + Vector2(dx[x], dy[y]), Vector2(dx[x + 1] - dx[x], dy[y + 1] - dy[y]));
							const Rect2 bounds = transform.xform(region).intersection(clip);
							if (!region.has_area() || !bounds.has_area()) {
								continue;
							}
							visible_bounds = visible ? visible_bounds.merge(bounds) : bounds;
							visible = true;
							const bool tiled = (x == 1 && patch->axis_x != RSE::NINE_PATCH_STRETCH) || (y == 1 && patch->axis_y != RSE::NINE_PATCH_STRETCH);
							const Vector2 source_size(sx[x + 1] - sx[x], sy[y + 1] - sy[y]);
							if (tiled || source_size.x <= 0 || source_size.y <= 0 || info->width == 0 || info->height == 0) {
								mapping_uncertain = true;
								continue;
							}
							const Transform2D mapping = transform * Transform2D(Vector2(region.size.x * info->width / source_size.x, 0), Vector2(0, region.size.y * info->height / source_size.y), region.position);
							const double scale = texel_scale(mapping, Vector2(info->width, info->height));
							if (!Math::is_finite(scale)) {
								mapping_uncertain = true;
							} else if (scale > largest_scale) {
								largest_scale = scale;
								largest_mapping = mapping;
							}
						}
					}
					if (visible) {
						record(patch->texture, owner, visible_bounds, clip, largest_mapping, mapping_uncertain || largest_scale == 0.0);
					}
				} break;
				case Item::Command::TYPE_POLYGON: {
					const auto *polygon = static_cast<Item::CommandPolygon *>(command);
					record(polygon->texture, owner, transform.xform(polygon->polygon.rect_cache), clip, transform * polygon->polygon.debug_uv_to_local, uncertain || !polygon->polygon.debug_uv_affine);
				} break;
				case Item::Command::TYPE_PRIMITIVE: {
					const auto *primitive = static_cast<Item::CommandPrimitive *>(command);
					if (primitive->point_count == 0) {
						break;
					}
					Rect2 bounds(primitive->points[0], Vector2());
					for (uint32_t i = 1; i < primitive->point_count; i++) {
						bounds.expand_to(primitive->points[i]);
					}
					record(primitive->texture, owner, transform.xform(bounds), clip, Transform2D(), true);
				} break;
				case Item::Command::TYPE_MESH:
				case Item::Command::TYPE_MULTIMESH:
				case Item::Command::TYPE_PARTICLES: {
					RID texture;
					if (command->type == Item::Command::TYPE_MESH) {
						texture = static_cast<Item::CommandMesh *>(command)->texture;
					} else if (command->type == Item::Command::TYPE_MULTIMESH) {
						texture = static_cast<Item::CommandMultiMesh *>(command)->texture;
					} else {
						texture = static_cast<Item::CommandParticles *>(command)->texture;
					}
					record(texture, owner, item->global_rect_cache, clip, Transform2D(), true);
				} break;
				case Item::Command::TYPE_ANIMATION_SLICE: {
					// Time-dependent command visibility is not reproduced here.
					// Treat the remaining commands conservatively in this item.
					uncertain = true;
				} break;
			}
		}
	}
}

void end_render_frame() {
	if (!is_active()) {
		return;
	}
	MutexLock lock(capture_mutex);
	if (is_active() && ++result.frames >= requested_frames) {
		active.store(false, std::memory_order_release);
	}
}

bool is_complete() {
	MutexLock lock(capture_mutex);
	return requested_frames > 0 && result.frames >= requested_frames;
}

Result finish() {
	MutexLock lock(capture_mutex);
	active.store(false, std::memory_order_release);
	Result captured = result;
	result = Result();
	texture_info.clear();
	canvas_textures.clear();
	root_texture = RID();
	requested_frames = 0;
	return captured;
}

void cancel() {
	finish();
}

} // namespace WGodotTextureCapture

#endif // TOOLS_ENABLED && DEBUG_ENABLED
