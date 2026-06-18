// wgodot-changes::file
/**************************************************************************/
/*  export_context.cpp                                                    */
/**************************************************************************/

#include "export_context.h"

#include "obfuscation_names.h"

#include "../gdscript_utility_functions.h"

#include "core/config/project_settings.h"
#include "core/object/class_db.h"
#include "core/object/script_language.h"
#include "core/string/ustring.h"
#include "core/templates/list.h"
#include "core/templates/local_vector.h"
#include "core/variant/variant.h"

namespace {

String get_class_primary_key(const GDScriptParser::ClassNode *p_class, const String &p_script_path) {
	if (p_class == nullptr) {
		return String();
	}
	if (!p_class->fqcn.is_empty()) {
		return p_class->fqcn;
	}
	if (p_class->outer == nullptr && !p_script_path.is_empty()) {
		return p_script_path;
	}
	if (p_class->identifier != nullptr && !p_class->identifier->name.is_empty()) {
		return String(p_class->identifier->name);
	}
	return String();
}

String make_random_path_segment(RandomPCG &r_random) {
	static const char *letters = "abcdefghijklmnopqrstuvwxyz";
	return String::chr(letters[r_random.rand(26)]);
}

String make_short_obfuscated_script_path(RandomPCG &r_random) {
	const int segment_count = 3 + r_random.rand(6);
	String path = "res://";
	for (int i = 0; i < segment_count; i++) {
		if (i > 0) {
			path += "/";
		}
		path += make_random_path_segment(r_random);
	}
	return path + ".gd";
}

String make_hash_obfuscated_script_path(const String &p_source_path, RandomPCG &r_random) {
	const String seed = p_source_path + "::" + String::num_uint64(r_random.rand()) + "::" + String::num_uint64(r_random.rand());
	return "res://a/f/" + seed.sha256_text() + ".gd";
}

String make_random_unicode_path_segment(RandomPCG &r_random) {
	static const char32_t codepoints[] = {
		0x3042, // Hiragana letter A.
		0x3044, // Hiragana letter I.
		0x30A2, // Katakana letter A.
		0x30AB, // Katakana letter Ka.
		0x4E00, // CJK ideograph.
		0x4E2D, // CJK ideograph.
		0x03BB, // Greek small letter lambda.
		0x03A9, // Greek capital letter omega.
		0x0416, // Cyrillic capital letter Zhe.
		0x05D0, // Hebrew letter Alef.
		0x0627, // Arabic letter Alef.
		0x2605, // Black star.
		0x2665, // Black heart suit.
		0x1F600, // Emoji code point.
		0x1F47E, // Emoji code point.
	};
	const int codepoint_count = sizeof(codepoints) / sizeof(codepoints[0]);
	const int length = 1 + r_random.rand(3);

	String segment;
	for (int i = 0; i < length; i++) {
		segment += String::chr(codepoints[r_random.rand(codepoint_count)]);
	}
	return segment;
}

String make_unicode_obfuscated_script_path(RandomPCG &r_random) {
	const int segment_count = 2 + r_random.rand(4);
	String path = "res://";
	for (int i = 0; i < segment_count; i++) {
		if (i > 0) {
			path += "/";
		}
		path += make_random_unicode_path_segment(r_random);
	}
	return path + ".gd";
}

String make_obfuscated_script_path(WGodotGDScriptExportTransform::ObfuscationStrategy p_strategy, const String &p_source_path, RandomPCG &r_random) {
	switch (p_strategy) {
		case WGodotGDScriptExportTransform::OBFUSCATION_STRATEGY_SHORT:
			return make_short_obfuscated_script_path(r_random);
		case WGodotGDScriptExportTransform::OBFUSCATION_STRATEGY_HASH:
			return make_hash_obfuscated_script_path(p_source_path, r_random);
		case WGodotGDScriptExportTransform::OBFUSCATION_STRATEGY_UNICODE:
			return make_unicode_obfuscated_script_path(r_random);
	}

	return make_short_obfuscated_script_path(r_random);
}

void reserve_function_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::FunctionNode *p_function);
void reserve_node_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::Node *p_node);
void reserve_expression_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::ExpressionNode *p_expression);

