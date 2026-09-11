// wgodot-changes::file

#include "gdscript_parser.h"

void GDScriptParser::parse_wgodot_interface(bool p_global_name) {
	current_class->wgodot_is_interface = true;
	current_class->wgodot_interface_global_name = p_global_name;
	current_class->is_abstract = true;

	if (consume(GDScriptTokenizer::Token::IDENTIFIER, p_global_name ? R"(Expected identifier for the global interface name after "interface_name".)" : R"(Expected identifier for the interface name after "interface".)")) {
		IdentifierNode *interface_identifier = parse_identifier();
		current_class->wgodot_interface_name = interface_identifier->name;
		if (p_global_name) {
			current_class->identifier = interface_identifier;
			current_class->fqcn = String(current_class->identifier->name);
		}
	}

	if (p_global_name && script_path.begins_with("res://") && script_path.contains("::")) {
		push_error(R"("interface_name" isn't allowed in built-in scripts.)");
	}

	make_completion_context(COMPLETION_DECLARATION, current_class);
	end_statement(p_global_name ? "interface_name statement" : "interface statement");
}

void GDScriptParser::parse_wgodot_implements() {
	do {
		ClassNode::WGodotInterfaceReference interface_ref;
		if (match(GDScriptTokenizer::Token::LITERAL)) {
			interface_ref.path_literal = parse_literal();
			const Variant &path = interface_ref.path_literal->value;
			if (path.get_type() != Variant::STRING) {
				push_error("An implemented interface path must be a string.");
			} else {
				interface_ref.path = path;
			}
		} else {
			make_completion_context(COMPLETION_WGODOT_INTERFACE_TYPE, current_class, 0);
			if (!consume(GDScriptTokenizer::Token::IDENTIFIER, "Expected interface name after 'implements'.")) {
				return;
			}
			interface_ref.identifiers.push_back(parse_identifier());
		}
		while (match(GDScriptTokenizer::Token::PERIOD)) {
			make_completion_context(COMPLETION_WGODOT_INTERFACE_TYPE, current_class, interface_ref.identifiers.size());
			if (!consume(GDScriptTokenizer::Token::IDENTIFIER, "Expected interface name after '.'.")) {
				return;
			}
			interface_ref.identifiers.push_back(parse_identifier());
		}
		current_class->wgodot_implements.push_back(interface_ref);
	} while (match(GDScriptTokenizer::Token::COMMA));
}
