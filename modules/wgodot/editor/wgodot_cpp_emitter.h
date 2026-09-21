// wgodot-changes::file
#pragma once

#include "wgodot_cpp_expression.h"
#include "wgodot_cpp_project.h"
#include "wgodot_cpp_signatures.h"

#include "core/object/wgodot_interface_registry.h"

class MethodBind;
class WGodotCppAsync;

class WGodotCppEmitter {
	friend class WGodotCppAsync;
	using Value = WGodotCppExpression;
	const WGodotCppProject &project;
	WGodotCppSignatures signatures;
	HashSet<const GDScriptParser::Node *> rendering_signatures;
	const WGodotCppProject::Class *current_class = nullptr;
	HashMap<String, String> files;
	Vector<String> diagnostics;
	HashMap<StringName, String> native_headers;
	HashMap<StringName, String> native_cpp_names;
	HashMap<String, String> native_methods;
	HashMap<StringName, HashMap<String, String>> native_access_methods;
	HashSet<StringName> used_native_interfaces;
	HashSet<const GDScriptParser::VariableNode *> interface_property_getters;
	HashSet<const GDScriptParser::VariableNode *> interface_property_setters;
	HashSet<String> used_native_headers;
	HashSet<String> class_native_headers;
	HashSet<String> class_dependencies;
	HashSet<const GDScriptParser::ClassNode *> required_classes;
	HashSet<const GDScriptParser::ClassNode *> inherited_classes;
	HashSet<String> class_call_headers;
	HashMap<String, int> preload_indices;
	Vector<WGodotCppProject::Preload> preloads;
	HashMap<const GDScriptParser::ConstantNode *, const WGodotCppProject::Class *> container_constant_owners;
	HashMap<const GDScriptParser::ClassNode *, Vector<const GDScriptParser::ConstantNode *>> class_container_constants;
	HashSet<const GDScriptParser::ConstantNode *> used_container_constants;
	bool initializing_container_constant = false;
	HashSet<const GDScriptParser::LambdaNode *> class_lambdas;
	String class_lambda_declarations;
	String class_lambda_definitions;
	struct FunctionDefinition {
		String signature;
		String body;
		bool prepare_class = false;
	};
	// Decide static preparation after lowering every body and retained resource.
	Vector<FunctionDefinition> class_function_definitions;
	bool class_uses_tasks = false;
	struct ClassLifecycle {
		bool prepare = false;
		bool fields = false;
		bool constructor = false;
		bool virtual_initializer = false;
		bool notifications = false;
		bool tasks = false;
	};
	HashMap<const GDScriptParser::ClassNode *, ClassLifecycle> class_lifecycles;
	const GDScriptParser::FunctionNode *current_function = nullptr;
	bool function_failed = false;
	HashMap<const GDScriptParser::ExpressionNode *, String> expression_overrides;
	HashMap<const GDScriptParser::Node *, String> local_overrides;
	HashMap<const GDScriptParser::Node *, String> object_views;
	const GDScriptParser::ExpressionNode *iterated_expression = nullptr;
	// Only this expression may retain an engine Dictionary at an explicit boundary.
	const GDScriptParser::ExpressionNode *engine_dictionary_source = nullptr;
	bool emitted_array_range = false;
	const GDScriptParser::CallNode *awaited_call = nullptr;
	uint64_t temporary_index = 0;

	Value lower(const GDScriptParser::ExpressionNode *p_expression);
	Value lower_call(const GDScriptParser::CallNode *p_call);
	Value lower_converted(const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::DataType &p_target, const GDScriptParser::Node *p_target_origin = nullptr, bool p_parameter = false);
	Value lower_engine_argument(const GDScriptParser::ExpressionNode *p_expression, Variant::Type p_target);
	Value lower_receiver(const GDScriptParser::ExpressionNode *p_expression);
	Value lower_binary(const GDScriptParser::BinaryOpNode *p_binary);
	Value lower_index(const GDScriptParser::SubscriptNode *p_subscript);
	Value lower_dictionary(const GDScriptParser::DictionaryNode *p_dictionary);
	Value value_facts(const GDScriptParser::ExpressionNode *p_expression, const String &p_code);
	Value lower_literal(const Variant &p_value, const GDScriptParser::Node *p_origin);
	bool receiver_needs_cast(const Value &p_value, const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_origin);
	String receiver_pointer(const Value &p_value, const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_origin);
	String materialize_receiver(Value &r_call, const Value &p_receiver, const String &p_pointer, bool p_as_argument = false);
	Value sequence(Vector<Value> &r_operands);
	void materialize(Value &r_value, Vector<String> &r_setup);
	String convert_value(const Value &p_value, const String &p_target) const;
	String leaf_expression(const GDScriptParser::ExpressionNode *p_expression);