void reserve_type_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::TypeNode *p_type) {
	if (p_type == nullptr) {
		return;
	}

	for (const GDScriptParser::TypeNode *container_type : p_type->container_types) {
		reserve_type_declaration_names_for_global_classes(r_context, container_type);
	}
}

void reserve_parameter_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::ParameterNode *p_parameter) {
	if (p_parameter == nullptr) {
		return;
	}

	if (p_parameter->identifier != nullptr) {
		r_context.reserve_global_class_name(p_parameter->identifier->name);
	}
	reserve_type_declaration_names_for_global_classes(r_context, p_parameter->datatype_specifier);
	reserve_expression_declaration_names_for_global_classes(r_context, p_parameter->initializer);
}

void reserve_suite_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::SuiteNode *p_suite) {
	if (p_suite == nullptr) {
		return;
	}

	for (const GDScriptParser::SuiteNode::Local &local : p_suite->locals) {
		r_context.reserve_global_class_name(local.name);
	}

	for (const GDScriptParser::Node *statement : p_suite->statements) {
		reserve_node_declaration_names_for_global_classes(r_context, statement);
	}
}

void reserve_function_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::FunctionNode *p_function) {
	if (p_function == nullptr) {
		return;
	}

	if (p_function->identifier != nullptr) {
		r_context.reserve_global_class_name(p_function->identifier->name);
	}
	for (const GDScriptParser::ParameterNode *parameter : p_function->parameters) {
		reserve_parameter_declaration_names_for_global_classes(r_context, parameter);
	}
	reserve_parameter_declaration_names_for_global_classes(r_context, p_function->rest_parameter);
	reserve_type_declaration_names_for_global_classes(r_context, p_function->return_type);
	reserve_suite_declaration_names_for_global_classes(r_context, p_function->body);
}

void reserve_expression_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::ExpressionNode *p_expression) {
	if (p_expression == nullptr) {
		return;
	}

	switch (p_expression->type) {
		case GDScriptParser::Node::ARRAY: {
			const GDScriptParser::ArrayNode *array = static_cast<const GDScriptParser::ArrayNode *>(p_expression);
			for (const GDScriptParser::ExpressionNode *element : array->elements) {
				reserve_expression_declaration_names_for_global_classes(r_context, element);
			}
		} break;
		case GDScriptParser::Node::ASSIGNMENT: {
			const GDScriptParser::AssignmentNode *assignment = static_cast<const GDScriptParser::AssignmentNode *>(p_expression);
			reserve_expression_declaration_names_for_global_classes(r_context, assignment->assignee);
			reserve_expression_declaration_names_for_global_classes(r_context, assignment->assigned_value);
		} break;
		case GDScriptParser::Node::AWAIT:
			reserve_expression_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::AwaitNode *>(p_expression)->to_await);
			break;
		case GDScriptParser::Node::BINARY_OPERATOR: {
			const GDScriptParser::BinaryOpNode *binary = static_cast<const GDScriptParser::BinaryOpNode *>(p_expression);
			reserve_expression_declaration_names_for_global_classes(r_context, binary->left_operand);
			reserve_expression_declaration_names_for_global_classes(r_context, binary->right_operand);
		} break;
		case GDScriptParser::Node::CALL: {
			const GDScriptParser::CallNode *call = static_cast<const GDScriptParser::CallNode *>(p_expression);
			reserve_expression_declaration_names_for_global_classes(r_context, call->callee);
			for (const GDScriptParser::ExpressionNode *argument : call->arguments) {
				reserve_expression_declaration_names_for_global_classes(r_context, argument);
			}
		} break;
		case GDScriptParser::Node::CAST: {
			const GDScriptParser::CastNode *cast = static_cast<const GDScriptParser::CastNode *>(p_expression);
			reserve_expression_declaration_names_for_global_classes(r_context, cast->operand);
			reserve_type_declaration_names_for_global_classes(r_context, cast->cast_type);
		} break;
		case GDScriptParser::Node::DICTIONARY: {
			const GDScriptParser::DictionaryNode *dictionary = static_cast<const GDScriptParser::DictionaryNode *>(p_expression);
			for (const GDScriptParser::DictionaryNode::Pair &pair : dictionary->elements) {
				reserve_expression_declaration_names_for_global_classes(r_context, pair.key);
				reserve_expression_declaration_names_for_global_classes(r_context, pair.value);
			}
		} break;
		case GDScriptParser::Node::LAMBDA:
			reserve_function_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::LambdaNode *>(p_expression)->function);
			break;
		case GDScriptParser::Node::PRELOAD:
			reserve_expression_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::PreloadNode *>(p_expression)->path);
			break;
		case GDScriptParser::Node::SUBSCRIPT: {
			const GDScriptParser::SubscriptNode *subscript = static_cast<const GDScriptParser::SubscriptNode *>(p_expression);
			reserve_expression_declaration_names_for_global_classes(r_context, subscript->base);
			if (!subscript->is_attribute) {
				reserve_expression_declaration_names_for_global_classes(r_context, subscript->index);
			}
		} break;
		case GDScriptParser::Node::TERNARY_OPERATOR: {
			const GDScriptParser::TernaryOpNode *ternary = static_cast<const GDScriptParser::TernaryOpNode *>(p_expression);
			reserve_expression_declaration_names_for_global_classes(r_context, ternary->condition);
			reserve_expression_declaration_names_for_global_classes(r_context, ternary->true_expr);
			reserve_expression_declaration_names_for_global_classes(r_context, ternary->false_expr);
		} break;
		case GDScriptParser::Node::TYPE_TEST: {
			const GDScriptParser::TypeTestNode *type_test = static_cast<const GDScriptParser::TypeTestNode *>(p_expression);
			reserve_expression_declaration_names_for_global_classes(r_context, type_test->operand);
			reserve_type_declaration_names_for_global_classes(r_context, type_test->test_type);
		} break;
		case GDScriptParser::Node::UNARY_OPERATOR:
			reserve_expression_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::UnaryOpNode *>(p_expression)->operand);
			break;
		default:
			break;
	}
}

