// wgodot-changes::file
#pragma once

#include "core/math/vector4.h"
#include "scene/main/canvas_item.h"

class UITextureDrawing : public RefCounted {
	GDCLASS(UITextureDrawing, RefCounted);

protected:
	static void _bind_methods();

public:
	static Vector4 fit_margins(const Vector4 &p_margins, const Vector2 &p_size);
	static void nine_slice(CanvasItem *p_canvas, const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Vector4 &p_source_margins, const Vector4 &p_target_margins, const Color &p_tint = Color(1, 1, 1), bool p_draw_center = true);
	static void nine_slice_rid(RID p_canvas, const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Vector4 &p_source_margins, const Vector4 &p_target_margins, const Color &p_tint = Color(1, 1, 1), bool p_draw_center = true);
	static void circle(CanvasItem *p_canvas, const Ref<Texture2D> &p_texture, const Rect2 &p_rect, const Color &p_tint = Color(1, 1, 1));
};
