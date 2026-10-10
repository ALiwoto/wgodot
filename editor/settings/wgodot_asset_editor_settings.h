// wgodot-changes::file
#pragma once

#include "core/variant/dictionary.h"

// One project-local, disk-indexed store. Only the requested asset is decoded.
class WGodotAssetEditorSettings {
public:
	static Error load(const String &p_key, Dictionary &r_settings);
	static Error save(const String &p_key, const Dictionary &p_settings);
};