void reserve_class_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr) {
		return;
	}

	if (p_class->identifier != nullptr) {
		r_context.reserve_global_class_name(p_class->identifier->name);
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		const String member_name = member.get_name();
		if (!member_name.is_empty()) {
			r_context.reserve_global_class_name(StringName(member_name));
		}
		reserve_node_declaration_names_for_global_classes(r_context, member.get_source_node());
	}
}

void reserve_node_declaration_names_for_global_classes(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::Node *p_node) {
	if (p_node == nullptr) {
		return;
	}

	switch (p_node->type) {
		case GDScriptParser::Node::CLASS:
			reserve_class_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::ClassNode *>(p_node));
			break;
		case GDScriptParser::Node::CONSTANT: {
			const GDScriptParser::ConstantNode *constant = static_cast<const GDScriptParser::ConstantNode *>(p_node);
			if (constant->identifier != nullptr) {
				r_context.reserve_global_class_name(constant->identifier->name);
			}
			reserve_type_declaration_names_for_global_classes(r_context, constant->datatype_specifier);
			reserve_expression_declaration_names_for_global_classes(r_context, constant->initializer);
		} break;
		case GDScriptParser::Node::FUNCTION:
			reserve_function_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::FunctionNode *>(p_node));
			break;
		case GDScriptParser::Node::VARIABLE: {
			const GDScriptParser::VariableNode *variable = static_cast<const GDScriptParser::VariableNode *>(p_node);
			if (variable->identifier != nullptr) {
				r_context.reserve_global_class_name(variable->identifier->name);
			}
			reserve_type_declaration_names_for_global_classes(r_context, variable->datatype_specifier);
			reserve_expression_declaration_names_for_global_classes(r_context, variable->initializer);
			if (variable->property == GDScriptParser::VariableNode::PROP_INLINE) {
				reserve_function_declaration_names_for_global_classes(r_context, variable->setter);
				reserve_function_declaration_names_for_global_classes(r_context, variable->getter);
			}
		} break;
		case GDScriptParser::Node::SIGNAL: {
			const GDScriptParser::SignalNode *signal = static_cast<const GDScriptParser::SignalNode *>(p_node);
			if (signal->identifier != nullptr) {
				r_context.reserve_global_class_name(signal->identifier->name);
			}
			for (const GDScriptParser::ParameterNode *parameter : signal->parameters) {
				reserve_parameter_declaration_names_for_global_classes(r_context, parameter);
			}
		} break;
		case GDScriptParser::Node::SUITE:
			reserve_suite_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::SuiteNode *>(p_node));
			break;
		case GDScriptParser::Node::LAMBDA:
			reserve_function_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::LambdaNode *>(p_node)->function);
			break;
		default:
			if (p_node->is_expression()) {
				reserve_expression_declaration_names_for_global_classes(r_context, static_cast<const GDScriptParser::ExpressionNode *>(p_node));
			}
			break;
	}
}

