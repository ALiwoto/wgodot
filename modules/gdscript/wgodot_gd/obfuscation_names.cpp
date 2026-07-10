// wgodot-changes::file
/**************************************************************************/
/*  obfuscation_names.cpp                                                 */
/**************************************************************************/

#include "obfuscation_names.h"

#include "export_timing.h"

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

bool is_ascii_identifier_start(char32_t p_char) {
	return p_char == '_' || (p_char >= 'a' && p_char <= 'z') || (p_char >= 'A' && p_char <= 'Z');
}

bool is_ascii_identifier_char(char32_t p_char) {
	return is_ascii_identifier_start(p_char) || (p_char >= '0' && p_char <= '9');
}

bool is_ascii_text_identifier(const String &p_name) {
	if (p_name.is_empty() || !is_ascii_identifier_start(p_name[0])) {
		return false;
	}

	for (int i = 1; i < p_name.length(); i++) {
		if (!is_ascii_identifier_char(p_name[i])) {
			return false;
		}
	}
	return true;
}

String make_random_binary_obfuscated_name(RandomPCG &r_random) {
	static constexpr char chars[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ _-$#@!%&'\"";
	constexpr int char_options = sizeof(chars) - 1;

	while (true) {
		const int char_count = 1 + r_random.rand(3);

		String name;
		for (int i = 0; i < char_count; i++) {
			name += String::chr(chars[r_random.rand(char_options)]);
		}
		if (!is_ascii_text_identifier(name)) {
			return name;
		}
	}
}

String wrap_binary_identifier_escape(const String &p_name) {
	return "${{" + p_name + "}}";
}

String unwrap_binary_identifier_escape(const String &p_name) {
	if (p_name.begins_with("${{") && p_name.ends_with("}}") && p_name.length() >= 5) {
		return p_name.substr(3, p_name.length() - 5);
	}

	return p_name;
}

String make_obfuscated_name(ObfuscationStrategy p_strategy, RandomPCG &r_random, HashSet<StringName> &r_reserved_names, const String &p_warning_context, bool p_binary_tokens_export) {
	if (p_strategy != OBFUSCATION_STRATEGY_SHORT) {
		WARN_PRINT_ONCE(String("WGodot ") + p_warning_context + " obfuscation currently only supports the 'short' strategy. Falling back to 'short'.");
	}

	const bool probe_enabled = is_obfuscation_name_probe_enabled();
	const uint64_t start_usec = probe_enabled ? export_timing_get_ticks_usec() : 0;
	int attempts = 0;
	int fallback_counter = 0;
	while (true) {
		attempts++;
		String candidate = p_binary_tokens_export ? make_random_binary_obfuscated_name(r_random) : make_random_short_obfuscated_name(r_random);
		if (fallback_counter > 10000) {
			candidate = make_short_obfuscated_name(fallback_counter++);
		} else {
			fallback_counter++;
		}
		const String source_candidate = p_binary_tokens_export ? wrap_binary_identifier_escape(candidate) : candidate;
		const StringName candidate_name(candidate);
		const StringName source_candidate_name(source_candidate);
		if (!r_reserved_names.has(candidate_name) && !r_reserved_names.has(source_candidate_name)) {
			r_reserved_names.insert(candidate_name);
			r_reserved_names.insert(source_candidate_name);
			if (probe_enabled) {
				record_obfuscation_name_probe(attempts, export_timing_get_ticks_usec() - start_usec);
			}
			return source_candidate;
		}
	}
}

} // namespace WGodotGDScriptExportTransform
