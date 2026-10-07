// wgodot-changes::file
/**************************************************************************/
/*  wgodot_game_bridge.h                                                  */
/**************************************************************************/

#pragma once

namespace WGodotGameBridge {

void initialize();
void deinitialize();
void set_rendering(bool p_rendering);
void set_debugging(bool p_debugging);
void end_frame();

} // namespace WGodotGameBridge