void index_class(WGodotGDScriptExportTransform::ExportContext &r_context, const GDScriptParser::ClassNode *p_class, const String &p_script_path, bool p_no_mangle_scope) {
	if (p_class == nullptr) {
		return;
	}

	r_context.reserve_script_global_class_name(p_class);

	const bool no_mangle_scope = p_no_mangle_scope || p_class->wgodot_no_mangle;
	const bool obfuscate_scope = p_class->wgodot_obfuscate;
	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		const String member_name = member.get_name();
		if (!member_name.is_empty()) {
			r_context.reserve_member_name(StringName(member_name));
		}
	}

	for (const GDScriptParser::ClassNode::Member &member : p_class->members) {
		if (member.type == GDScriptParser::ClassNode::Member::CLASS) {
			index_class(r_context, member.m_class, p_script_path, no_mangle_scope);
			continue;
		}

		if (no_mangle_scope) {
			continue;
		}

		StringName member_name;
		if (member.type == GDScriptParser::ClassNode::Member::FUNCTION &&
				member.function != nullptr &&
				member.function->identifier != nullptr &&
				(obfuscate_scope || member.function->wgodot_obfuscate) &&
				!member.function->wgodot_no_mangle) {
			member_name = member.function->identifier->name;
		} else if (member.type == GDScriptParser::ClassNode::Member::VARIABLE &&
				member.variable != nullptr &&
				member.variable->identifier != nullptr &&
				(obfuscate_scope || member.variable->wgodot_obfuscate) &&
				!member.variable->wgodot_no_mangle) {
			member_name = member.variable->identifier->name;
		} else {
			continue;
		}

		Vector<String> keys;
		WGodotGDScriptExportTransform::ExportContext::make_member_keys(p_class, p_script_path, member_name, keys);
		if (keys.is_empty()) {
			continue;
		}

		const String obfuscated_name = r_context.get_or_create_member_rename(keys[0]);
		for (int i = 1; i < keys.size(); i++) {
			r_context.bind_member_rename(keys[i], obfuscated_name);
		}
	}
}

} // namespace

