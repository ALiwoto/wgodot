// wgodot-changes::file
#pragma once

#include "wgodot_cpp_project.h"
#include "wgodot_cpp_signatures.h"

class MethodBind;
class WGodotCppAsync;

class WGodotCppEmitter {
	friend class WGodotCppAsync;
	const WGodotCppProject &project;
	WGodotCppSignatures signatures;
	HashSet<const GDScriptParser::Node *> rendering_signatures;
	const WGodotCppProject::Class *current_class = nullptr;
	HashMap<String, String> files;
	Vector<String> diagnostics;
	HashMap<StringName, String> native_headers;
	HashMap<StringName, String> native_cpp_names;
	HashMap<String, String> native_methods;
	HashSet<String> used_native_headers;
	HashSet<String> class_native_headers;
	HashSet<String> class_dependencies;
	HashSet<String> class_call_headers;
	HashMap<String, String> class_resource_types;
	HashSet<const GDScriptParser::LambdaNode *> class_lambdas;
	String class_lambda_declarations;
	String class_lambda_definitions;
	const GDScriptParser::FunctionNode *current_function = nullptr;
	bool function_failed = false;
	HashMap<const GDScriptParser::ExpressionNode *, String> expression_overrides;
	HashMap<const GDScriptParser::Node *, String> local_overrides;
	HashMap<const GDScriptParser::Node *, String> object_views;
	const GDScriptParser::ExpressionNode *iterated_expression = nullptr;
	bool emitted_array_range = false;
	const GDScriptParser::CallNode *awaited_call = nullptr;

	void unsupported(const GDScriptParser::Node *p_node, const String &p_feature);
	String class_name(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_origin);
	StringName native_base(const GDScriptParser::DataType &p_type) const;
	String type(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_origin);
	String signature_type(const GDScriptParser::Node *p_origin, bool p_signal = false);
	String function_result(const GDScriptParser::FunctionNode *p_function);
	bool native_only(const GDScriptParser::DataType &p_type) const;
	String callback_call(const GDScriptParser::CallNode *p_call);
	bool validate_callback(const GDScriptParser::ExpressionNode *p_source, const WGodotCppSignatures::Signature &p_target, bool p_discard_result = false);
	bool is_warray(const GDScriptParser::DataType &p_type) const;
	GDScriptParser::DataType expression_type(const GDScriptParser::ExpressionNode *p_expression) const;
	GDScriptParser::DataType variable_type(const GDScriptParser::VariableNode *p_variable) const;
	bool has_native_value_signature(const GDScriptParser::FunctionNode *p_function) const;
	bool validate_array_conversion(const GDScriptParser::ExpressionNode *p_value, const GDScriptParser::DataType &p_target, const GDScriptParser::Node *p_target_origin = nullptr);
	String array_literal(const GDScriptParser::ArrayNode *p_array, const GDScriptParser::DataType &p_target);
	String warray_call(const GDScriptParser::CallNode *p_call, bool p_to_array = false);
	String engine_argument(const GDScriptParser::ExpressionNode *p_value, Variant::Type p_target);
	bool is_array_duplicate(const GDScriptParser::ExpressionNode *p_value) const;
	String literal(const Variant &p_value, const GDScriptParser::Node *p_origin);
	String converted(const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::DataType &p_target, const GDScriptParser::Node *p_target_origin = nullptr);
	String truth(const GDScriptParser::ExpressionNode *p_expression);
	const WGodotCppProject::Class *member_owner(const GDScriptParser::ClassNode *p_class, const StringName &p_name) const;
	String member(const GDScriptParser::ExpressionNode *p_base, const StringName &p_name, const GDScriptParser::ExpressionNode *p_origin);
	String call(const GDScriptParser::CallNode *p_call);
	const GDScriptParser::FunctionNode *interface_method(const GDScriptParser::CallNode *p_call) const;
	String builtin_call(const GDScriptParser::CallNode *p_call);
	String global_call(const GDScriptParser::CallNode *p_call);
	String variant_type(Variant::Type p_type) const;
	String operation(Variant::Operator p_operation, const GDScriptParser::DataType &p_result, const GDScriptParser::DataType &p_left_type, const GDScriptParser::DataType &p_right_type, const String &p_left, const String &p_right, const GDScriptParser::Node *p_origin);
	String cast(const GDScriptParser::CastNode *p_cast);
	String type_test(const GDScriptParser::TypeTestNode *p_test);
	void initialize_native_methods();
	bool validate_native_arguments(const MethodBind *p_method, const GDScriptParser::Node *p_origin);
	bool validate_builtin_arguments(Variant::Type p_type, const StringName &p_method, const GDScriptParser::Node *p_origin);
	bool native_override(const MethodBind *p_method, const String &p_receiver, const Vector<String> &p_arguments, const String &p_result, const GDScriptParser::Node *p_origin, String &r_code);
	String native_call(const GDScriptParser::CallNode *p_call, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::DataType &p_base_type);
	String native_adapter(const StringName &p_owner, const StringName &p_name, bool p_static, bool p_vararg);
	String native_invoke(const MethodBind *p_method, const String &p_receiver, Vector<String> p_arguments, const String &p_result, const GDScriptParser::Node *p_origin);
	String native_property(const GDScriptParser::ExpressionNode *p_base, const StringName &p_name, const GDScriptParser::ExpressionNode *p_origin, const GDScriptParser::ExpressionNode *p_value = nullptr);
	String property_access(const GDScriptParser::DataType &p_base_type, const StringName &p_name, const GDScriptParser::ExpressionNode *p_origin, const String &p_receiver, const String &p_value = String());
	String store_identifier(const GDScriptParser::IdentifierNode *p_target, const String &p_value);
	StringName accessor_name(const GDScriptParser::VariableNode *p_variable, bool p_setter) const;
	const GDScriptParser::Node *local_source(const GDScriptParser::IdentifierNode *p_identifier) const;
	String assignment(const GDScriptParser::AssignmentNode *p_assignment);
	String expression(const GDScriptParser::ExpressionNode *p_expression);
	String receiver_expression(const GDScriptParser::ExpressionNode *p_expression);
	String array_iteration_element(const GDScriptParser::ExpressionNode *p_expression);
	String array_iteration_result(const String &p_call, const GDScriptParser::Node *p_origin);
	String iteration(const GDScriptParser::ForNode *p_loop, int p_indent);
	String suite(const GDScriptParser::SuiteNode *p_suite, int p_indent);
	String match_condition(const GDScriptParser::PatternNode *p_pattern, const String &p_value);
	String function(const GDScriptParser::FunctionNode *p_function, String &r_declaration, const String &p_cpp_name = String());
	String lambda(const GDScriptParser::LambdaNode *p_lambda);
	void emit_class(const WGodotCppProject::Class &p_class);
	void emit_interface(const WGodotCppProject::Class &p_class);
	String register_interfaces();
	void emit_virtuals(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions);
	void register_class(const WGodotCppProject::Class &p_class, HashSet<String> &r_registered, String &r_code);

public:
	explicit WGodotCppEmitter(const WGodotCppProject &p_project);
	Error generate();
	Error write(const String &p_directory) const;
	const Vector<String> &get_diagnostics() const { return diagnostics; }
};
