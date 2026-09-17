// wgodot-changes::file

#include "editor_log.h"

#include "modules/modules_enabled.gen.h"

#ifdef MODULE_WGODOT_ENABLED
#include "modules/wgodot/editor/wgodot_project_check.h"

class WGodotReanalyzeButton : public Button {
	GDCLASS(WGodotReanalyzeButton, Button);

	EditorLog *editor_log = nullptr;
	WGodotProjectCheck project_check;

	void show_result(const Dictionary &p_result) {
		set_process(false);
		set_disabled(false);
		if (!(bool)p_result.get("ok", false)) {
			editor_log->add_message(String(p_result["message"]).replace("[", "[lb]"), EditorLog::MSG_TYPE_ERROR);
			return;
		}

		// Replace startup and refresh diagnostics with the complete check result.
		editor_log->clear();
		const Array diagnostics = p_result.get("diagnostics", Array());
		for (int i = 0; i < diagnostics.size(); i++) {
			const Dictionary diagnostic = diagnostics[i];
			const String path = diagnostic["path"];
			const int line = diagnostic["line"];
			const String message = String(diagnostic["message"]).replace("[", "[lb]");
			const bool warning = diagnostic["warning"];
			const String location = line > 0 ? vformat("[url]%s:%d[/url]", path.replace("[", "[lb]"), line) : path.replace("[", "[lb]");
			editor_log->add_message(location + " - " + message, warning ? EditorLog::MSG_TYPE_WARNING : EditorLog::MSG_TYPE_ERROR);
		}
		const PackedStringArray output = p_result["output"];
		editor_log->add_message(output[output.size() - 1]);
	}

protected:
	static void _bind_methods() {}

	void pressed() override {
		set_disabled(true);
		editor_log->clear();
		editor_log->add_message(TTR("Re-analyzing saved project scripts..."));
		const Dictionary result = project_check.start();
		if (!result.is_empty()) {
			show_result(result);
			return;
		}
		set_process(true);
	}

	void _notification(int p_what) {
		switch (p_what) {
			case NOTIFICATION_ENTER_TREE:
			case NOTIFICATION_THEME_CHANGED: {
				set_button_icon(get_editor_theme_icon(SNAME("Reload")));
			} break;
			case NOTIFICATION_PROCESS: {
				// Imports and script reloads can process editor events recursively.
				set_process(false);
				const Dictionary result = project_check.poll();
				if (result.is_empty()) {
					set_process(true);
				} else {
					show_result(result);
				}
			} break;
		}
	}

public:
	explicit WGodotReanalyzeButton(EditorLog *p_editor_log) :
			editor_log(p_editor_log) {
		set_accessibility_name(TTRC("Re-analyze"));
		set_tooltip_text(TTR("Re-analyze\nClear output and check all saved project scripts for errors and warnings."));
		set_theme_type_variation("BottomPanelButton");
		set_focus_mode(FOCUS_ACCESSIBILITY);
	}
};
#endif // MODULE_WGODOT_ENABLED

void EditorLog::_wgodot_add_reanalyze_button(Control *p_toolbar) {
#ifdef MODULE_WGODOT_ENABLED
	p_toolbar->add_child(memnew(WGodotReanalyzeButton(this)));
#endif
}

Array EditorLog::wgodot_get_messages() const {
	Array result;
	for (const LogMessage &message : messages) {
		Dictionary entry;
		entry["text"] = message.text;
		entry["type"] = static_cast<int>(message.type);
		entry["count"] = message.count;
		result.push_back(entry);
	}
	return result;
}
