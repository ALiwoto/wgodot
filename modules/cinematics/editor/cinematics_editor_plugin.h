// wgodot-changes::file
#pragma once

#include "../cinematic_sequence.h"

#include "editor/plugins/editor_plugin.h"
#include "scene/gui/box_container.h"

class Button;
class CheckBox;
class EditorDock;
class Label;
class OptionButton;
class PopupPanel;
class ScrollContainer;
class HSlider;
class SpinBox;
class SubViewport;
class TextureRect;
class Timer;

class CinematicsEditorPanel : public VBoxContainer {
	GDCLASS(CinematicsEditorPanel, VBoxContainer);

	ObjectID source_id;
	Node *preview_root = nullptr;
	CinematicSequence *sequence = nullptr;
	SubViewport *viewport = nullptr;
	TextureRect *image = nullptr;
	HSlider *slider = nullptr;
	SpinBox *time = nullptr;
	SpinBox *speed = nullptr;
	SpinBox *fps = nullptr;
	SpinBox *preview_fps = nullptr;
	OptionButton *render_scale = nullptr;
	PopupPanel *shot_popup = nullptr;
	ScrollContainer *shot_scroll = nullptr;
	VBoxContainer *shot_list = nullptr;
	Vector<Button *> shot_buttons;
	Vector<Button *> transport_buttons;
	Timer *step_timer = nullptr;
	int step_direction = 0;
	Button *play_button = nullptr;
	CheckBox *loop = nullptr;
	CheckBox *audio = nullptr;
	Label *scene_label = nullptr;
	Label *clock = nullptr;
	Vector<StringName> buses;
	String scene_path;
	String sequence_path;
	double render_elapsed = 0.0;
	int settling_frames = 0;
	bool capture_pending = false;

	Button *_button(Container *p_row, const String &p_text, const Callable &p_action);
	Button *_icon_button(Container *p_row, const StringName &p_icon, const String &p_tooltip);
	void _frame_button(Container *p_row, const StringName &p_icon, const String &p_tooltip, int p_direction);
	SpinBox *_number(Container *p_row, const String &p_title, double p_low, double p_high, double p_step, double p_initial);
	void _populate_shots();
	void _shot_button_input(const Ref<InputEvent> &p_event, Button *p_button);
	void _set_status(const String &p_message = String());
	CinematicSequence *_find_sequence(Node *p_node) const;
	void _load_scene();
	void _isolate_resources(Node *p_node);
	void _release_preview();
	void _seek(double p_seconds);
	void _request_frame();
	void _render_scale_changed(int p_index);
	void _step(int p_direction);
	void _start_step(int p_direction);
	void _stop_step();
	void _repeat_step();
	void _preview_input(const Ref<InputEvent> &p_event);
	void _draw_shot_markers();
	void _toggle_play();
	void _select_shot(int p_index);
	void _change_shot(int p_direction);
	void _update_clock();
	void _set_audio();
	void _visibility_changed();
	void _set_preview_quality(bool p_enabled);
	String _timestamp() const;
	void _copy_timestamp();
	void _capture_frame();
	void _finish_capture();
	void _update_icons();

protected:
	void _notification(int p_what);
	static void _bind_methods() {}

public:
	void set_source(Node *p_scene);
	CinematicsEditorPanel();
};

class CinematicsEditorPlugin : public EditorPlugin {
	GDCLASS(CinematicsEditorPlugin, EditorPlugin);

	EditorDock *dock = nullptr;
	CinematicsEditorPanel *panel = nullptr;
	void _scene_changed(Node *p_scene);

protected:
	void _notification(int p_what);

public:
	CinematicsEditorPlugin();
};
