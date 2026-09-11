// wgodot-changes::file
#include "ui_texture_drawing.h"

#include "core/object/class_db.h"

#include "servers/rendering/rendering_server.h"

Vector4 UITextureDrawing::fit_margins(const Vector4 &p_margins, const Vector2 &p_size) {
	Vector4 margins = p_margins.max(Vector4());
	const real_t horizontal = margins.x + margins.z;
	const real_t vertical = margins.y + margins.w;
	if (horizontal > p_size.x && horizontal > 0) {
		const real_t scale = MAX(p_size.x, 0) / horizontal;
		margins.x *= scale;
		margins.z *= scale;
	}
	if (vertical > p_size.y && vertical > 0) {
		const real_t scale = MAX(p_size.y, 0) / vertical;
		margins.y *= scale;
		margins.w *= scale;
	}
	return margins;
}

void UITextureDrawing::nine_slice(CanvasItem *p_canvas, const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Vector4 &p_source_margins, const Vector4 &p_target_margins, const Color &p_tint, bool p_draw_center) {
	ERR_FAIL_NULL(p_canvas);
	nine_slice_rid(p_canvas->get_canvas_item(), p_texture, p_rect, p_source_margins, p_target_margins, p_tint, p_draw_center);
}

void UITextureDrawing::nine_slice_rid(RID p_canvas, const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Vector4 &p_source_margins, const Vector4 &p_target_margins, const Color &p_tint, bool p_draw_center) {
	ERR_FAIL_COND(p_texture.is_null());
	const Vector2 size = p_texture->get_size();
	const Vector4 source = fit_margins(p_source_margins, size);
	const Vector4 target = fit_margins(p_target_margins, p_rect.size);
	const real_t sx[] = { 0, source.x, size.x - source.z, size.x };
	const real_t sy[] = { 0, source.y, size.y - source.w, size.y };
	const real_t dx[] = { p_rect.position.x, p_rect.position.x + target.x, p_rect.get_end().x - target.z, p_rect.get_end().x };
	const real_t dy[] = { p_rect.position.y, p_rect.position.y + target.y, p_rect.get_end().y - target.w, p_rect.get_end().y };
	for (int row = 0; row < 3; row++) {
		for (int column = 0; column < 3; column++) {
			if (!p_draw_center && row == 1 && column == 1) {
				continue;
			}
			const Rect2 from(sx[column], sy[row], sx[column + 1] - sx[column], sy[row + 1] - sy[row]);
			const Rect2 to(dx[column], dy[row], dx[column + 1] - dx[column], dy[row + 1] - dy[row]);
			if (from.has_area() && to.has_area()) {
				p_texture->draw_rect_region(p_canvas, to, from, p_tint);
			}
		}
	}
}

void UITextureDrawing::circle(CanvasItem *p_canvas, const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Color &p_tint) {
	ERR_FAIL_NULL(p_canvas);
	ERR_FAIL_COND(p_texture.is_null());
	Vector<Vector2> points;
	Vector<Vector2> uvs;
	points.resize(64);
	uvs.resize(64);
	for (int i = 0; i < 64; i++) {
		const Vector2 direction = Vector2::from_angle(i * Math::TAU / 64);
		points.write[i] = p_rect.get_center() + direction * p_rect.size * 0.5;
		uvs.write[i] = Vector2(0.5, 0.5) + direction * 0.5;
	}
	p_canvas->draw_polygon(points, Vector<Color>({ p_tint }), uvs, p_texture);
}

void UITextureDrawing::_bind_methods() {
	ClassDB::bind_static_method("UITextureDrawing", D_METHOD("fit_margins", "margins", "size"), &UITextureDrawing::fit_margins);
	ClassDB::bind_static_method("UITextureDrawing", D_METHOD("nine_slice", "canvas", "texture", "rect", "source_margins", "target_margins", "tint", "draw_center"), &UITextureDrawing::nine_slice, DEFVAL(Color(1, 1, 1)), DEFVAL(true));
	ClassDB::bind_static_method("UITextureDrawing", D_METHOD("nine_slice_rid", "canvas", "texture", "rect", "source_margins", "target_margins", "tint", "draw_center"), &UITextureDrawing::nine_slice_rid, DEFVAL(Color(1, 1, 1)), DEFVAL(true));
	ClassDB::bind_static_method("UITextureDrawing", D_METHOD("circle", "canvas", "texture", "rect", "tint"), &UITextureDrawing::circle, DEFVAL(Color(1, 1, 1)));
}
