// wgodot-changes::file
#include "wgodot_cpp_project.h"

#include "modules/gdscript/wgodot_gd/editor/export/source_rewrite.h"

using namespace WGodotGDScriptExportTransform;
using Parser = GDScriptParser;

namespace {
bool editor_only(const Parser::Node *p_node) {
	for (const Parser::AnnotationNode *annotation : p_node->annotations) {
		if (annotation->name == SNAME("@editor_only")) {
			return true;
		}
	}
	return false;
}

void remove_declaration(RewriteContext &r_rewrite, const Parser::Node *p_node) {
	int start = get_offset(r_rewrite, p_node->start_line, p_node->start_column);
	for (const Parser::AnnotationNode *annotation : p_node->annotations) {
		start = MIN(start, get_offset(r_rewrite, annotation->start_line, annotation->start_column));
	}
	const int end = get_offset(r_rewrite, p_node->end_line, p_node->end_column);
	String replacement = r_rewrite.source.substr(start, end - start);
	for (int i = 0; i < replacement.length(); i++) {
		if (replacement[i] != '\n' && replacement[i] != '\r') {
			replacement[i] = ' ';
		}
	}
	// Keep line numbers and a legal suite even when this was its last member.
	replacement = "pass" + replacement.substr(4);
	r_rewrite.replacements.push_back({ start, end, replacement });
}

void filter_members(const Parser::ClassNode *p_class, RewriteContext &r_rewrite, HashMap<String, HashSet<StringName>> &r_members) {
	HashSet<const Parser::Node *> removed;
	for (const Parser::ClassNode::Member &member : p_class->members) {
		const Parser::Node *node = member.type == Parser::ClassNode::Member::ENUM_VALUE ? member.enum_value.parent_enum : member.get_source_node();
		if (!node) {
			continue;
		}
		if (editor_only(node)) {
			r_members[p_class->fqcn].insert(member.get_name());
			if (!removed.has(node)) {
				remove_declaration(r_rewrite, node);
				removed.insert(node);
			}
		} else if (member.type == Parser::ClassNode::Member::CLASS) {
			filter_members(member.m_class, r_rewrite, r_members);
		}
	}
}
} // namespace

Error WGodotCppProject::prepare_editor_only() {
	HashMap<String, Vector<SourceEdit>> edits;
	{
		ExportAnalysis parsed(export_project, export_context);
		for (const String &path : export_project.get_script_paths()) {
			if (!export_project.is_exported(path)) {
				continue;
			}
			Error error = OK;
			const Ref<GDScriptParserRef> ref = parsed.get_parser(path, GDScriptParserRef::PARSED, error);
			if (error != OK) {
				if (ref.is_valid()) {
					for (const Parser::ParserError &message : ref->get_parser()->get_errors()) {
						diagnostics.push_back(vformat("%s:%d: %s", path, message.start_line, message.message));
					}
				}
				return error;
			}
			const Parser::ClassNode *root = ref->get_parser()->get_tree();
			if (editor_only(root)) {
				target.exclude_script(path);
				continue;
			}
			RewriteContext rewrite;
			rewrite.source = export_project.get_source(path)->get_text();
			build_line_offsets(rewrite);
			filter_members(root, rewrite, editor_members);
			if (!rewrite.replacements.is_empty()) {
				edits.insert(path, rewrite.replacements);
			}
		}
	}
	ExportProject filtered;
	String message;
	const Error error = export_project.apply(edits, SNAME("editor_only"), filtered, message);
	if (error != OK) {
		diagnostics.push_back(message);
		return error;
	}
	export_project = filtered.without_scripts(target.get_excluded_scripts());
	return OK;
}

bool WGodotCppProject::is_editor_member(const Parser::ClassNode *p_class, const StringName &p_member) const {
	for (const Parser::ClassNode *node = p_class; node; node = node->base_type.class_type) {
		const HashSet<StringName> *members = editor_members.getptr(node->fqcn);
		if (members && members->has(p_member)) {
			return true;
		}
		if (node->has_member(p_member)) {
			return false;
		}
	}
	return false;
}
