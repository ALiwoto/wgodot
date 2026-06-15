// wgodot-changes::file
/**************************************************************************/
/*  obfuscation_names.cpp                                                 */
/**************************************************************************/

#include "obfuscation_names.h"

#include "core/error/error_macros.h"

namespace WGodotGDScriptExportTransform {

String make_short_obfuscated_name(int p_index) {
	static const char *letters = "abcdefghijklmnopqrstuvwxyz";
	const int digit = p_index % 10;
	int group = p_index / 10;

	String prefix;
	do {
		prefix = String::chr(letters[group % 26]) + prefix;
		group = (group / 26) - 1;
	} while (group >= 0);

	return prefix + itos(digit);
}

String make_obfuscated_name(ObfuscationStrategy p_strategy, int &r_counter, HashSet<StringName> &r_reserved_names, const String &p_warning_context) {
	if (p_strategy != OBFUSCATION_STRATEGY_SHORT) {
		WARN_PRINT_ONCE(String("WGodot ") + p_warning_context + " obfuscation currently only supports the 'short' strategy. Falling back to 'short'.");
	}

	while (true) {
		const String candidate = make_short_obfuscated_name(r_counter++);
		const StringName candidate_name(candidate);
		if (!r_reserved_names.has(candidate_name)) {
			r_reserved_names.insert(candidate_name);
			return candidate;
		}
	}
}

} // namespace WGodotGDScriptExportTransform