namespace WGodotGDScriptExportTransform {

void ExportContext::reset() {
	member_renames.clear();
	global_class_renames.clear();
	global_class_renames_by_path.clear();
	builtin_class_aliases.clear();
	builtin_function_aliases.clear();
	builtin_instance_method_aliases.clear();
	builtin_static_method_aliases.clear();
	builtin_instance_property_aliases.clear();
	builtin_static_property_aliases.clear();
	script_path_renames.clear();
	reserved_member_names.clear();
	reserved_global_class_names.clear();
	reserved_script_paths.clear();
	reserve_registered_global_class_names();
	reserve_builtin_class_names();
	reserve_builtin_function_names();
	obfuscation_random.randomize();
}

void ExportContext::set_options(const TransformOptions &p_options) {
	options = p_options;
}

const TransformOptions &ExportContext::get_options() const {
	return options;
}

void ExportContext::reserve_member_name(const StringName &p_name) {
	if (!p_name.is_empty()) {
		reserved_member_names.insert(p_name);
	}
}

void ExportContext::reserve_global_class_name(const StringName &p_name) {
	if (p_name.is_empty()) {
		return;
	}

	reserved_global_class_names.insert(p_name);
	reserved_member_names.insert(p_name);
}

void ExportContext::reserve_script_path(const String &p_path) {
	if (!p_path.is_empty()) {
		reserved_script_paths.insert(p_path);
		if (p_path.get_extension() == "gd") {
			reserved_script_paths.insert(p_path.get_basename() + ".gdc");
		}
	}
}

void ExportContext::reserve_script_global_class_name(const GDScriptParser::ClassNode *p_class) {
	if (p_class == nullptr ||
			p_class->outer != nullptr ||
			p_class->identifier == nullptr ||
			p_class->identifier->name.is_empty() ||
			p_class->fqcn.begins_with("res://")) {
		return;
	}

	reserve_global_class_name(p_class->identifier->name);
}

void ExportContext::reserve_script_declaration_names_for_global_classes(const GDScriptParser::ClassNode *p_class) {
	reserve_class_declaration_names_for_global_classes(*this, p_class);
}

void ExportContext::reserve_registered_global_class_names() {
	LocalVector<StringName> global_classes;
	ScriptServer::get_global_class_list(global_classes);
	for (const StringName &global_class : global_classes) {
		reserve_global_class_name(global_class);
	}
}

void ExportContext::reserve_builtin_class_names() {
	for (int i = 0; i < Variant::VARIANT_MAX; i++) {
		const StringName builtin_name = Variant::get_type_name(Variant::Type(i));
		if (!builtin_name.is_empty()) {
			reserve_global_class_name(builtin_name);
		}
	}

	LocalVector<StringName> native_classes;
	ClassDB::get_class_list(native_classes);
	for (const StringName &native_class : native_classes) {
		if (ClassDB::is_class_exposed(native_class)) {
			reserve_global_class_name(native_class);
		}
	}

	if (ProjectSettings::get_singleton() != nullptr) {
		const HashMap<StringName, ProjectSettings::AutoloadInfo> &autoloads = ProjectSettings::get_singleton()->get_autoload_list();
		for (const KeyValue<StringName, ProjectSettings::AutoloadInfo> &autoload : autoloads) {
			reserve_global_class_name(autoload.key);
		}
	}
}

void ExportContext::reserve_builtin_function_names() {
	List<StringName> utility_functions;
	Variant::get_utility_function_list(&utility_functions);
	GDScriptUtilityFunctions::get_function_list(&utility_functions);

	for (const StringName &function_name : utility_functions) {
		reserve_global_class_name(function_name);
	}
}

void ExportContext::seed_reserved_obfuscated_names(HashSet<StringName> &r_reserved_names) const {
	for (const StringName &global_class : reserved_global_class_names) {
		r_reserved_names.insert(global_class);
	}
}

String ExportContext::make_obfuscated_name(HashSet<StringName> &r_reserved_names, const String &p_warning_context) {
	seed_reserved_obfuscated_names(r_reserved_names);
	return WGodotGDScriptExportTransform::make_obfuscated_name(options.obfuscation_strategy, obfuscation_random, r_reserved_names, p_warning_context, options.binary_tokens_export);
}

String ExportContext::make_member_key(const String &p_class_key, const StringName &p_member_name) {
	if (p_class_key.is_empty() || p_member_name.is_empty()) {
		return String();
	}

	return p_class_key + "::" + String(p_member_name);
}

void ExportContext::make_member_keys(const GDScriptParser::ClassNode *p_class, const String &p_script_path, const StringName &p_member_name, Vector<String> &r_keys) {
	const String primary_key = get_class_primary_key(p_class, p_script_path);
	const String primary_member_key = make_member_key(primary_key, p_member_name);
	if (!primary_member_key.is_empty()) {
		r_keys.push_back(primary_member_key);
	}

	if (p_class != nullptr && p_class->outer == nullptr && !p_script_path.is_empty() && primary_key != p_script_path) {
		const String script_member_key = make_member_key(p_script_path, p_member_name);
		if (!script_member_key.is_empty()) {
			r_keys.push_back(script_member_key);
		}
	}
}

void ExportContext::index_script(const GDScriptParser::ClassNode *p_class, const String &p_script_path) {
	index_class(*this, p_class, p_script_path, false);
}

void ExportContext::index_global_class_rename(const GDScriptParser::ClassNode *p_class, const String &p_script_path) {
	if (p_class == nullptr ||
			p_class->outer != nullptr ||
			p_class->identifier == nullptr ||
			p_class->identifier->name.is_empty() ||
			!p_class->wgodot_obfuscate ||
			p_class->wgodot_no_mangle ||
			p_class->fqcn.begins_with("res://")) {
		return;
	}

	(void)get_or_create_global_class_rename(p_class->identifier->name, p_script_path);
}

String ExportContext::get_or_create_member_rename(const String &p_key) {
	if (p_key.is_empty()) {
		return String();
	}

	const String *existing = member_renames.getptr(p_key);
	if (existing != nullptr) {
		return *existing;
	}

	const String obfuscated_name = make_obfuscated_name(reserved_member_names, "member name");
	member_renames[p_key] = obfuscated_name;
	return obfuscated_name;
}

void ExportContext::bind_member_rename(const String &p_key, const String &p_obfuscated_name) {
	if (p_key.is_empty() || p_obfuscated_name.is_empty() || member_renames.has(p_key)) {
		return;
	}

	member_renames[p_key] = p_obfuscated_name;
	reserved_member_names.insert(StringName(p_obfuscated_name));
}

const String *ExportContext::get_member_rename(const String &p_key) const {
	if (p_key.is_empty()) {
		return nullptr;
	}

	return member_renames.getptr(p_key);
}

StringName ExportContext::get_or_create_global_class_rename(const StringName &p_name, const String &p_path) {
	if (p_name.is_empty()) {
		return StringName();
	}

	if (const StringName *existing = global_class_renames.getptr(p_name)) {
		if (!p_path.is_empty()) {
			global_class_renames_by_path[p_path] = *existing;
		}
		return *existing;
	}

	const String obfuscated_name = WGodotGDScriptExportTransform::make_obfuscated_name(options.obfuscation_strategy, obfuscation_random, reserved_global_class_names, "global class name", options.binary_tokens_export);
	const StringName obfuscated_string_name(obfuscated_name);
	global_class_renames[p_name] = obfuscated_string_name;
	if (!p_path.is_empty()) {
		global_class_renames_by_path[p_path] = obfuscated_string_name;
	}
	reserved_member_names.insert(obfuscated_string_name);
	return obfuscated_string_name;
}

const StringName *ExportContext::get_global_class_rename(const StringName &p_name) const {
	if (p_name.is_empty()) {
		return nullptr;
	}

	return global_class_renames.getptr(p_name);
}

const StringName *ExportContext::get_global_class_rename_by_path(const String &p_path) const {
	if (p_path.is_empty()) {
		return nullptr;
	}

	return global_class_renames_by_path.getptr(p_path);
}

String ExportContext::get_or_create_script_path_rename(const String &p_path) {
	if (p_path.is_empty()) {
		return String();
	}

	if (const String *existing = script_path_renames.getptr(p_path)) {
		return *existing;
	}

	for (int attempt = 0; attempt < 10000; attempt++) {
		const String obfuscated_path = make_obfuscated_script_path(options.file_path_obfuscation_strategy, p_path, obfuscation_random);
		if (!reserved_script_paths.has(obfuscated_path) && !reserved_script_paths.has(obfuscated_path.get_basename() + ".gdc")) {
			script_path_renames[p_path] = obfuscated_path;
			reserve_script_path(obfuscated_path);
			return obfuscated_path;
		}
	}

	WARN_PRINT("WGodot failed to generate a unique obfuscated script path for '" + p_path + "'. Keeping the original path.");
	return String();
}

const String *ExportContext::get_script_path_rename(const String &p_path) const {
	if (p_path.is_empty()) {
		return nullptr;
	}

	return script_path_renames.getptr(p_path);
}

String ExportContext::get_exported_script_path(const String &p_path) const {
	const String *renamed_path = get_script_path_rename(p_path);
	if (renamed_path == nullptr) {
		return String();
	}

	if (options.binary_tokens_export && renamed_path->get_extension() == "gd") {
		return renamed_path->get_basename() + ".gdc";
	}
	return *renamed_path;
}

StringName ExportContext::get_or_create_builtin_class_alias(const StringName &p_name) {
	if (p_name.is_empty()) {
		return StringName();
	}

	if (const StringName *existing = builtin_class_aliases.getptr(p_name)) {
		return *existing;
	}

	const String alias = WGodotGDScriptExportTransform::make_obfuscated_name(options.obfuscation_strategy, obfuscation_random, reserved_global_class_names, "built-in class alias", options.binary_tokens_export);
	const StringName alias_name(alias);
	builtin_class_aliases[p_name] = alias_name;
	reserved_global_class_names.insert(alias_name);
	reserved_member_names.insert(alias_name);
	return alias_name;
}

const StringName *ExportContext::get_builtin_class_alias(const StringName &p_name) const {
	if (p_name.is_empty()) {
		return nullptr;
	}

	return builtin_class_aliases.getptr(p_name);
}

const HashMap<StringName, StringName> &ExportContext::get_builtin_class_aliases() const {
	return builtin_class_aliases;
}

StringName ExportContext::get_or_create_builtin_function_alias(const StringName &p_name) {
	if (p_name.is_empty()) {
		return StringName();
	}

	if (const StringName *existing = builtin_function_aliases.getptr(p_name)) {
		return *existing;
	}

	const String alias = WGodotGDScriptExportTransform::make_obfuscated_name(options.obfuscation_strategy, obfuscation_random, reserved_global_class_names, "built-in function alias", options.binary_tokens_export);
	const StringName alias_name(alias);
	builtin_function_aliases[p_name] = alias_name;
	reserved_global_class_names.insert(alias_name);
	reserved_member_names.insert(alias_name);
	return alias_name;
}

const StringName *ExportContext::get_builtin_function_alias(const StringName &p_name) const {
	if (p_name.is_empty()) {
		return nullptr;
	}

	return builtin_function_aliases.getptr(p_name);
}

const HashMap<StringName, StringName> &ExportContext::get_builtin_function_aliases() const {
	return builtin_function_aliases;
}

StringName ExportContext::get_or_create_builtin_member_alias(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) {
	if (p_owner.is_empty() || p_name.is_empty()) {
		return StringName();
	}

	const StringName key(String(p_owner) + "::" + String(p_name));
	HashMap<StringName, StringName> &aliases = p_property ? (p_static ? builtin_static_property_aliases : builtin_instance_property_aliases) : (p_static ? builtin_static_method_aliases : builtin_instance_method_aliases);
	if (const StringName *existing = aliases.getptr(key)) {
		return *existing;
	}

	const String alias = WGodotGDScriptExportTransform::make_obfuscated_name(options.obfuscation_strategy, obfuscation_random, reserved_global_class_names, "built-in member alias", options.binary_tokens_export);
	const StringName alias_name(alias);
	aliases[key] = alias_name;
	reserved_global_class_names.insert(alias_name);
	reserved_member_names.insert(alias_name);
	return alias_name;
}

const StringName *ExportContext::get_builtin_member_alias(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_property) const {
	if (p_owner.is_empty() || p_name.is_empty()) {
		return nullptr;
	}

	const StringName key(String(p_owner) + "::" + String(p_name));
	const HashMap<StringName, StringName> &aliases = p_property ? (p_static ? builtin_static_property_aliases : builtin_instance_property_aliases) : (p_static ? builtin_static_method_aliases : builtin_instance_method_aliases);
	return aliases.getptr(key);
}

const HashMap<StringName, StringName> &ExportContext::get_builtin_instance_method_aliases() const {
	return builtin_instance_method_aliases;
}

const HashMap<StringName, StringName> &ExportContext::get_builtin_static_method_aliases() const {
	return builtin_static_method_aliases;
}

const HashMap<StringName, StringName> &ExportContext::get_builtin_instance_property_aliases() const {
	return builtin_instance_property_aliases;
}

const HashMap<StringName, StringName> &ExportContext::get_builtin_static_property_aliases() const {
	return builtin_static_property_aliases;
}

} // namespace WGodotGDScriptExportTransform
