/**************************************************************************/
/*  render_data.cpp                                                       */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "render_data.h"

#include "core/object/class_db.h"

// wgodot-changes::begin
#include "modules/modules_enabled.gen.h"
#if defined(TOOLS_ENABLED) && defined(MODULE_WGODOT_ENABLED)
#include "modules/wgodot/rendering/render_capture.h"
#include "servers/rendering/renderer_rd/storage_rd/render_data_rd.h"
#endif

void RenderData::capture_texture(const StringName &p_name, RID p_texture, int p_view) const {
#if defined(TOOLS_ENABLED) && defined(MODULE_WGODOT_ENABLED)
	const RenderDataRD *data = Object::cast_to<RenderDataRD>(this);
	if (data) {
		WRenderCapture::compositor_texture(data, p_name, p_texture, p_view);
	}
#endif
}
// wgodot-changes::end

void RenderData::_bind_methods() {
	ClassDB::bind_method(D_METHOD("get_render_scene_buffers"), &RenderData::get_render_scene_buffers);
	ClassDB::bind_method(D_METHOD("get_render_scene_data"), &RenderData::get_render_scene_data);
	ClassDB::bind_method(D_METHOD("get_environment"), &RenderData::get_environment);
	ClassDB::bind_method(D_METHOD("get_camera_attributes"), &RenderData::get_camera_attributes);
	// wgodot-changes::begin
	ClassDB::bind_method(D_METHOD("capture_texture", "name", "texture", "view"), &RenderData::capture_texture, DEFVAL(0));
	// wgodot-changes::end
}
