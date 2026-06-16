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

String make_random_short_obfuscated_name(RandomPCG &r_random) {
	static const char *letters = "abcdefghijklmnopqrstuvwxyz";
	const int letter_count = 1 + r_random.rand(3);

	String name;
	for (int i = 0; i < letter_count; i++) {
		name += String::chr(letters[r_random.rand(26)]);
	}
	name += itos(r_random.rand(10));
	return name;
}

String make_obfuscated_name(ObfuscationStrategy p_strategy, RandomPCG &r_random, HashSet<StringName> &r_reserved_names, const String &p_warning_context) {
	if (p_strategy != OBFUSCATION_STRATEGY_SHORT) {
		WARN_PRINT_ONCE(String("WGodot ") + p_warning_context + " obfuscation currently only supports the 'short' strategy. Falling back to 'short'.");
	}

	int fallback_counter = 0;
	while (true) {
		String candidate = make_random_short_obfuscated_name(r_random);
		if (fallback_counter > 10000) {
			candidate = make_short_obfuscated_name(fallback_counter++);
		} else {
			fallback_counter++;
		}
		const StringName candidate_name(candidate);
		if (!r_reserved_names.has(candidate_name)) {
			r_reserved_names.insert(candidate_name);
			return candidate;
		}
	}
}

} // namespace WGodotGDScriptExportTransform
