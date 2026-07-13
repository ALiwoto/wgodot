// wgodot-changes::file
/**************************************************************************/
/*  wgodot_wait_controller.h                                              */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace WGodotWaitController {

void initialize();
void deinitialize();

bool request_wait(uint64_t p_request_id, int p_count, bool p_physics, Dictionary &r_error);
void cancel_process_wait(const String &p_message);
void cancel_physics_wait(const String &p_message);

void end_physics_step();
void end_frame();

} // namespace WGodotWaitController
