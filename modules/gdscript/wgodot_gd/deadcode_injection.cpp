// wgodot-changes::file
/**************************************************************************/
/*  deadcode_injection.cpp                                                */
/**************************************************************************/

#include "deadcode_injection.h"

#include "../gdscript_parser.h"
#include "deadcode.gen.h"

#include "core/math/random_pcg.h"
#include "core/templates/local_vector.h"

namespace {

using DeadCodeInsertion = WGodotGDScriptDeadCodeInjection::Insertion;

struct InsertionSort {
	bool operator()(const DeadCodeInsertion &p_left, const DeadCodeInsertion &p_right) const {
		return p_left.offset < p_right.offset;
	}
};

struct DeadCodeTemplatePool {
	const WGodotGDScriptDeadCodeTemplates::DeadCodeTemplate *templates = nullptr;
	int count = 0;
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

bool has_annotation(const GDScriptParser::Node *p_node, const StringName &p_annotation_name) {
	if (p_node == nullptr) {
		return false;
	}

	for (const GDScriptParser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation != nullptr && annotation->name == p_annotation_name) {
			return true;
		}
	}
	return false;
}

bool is_no_mangle_class(const GDScriptParser::ClassNode *p_class) {
	return p_class != nullptr && (p_class->wgodot_no_mangle || has_annotation(p_class, SNAME("@no_mangle")));
}

bool is_static_class(const GDScriptParser::ClassNode *p_class) {
	return p_class != nullptr && (p_class->wgodot_static_class || has_annotation(p_class, SNAME("@static_class")));
}

bool is_interface_class(const GDScriptParser::ClassNode *p_class) {
	return p_class != nullptr && p_class->wgodot_is_interface;
}

int get_annotated_start_line(const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return -1;
	}

	int start_line = p_node->start_line;
	for (const GDScriptParser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation != nullptr && annotation->start_line > 0) {
			start_line = start_line > 0 ? MIN(start_line, annotation->start_line) : annotation->start_line;
		}
	}
	return start_line;
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
	const String seed_text = p_path + "::" + String::num_uint64(p_source.hash64()) + "::" + itos(p_options.min_in_class_dead_code_injection) + "::" + itos(p_options.max_in_class_dead_code_injection) + "::" + itos(p_options.max_dead_code_gaps_per_file);
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

DeadCodeTemplatePool get_template_pool(bool p_static_class) {
	DeadCodeTemplatePool pool;
	if (p_static_class) {
		pool.templates = WGodotGDScriptDeadCodeTemplates::STATIC_IN_CLASS_DEAD_CODE_TEMPLATES;
		pool.count = WGodotGDScriptDeadCodeTemplates::STATIC_IN_CLASS_DEAD_CODE_TEMPLATE_COUNT;
		return pool;
	}

	pool.templates = WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATES;
	pool.count = WGodotGDScriptDeadCodeTemplates::IN_CLASS_DEAD_CODE_TEMPLATE_COUNT;
	return pool;
}

String make_dead_code_block(RandomPCG &r_random, const String &p_indent, uint64_t &r_unique_id, const DeadCodeTemplatePool &p_template_pool) {
	if (p_template_pool.count <= 0 || p_template_pool.templates == nullptr) {
		return String();
	}

	const uint32_t template_index = r_random.rand(p_template_pool.count);
	String snippet = p_template_pool.templates[template_index].source;
	snippet = snippet.strip_edges();
	if (snippet.is_empty()) {
		return String();
	}

	snippet = snippet.replace("WGODOT_DC_ID", make_dead_code_identifier_id(r_random, r_unique_id));
	return "\n" + indent_snippet(snippet, p_indent) + "\n";
}

void add_dead_code_insertion(int p_offset, const String &p_indent, bool p_static_class, Vector<DeadCodeInsertion> &r_insertions) {
	if (p_offset < 0) {
		return;
	}

	DeadCodeInsertion insertion;
	insertion.offset = p_offset;
	insertion.indent = p_indent;
	insertion.static_class = p_static_class;
	r_insertions.push_back(insertion);
}

String get_empty_class_body_indent(const String &p_source, const Vector<int> &p_line_offsets, const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr || p_class->outer == nullptr) {
		return String();
	}

	return get_line_indent(p_source, p_line_offsets, p_class->start_line) + "\t";
}

