// wgodot-changes::file
#pragma once

#include "wgodot_cpp_project.h"

class WGodotCppEmitter;

// Export-only signature propagation. Never changes the editor's cached AST.
class WGodotCppSignatures {
	using Parser = GDScriptParser;

public:
	struct Value {
		Parser::DataType type;
		const Parser::Node *origin = nullptr;
	};
	struct Signature {
		Value result;
		Vector<Value> arguments;
		int defaults = 0;
		bool signal = false;
		int priority = 1;
	};

private:
	struct Scope {
		const WGodotCppProject::Class *owner = nullptr;
		const Parser::FunctionNode *function = nullptr;
	};
	struct Link {
		const Parser::Node *source;
		const Parser::Node *target;
		bool reverse;
	};
	const WGodotCppProject &project;
	HashMap<const Parser::Node *, Signature> known;
	HashMap<const Parser::Node *, Signature> demands;
	HashMap<const Parser::Node *, Scope> scopes;
	Vector<const Parser::Node *> nodes;
	Vector<Link> links;
	HashSet<const Parser::Node *> discarded;
	HashSet<const Parser::Node *> producers;
	const Parser::Node *member(const Parser::DataType &p_type, const StringName &p_name) const;
	const Parser::Node *source(const Parser::Node *p_node) const;
	void link(const Parser::Node *p_source, const Parser::Node *p_target, bool p_reverse = true);
	void collect(const Parser::Node *p_node, Scope p_scope);
	Signature function(const Parser::FunctionNode *p_function) const;
	void seed(const Parser::Node *p_node);
	bool demand(const Parser::Node *p_node, const Signature &p_signature);

public:
	explicit WGodotCppSignatures(const WGodotCppProject &p_project) : project(p_project) {}
	void analyze();
	const Signature *get(const Parser::Node *p_node) const;
	static bool contains_signature(const Parser::DataType &p_type);
};
