// wgodot-changes::file
/**************************************************************************/
/*  deadcode_injection.cpp                                                */
/**************************************************************************/

#include "deadcode_injection.h"

#include "deadcode.gen.h"

#include "../gdscript_parser.h"

#include "core/math/random_pcg.h"
#include "core/templates/local_vector.h"

namespace {

struct DeadCodeInsertion {
	int offset = 0;
	String text;
};

struct InsertionSort {
	bool operator()(const DeadCodeInsertion &p_left, const DeadCodeInsertion &p_right) const {
		return p_left.offset < p_right.offset;
	}
};

void build_line_offsets(const String &p_source, Vector<int> &r_line_offsets) {
	r_line_offsets.clear();
	r_line_offsets.push_back(0);
	for (int i = 0; i < p_source.length(); i++) {
		if (p_source[i] == '\n') {
			r_line_offsets.push_back(i + 1);
		}
	}
}

int get_line_start_offset(const Vector<int> &p_line_offsets, int p_line) {
	if (p_line <= 0 || p_line > p_line_offsets.size()) {
		return -1;
	}
	return p_line_offsets[p_line - 1];
}

int get_line_end_offset(const String &p_source, const Vector<int> &p_line_offsets, int p_line) {
	const int line_start = get_line_start_offset(p_line_offsets, p_line);
	if (line_start < 0) {
		return -1;
	}
	if (p_line < p_line_offsets.size()) {
		return p_line_offsets[p_line] - 1;
	}
	return p_source.length();
}

String get_line_indent(const String &p_source, const Vector<int> &p_line_offsets, int p_line) {
	const int line_start = get_line_start_offset(p_line_offsets, p_line);
	const int line_end = get_line_end_offset(p_source, p_line_offsets, p_line);
	if (line_start < 0 || line_end < line_start) {
		return String();
	}

	int index = line_start;
	while (index < line_end && (p_source[index] == ' ' || p_source[index] == '\t')) {
		index++;
	}
	return p_source.substr(line_start, index - line_start);
}

String indent_snippet(const String &p_snippet, const String &p_indent) {
	const Vector<String> lines = p_snippet.replace("\r\n", "\n").replace("\r", "\n").split("\n", true);
	String text;
	for (int i = 0; i < lines.size(); i++) {
		const String line = lines[i];
		if (!line.strip_edges().is_empty()) {
			text += p_indent + line;
		}
		if (i + 1 < lines.size()) {
			text += "\n";
		}
	}
	return text;
}

uint64_t make_seed(const String &p_source, const String &p_path, const WGodotGDScriptExportTransform::TransformOptions &p_options) {
	const String seed_text = p_path + "::" + String::num_uint64(p_source.hash64()) + "::" + itos(p_options.min_in_class_dead_code_injection) + "::" + itos(p_options.max_in_class_dead_code_injection);
	return seed_text.hash64();
}

int get_random_injection_count(RandomPCG &r_random, int p_min, int p_max) {
	const int min_count = MAX(p_min, 0);
	const int max_count = MAX(p_max, min_count);
	if (max_count <= 0) {
		return 0;
	}
	return min_count + static_cast<int>(r_random.rand(max_count - min_count + 1));
}

String make_dead_code_identifier_id(RandomPCG &r_random, uint64_t &r_unique_id) {
	const uint64_t random_bits = (static_cast<uint64_t>(r_random.rand()) << 32) | r_random.rand();
	const uint64_t identifier_id = random_bits ^ (r_unique_id++ * 0x9E3779B97F4A7C15ULL);
	return String::num_uint64(identifier_id);
}

String make_dead_code_block(RandomPCG &r_random, const String &p_indent, uint64_t &r_unique_id) {
	if (WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATE_COUNT <= 0) {
		return String();
	}

	const uint32_t template_index = r_random.rand(WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATE_COUNT);
	String snippet = WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATES[template_index].source;
	snippet = snippet.strip_edges();
	if (snippet.is_empty()) {
		return String();
	}

	snippet = snippet.replace("WGODOT_DC_ID", make_dead_code_identifier_id(r_random, r_unique_id));
	return "\n" + indent_snippet(snippet, p_indent) + "\n";
}

void collect_class_insertions(const String &p_source, const Vector<int> &p_line_offsets, const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope, RandomPCG &r_random, uint64_t &r_unique_id, int p_min, int p_max, LocalVector<DeadCodeInsertion> &r_insertions) {
	if (p_class == nullptr) {
		return;
	}

	const bool no_mangle_scope = p_no_mangle_scope || p_class->wgodot_no_mangle;
	if (!no_mangle_scope && p_class->members.size() > 1) {
		for (int i = 0; i + 1 < p_class->members.size(); i++) {
			const GDScriptParser::Node *current_member = p_class->members[i].get_source_node();
			const GDScriptParser::Node *next_member = p_class->members[i + 1].get_source_node();
			if (current_member == nullptr || next_member == nullptr) {
				continue;
			}

			const int offset = get_line_start_offset(p_line_offsets, current_member->end_line + 1);
			if (offset < 0) {
				continue;
			}

			const String indent = get_line_indent(p_source, p_line_offsets, next_member->start_line);
			const int injection_count = get_random_injection_count(r_random, p_min, p_max);
			String block;
			for (int j = 0; j < injection_count; j++) {
				block += make_dead_code_block(r_random, indent, r_unique_id);
			}
			if (block.is_empty()) {
				continue;
			}

			DeadCodeInsertion insertion;
			insertion.offset = offset;
			insertion.text = block;
			r_insertions.push_back(insertion);
		}
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			collect_class_insertions(p_source, p_line_offsets, member.m_class, no_mangle_scope, r_random, r_unique_id, p_min, p_max, r_insertions);
		}
	}
}

} // namespace

namespace WGodotGDScriptDeadCodeInjection {

String inject_in_class_dead_code(const String &p_source, const String &p_path, const WGodotGDScriptExportTransform::TransformOptions &p_options, bool *r_changed) {
	if (r_changed != nullptr) {
		*r_changed = false;
	}

	if (!p_options.dead_code_injection_enabled ||
			p_options.max_in_class_dead_code_injection <= 0 ||
			WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATE_COUNT <= 0) {
		return p_source;
	}

	GDScriptParser parser;
	if (parser.parse(p_source, p_path, false) != OK) {
		return p_source;
	}

	Vector<int> line_offsets;
	build_line_offsets(p_source, line_offsets);

	RandomPCG random;
	random.seed(make_seed(p_source, p_path, p_options));

	LocalVector<DeadCodeInsertion> insertions;
	uint64_t unique_id = 1;
	collect_class_insertions(p_source, line_offsets, parser.get_tree(), false, random, unique_id, p_options.min_in_class_dead_code_injection, p_options.max_in_class_dead_code_injection, insertions);
	if (insertions.is_empty()) {
		return p_source;
	}

	insertions.sort_custom<InsertionSort>();
	String result = p_source;
	for (int i = insertions.size() - 1; i >= 0; i--) {
		const DeadCodeInsertion &insertion = insertions[i];
		if (insertion.offset < 0 || insertion.offset > result.length()) {
			continue;
		}
		result = result.substr(0, insertion.offset) + insertion.text + result.substr(insertion.offset);
	}

	if (result != p_source && r_changed != nullptr) {
		*r_changed = true;
	}
	return result;
}

} // namespace WGodotGDScriptDeadCodeInjection