int get_empty_class_insertion_offset(const String &p_source, const Vector<int> &p_line_offsets, const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return -1;
	}

	if (p_class->outer != nullptr && p_class->end_line <= p_class->start_line) {
		return -1;
	}

	const int offset = get_line_start_offset(p_line_offsets, p_class->end_line + 1);
	return offset >= 0 ? offset : p_source.length();
}

void collect_class_insertions(const String &p_source, const Vector<int> &p_line_offsets, const GDScriptParser::ClassNode *p_class, bool p_no_mangle_scope, Vector<DeadCodeInsertion> &r_insertions) {
	if (p_class == nullptr) {
		return;
	}

	if (is_interface_class(p_class)) {
		return;
	}

	const bool no_mangle_scope = p_no_mangle_scope || is_no_mangle_class(p_class);
	if (!no_mangle_scope) {
		const bool static_class = is_static_class(p_class);
		if (p_class->members.is_empty()) {
			const int offset = get_empty_class_insertion_offset(p_source, p_line_offsets, p_class);
			const String indent = get_empty_class_body_indent(p_source, p_line_offsets, p_class);
			add_dead_code_insertion(offset, indent, static_class, r_insertions);
		} else {
			const GDScriptParser::Node *first_member = p_class->members[0].get_source_node();
			if (first_member != nullptr) {
				const int first_member_start_line = get_annotated_start_line(first_member);
				const int offset = get_line_start_offset(p_line_offsets, first_member_start_line);
				const String indent = get_line_indent(p_source, p_line_offsets, first_member_start_line);
				add_dead_code_insertion(offset, indent, static_class, r_insertions);
			}

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

				const int next_member_start_line = get_annotated_start_line(next_member);
				const String indent = get_line_indent(p_source, p_line_offsets, next_member_start_line);
				add_dead_code_insertion(offset, indent, static_class, r_insertions);
			}
		}
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			collect_class_insertions(p_source, p_line_offsets, member.m_class, no_mangle_scope, r_insertions);
		}
	}
}

} // namespace

namespace WGodotGDScriptDeadCodeInjection {

void analyze_in_class_dead_code(const String &p_source, const GDScriptParser::ClassNode *p_tree, Vector<Insertion> &r_insertions) {
	Vector<int> line_offsets;
	build_line_offsets(p_source, line_offsets);
	r_insertions.clear();
	collect_class_insertions(p_source, line_offsets, p_tree, false, r_insertions);
}

void make_in_class_dead_code_edits(const String &p_source, const String &p_path, const WGodotGDScriptExportTransform::TransformOptions &p_options, const Vector<Insertion> &p_insertions, Vector<WGodotGDScriptExportTransform::SourceEdit> &r_edits) {
	if (p_insertions.is_empty()) {
		return;
	}
	RandomPCG random;
	random.seed(make_seed(p_source, p_path, p_options));
	Vector<Insertion> insertions = p_insertions;
	const int gap_count = MIN(insertions.size(), MIN(p_options.max_dead_code_gaps_per_file, 5));
	for (int i = 0; i < gap_count; i++) {
		const int selected = i + random.rand(insertions.size() - i);
		SWAP(insertions.write[i], insertions.write[selected]);
	}
	insertions.resize(gap_count);
	insertions.sort_custom<InsertionSort>();
	uint64_t unique_id = 1;
	for (int i = insertions.size() - 1; i >= 0; i--) {
		const Insertion &insertion = insertions[i];
		const DeadCodeTemplatePool template_pool = get_template_pool(insertion.static_class);
		const int injection_count = get_random_injection_count(random, p_options.min_in_class_dead_code_injection, p_options.max_in_class_dead_code_injection);
		String block;
		for (int j = 0; j < injection_count; j++) {
			block += make_dead_code_block(random, insertion.indent, unique_id, template_pool);
		}
		if (block.is_empty()) {
			continue;
		}
		if (!r_edits.is_empty() && r_edits[r_edits.size() - 1].start == insertion.offset) {
			r_edits.write[r_edits.size() - 1].text = block + r_edits[r_edits.size() - 1].text;
		} else {
			WGodotGDScriptExportTransform::SourceEdit edit;
			edit.start = insertion.offset;
			edit.end = insertion.offset;
			edit.text = block;
			r_edits.push_back(edit);
		}
	}
}

} // namespace WGodotGDScriptDeadCodeInjection