	void unsupported(const GDScriptParser::Node *p_node, const String &p_feature);
	String source_header(const String &p_path, const String &p_class) const;
	String class_name(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_origin);
	StringName native_base(const GDScriptParser::DataType &p_type) const;
	String type(const GDScriptParser::DataType &p_type, const GDScriptParser::Node *p_origin);
	String signature_type(const GDScriptParser::Node *p_origin, bool p_signal = false);
	String function_result(const GDScriptParser::FunctionNode *p_function);
	bool native_only(const GDScriptParser::DataType &p_type) const;
	Value callback_call(const GDScriptParser::CallNode *p_call);
	bool validate_callback(const GDScriptParser::ExpressionNode *p_source, const WGodotCppSignatures::Signature &p_target, bool p_discard_result = false);
	bool is_warray(const GDScriptParser::DataType &p_type) const;
	bool is_wdictionary(const GDScriptParser::DataType &p_type) const;
	bool is_dictionary_duplicate(const GDScriptParser::ExpressionNode *p_value) const;
	Value dictionary_literal(const GDScriptParser::DictionaryNode *p_dictionary, const GDScriptParser::DataType &p_target);
	Value wdictionary_call(const GDScriptParser::CallNode *p_call, bool p_to_dictionary = false);
	bool try_lower_dictionary_get(const GDScriptParser::ExpressionNode *p_expression, Value &r_result);
	String dictionary_engine_argument(const GDScriptParser::ExpressionNode *p_value, Variant::Type p_target);
	bool validate_dictionary_conversion(const GDScriptParser::ExpressionNode *p_value, const GDScriptParser::DataType &p_target, const GDScriptParser::Node *p_target_origin);
	bool is_packed(const GDScriptParser::DataType &p_type) const;
	Value packed_array(const GDScriptParser::ExpressionNode *p_source, const GDScriptParser::DataType &p_target);
	GDScriptParser::DataType expression_type(const GDScriptParser::ExpressionNode *p_expression) const;
	GDScriptParser::DataType variable_type(const GDScriptParser::VariableNode *p_variable) const;
	bool has_native_value_signature(const GDScriptParser::FunctionNode *p_function) const;
	bool validate_array_conversion(const GDScriptParser::ExpressionNode *p_value, const GDScriptParser::DataType &p_target, const GDScriptParser::Node *p_target_origin = nullptr);
	Value array_literal(const GDScriptParser::ArrayNode *p_array, const GDScriptParser::DataType &p_target);
	Value warray_call(const GDScriptParser::CallNode *p_call, bool p_to_array = false);
	String engine_argument(const GDScriptParser::ExpressionNode *p_value, Variant::Type p_target);
	bool is_array_duplicate(const GDScriptParser::ExpressionNode *p_value) const;
	String literal(const Variant &p_value, const GDScriptParser::Node *p_origin);
	int resource_index(const Resource *p_resource);
	void collect_resource_indices(const Variant &p_value, HashSet<int> &r_indices);
	void emit_preloads();
	void collect_container_constants();
	const GDScriptParser::ConstantNode *container_constant_source(const GDScriptParser::ExpressionNode *p_expression) const;
	GDScriptParser::DataType container_constant_type(const GDScriptParser::ConstantNode *p_constant) const;
	bool can_inline_constant(const GDScriptParser::ExpressionNode *p_expression) const;
	Value container_constant(const GDScriptParser::ConstantNode *p_constant);
	void emit_container_constants(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions);
	String converted(const GDScriptParser::ExpressionNode *p_expression, const GDScriptParser::DataType &p_target, const GDScriptParser::Node *p_target_origin = nullptr);
	// For a C++ boolean context. Use bool(...) when storing the result instead.
	String truth(const GDScriptParser::ExpressionNode *p_expression);
	const WGodotCppProject::Class *member_owner(const GDScriptParser::ClassNode *p_class, const StringName &p_name) const;
	Value member(const GDScriptParser::ExpressionNode *p_base, const StringName &p_name, const GDScriptParser::ExpressionNode *p_origin);
	Value call(const GDScriptParser::CallNode *p_call);
	const GDScriptParser::FunctionNode *interface_method(const GDScriptParser::CallNode *p_call) const;
	Value builtin_call(const GDScriptParser::CallNode *p_call);
	Value global_call(const GDScriptParser::CallNode *p_call);
	String variant_type(Variant::Type p_type) const;
	String operation(Variant::Operator p_operation, const GDScriptParser::DataType &p_result, const GDScriptParser::DataType &p_left_type, const GDScriptParser::DataType &p_right_type, const String &p_left, const String &p_right, const GDScriptParser::Node *p_origin);
	String cast(const GDScriptParser::CastNode *p_cast);
	String type_test(const GDScriptParser::TypeTestNode *p_test);
	void initialize_native_methods();
	String native_access(const MethodBind *p_method, const GDScriptParser::Node *p_origin, bool p_qualified = false);
	void emit_native_access();
	bool validate_native_arguments(const MethodBind *p_method, const GDScriptParser::Node *p_origin);
	bool validate_builtin_arguments(Variant::Type p_type, const StringName &p_method, const GDScriptParser::Node *p_origin);
	struct NativeCall {
		String owner;
		String method;
		Vector<String> argument_types;
		bool is_static = false;
		bool adapted = false;
		bool snapshot_result = false;
	};
	void configure_native_call(const MethodBind *p_method, const GDScriptParser::Node *p_origin, Vector<Value> &r_arguments, NativeCall &r_call);
	Value native_call(const GDScriptParser::CallNode *p_call, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::DataType &p_base_type);
	Value javascript_call(const GDScriptParser::CallNode *p_call, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::DataType &p_base_type);
	Value group_call(const GDScriptParser::CallNode *p_call, const GDScriptParser::ExpressionNode *p_base, const GDScriptParser::DataType &p_base_type);
	Value tween_property_call(const GDScriptParser::CallNode *p_call, const GDScriptParser::ExpressionNode *p_base);
	String builtin_member(Variant::Type p_type, const StringName &p_name, const GDScriptParser::Node *p_origin, const String &p_receiver, const String &p_value = String());
	String native_argument_type(const PropertyInfo &p_info, const GDScriptParser::Node *p_origin);
	Value native_result_value(const String &p_code, const PropertyInfo &p_info, int p_metadata, const GDScriptParser::Node *p_origin);
	Value native_invoke(const MethodBind *p_method, Value p_receiver, Vector<Value> p_arguments, const String &p_result, const GDScriptParser::Node *p_origin);
	Value native_property(const GDScriptParser::ExpressionNode *p_base, const StringName &p_name, const GDScriptParser::ExpressionNode *p_origin, const GDScriptParser::ExpressionNode *p_value = nullptr);
	Value property_access(const GDScriptParser::DataType &p_base_type, const StringName &p_name, const GDScriptParser::ExpressionNode *p_origin, Value p_receiver, Value p_value = Value());
	Value store_identifier(const GDScriptParser::IdentifierNode *p_target, const Value &p_value);
	StringName accessor_name(const GDScriptParser::VariableNode *p_variable, bool p_setter) const;
	const GDScriptParser::Node *local_source(const GDScriptParser::IdentifierNode *p_identifier) const;
	Value assignment(const GDScriptParser::AssignmentNode *p_assignment);
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
	void emit_class_lifecycle(const WGodotCppProject::Class &p_class, const String &p_initialization, String p_static_fields, String p_static_initialization, String &r_declaration, String &r_definitions);
	void emit_interface(const WGodotCppProject::Class &p_class);
	bool is_interface_type(const GDScriptParser::DataType &p_type) const;
	String native_interface_name(const StringName &p_name) const;
	void emit_native_interface(const WGodotNativeInterfaces::Descriptor &p_interface);
	String native_interface_signal_type(const MethodInfo &p_signal);
	String interface_value_definition(const String &p_name, const String &p_base, const String &p_interface, const String &p_members = String()) const;
	String interface_value_traits(const String &p_name) const;
	Value native_interface_call(const GDScriptParser::CallNode *p_call, const GDScriptParser::ExpressionNode *p_base, const WGodotNativeInterfaces::Descriptor &p_interface);
	Value native_interface_member(const GDScriptParser::ExpressionNode *p_base, const StringName &p_name, const GDScriptParser::ExpressionNode *p_origin, const WGodotNativeInterfaces::Descriptor &p_interface);
	String interface_cpp_type(const GDScriptParser::ClassNode *p_interface);
	void collect_interface_property_accessors();
	void emit_interface_inheritance(const WGodotCppProject::Class &p_class, String &r_bases, String &r_declaration, String &r_definitions);
	String register_interfaces();
	void emit_virtuals(const WGodotCppProject::Class &p_class, String &r_declaration, String &r_definitions);
	void register_class(const WGodotCppProject::Class &p_class, HashSet<String> &r_registered, String &r_code);

public:
	explicit WGodotCppEmitter(const WGodotCppProject &p_project);
	Error generate();
	Error write(const String &p_directory) const;
	const Vector<String> &get_diagnostics() const { return diagnostics; }
};
