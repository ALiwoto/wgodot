// wgodot-changes::file

#include "editor_log.h"

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
