// wgodot-changes::file
/**************************************************************************/
/*  wgodot_pause_controller.h                                             */
/**************************************************************************/

#pragma once

#include "core/variant/dictionary.h"

namespace WGodotPauseController {

void initialize();
void deinitialize();

void set_game_paused(bool p_paused);
bool is_game_paused();

void set_physics_paused(bool p_paused);
bool is_physics_paused();
bool is_physics_effectively_paused();

bool request_process_steps(uint64_t p_request_id, int p_count, Dictionary &r_error);
bool request_physics_steps(uint64_t p_request_id, int p_count, Dictionary &r_error);

bool begin_process_frame();
void end_process_frame();

bool begin_physics_step();
void end_physics_step();

bool is_process_step_active();
bool is_physics_step_active();

Dictionary get_state();

} // namespace WGodotPauseController
