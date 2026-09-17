// wgodot-changes::file
#include "wgodot_cpp_emitter.h"
#include "wgodot_cpp_names.h"

using Parser = GDScriptParser;
using namespace WGodotCppNames;

void WGodotCppEmitter::emit_class_lifecycle(const WGodotCppProject::Class &p_class, const String &p_initialization, String p_static_fields, String p_static_initialization, String &r_declaration, String &r_definitions) {
	const auto *node = p_class.node;
	const String &name = p_class.cpp_name;
	const bool is_static = node->wgodot_static_class;
	const bool game_parent = !is_static && node->base_type.kind == Parser::DataType::CLASS;
	const String parent = game_parent ? class_name(node->base_type, node) : String();
	const ClassLifecycle inherited = game_parent ? class_lifecycles[node->base_type.class_type] : ClassLifecycle();
	const auto *owner = is_static ? nullptr : member_owner(node, SNAME("_init"));
	const auto *initializer = owner ? owner->node->get_member(SNAME("_init")).function : nullptr;
	Vector<String> parameters;
	Vector<String> factory_parameters;
	Vector<String> arguments;
	bool default_constructible = true;
	if (initializer) {
		for (const auto *parameter : initializer->parameters) {
			String parameter_text = type(parameter->type_constraint, parameter) + " v_" + symbol(parameter->identifier->name);
			parameters.push_back(parameter_text);
			arguments.push_back("v_" + symbol(parameter->identifier->name));
			if (parameter->initializer) {
				parameter_text += " = " + converted(parameter->initializer, parameter->type_constraint, parameter);
			} else {
				default_constructible = false;
			}
			factory_parameters.push_back(parameter_text);
		}
	}
	// Constructor defaults can also retain resources. Finish lowering them before
	// deciding whether this class needs its own static storage and preparation.
	if (!class_resource_types.is_empty()) {
		Vector<String> paths;
		for (const auto &resource : class_resource_types) {
			paths.push_back(resource.key);
		}
		paths.sort();
		String resources;
		for (const String &path : paths) {
			const String field = "resource_" + path.sha256_text().substr(0, 16);
			p_static_fields += "\t\t" + class_resource_types[path] + " " + field + ";\n";
			resources += "\tfields." + field + " = " + resource_load(path) + ";\n";
			resources += "\tERR_FAIL_COND_MSG(fields." + field + ".is_null(), \"Could not preload native game resource: \" + String::utf8(" + quoted(path) + "));\n";
		}
		p_static_initialization = resources + p_static_initialization;
	}
	const bool has_static_initializer = node->has_function(SNAME("_static_init"));
	const bool prepare = !p_static_fields.is_empty() || has_static_initializer;
	const bool initialize_fields = !is_static && (!p_initialization.is_empty() || prepare);
	ClassLifecycle lifecycle = inherited;
	lifecycle.prepare |= prepare;
	lifecycle.fields |= initialize_fields;
	lifecycle.constructor |= initializer != nullptr;
	lifecycle.notifications |= !is_static && node->has_function(SNAME("_notification"));
	lifecycle.tasks |= class_uses_tasks;
	class_lifecycles.insert(node, lifecycle);

	if (prepare) {
		class_call_headers.insert("modules/wgodot/native/wgodot_native_static.h");
		r_declaration += "\tstruct StaticFields {\n" + p_static_fields + "\t};\n\tstatic StaticFields &static_fields();\nprivate:\n\tstatic void initialize_static_fields(StaticFields &fields);\npublic:\n\tstatic void prepare_game_class();\n";
		r_definitions += name + "::StaticFields &" + name + "::static_fields() {\n\treturn WGodotNative::StaticStorage<StaticFields>::get(&initialize_static_fields);\n}\n\n";
		if (has_static_initializer) {
			p_static_initialization += "\tm_" + symbol(SNAME("_static_init")) + "();\n";
		}
		const String prepare_parent = inherited.prepare ? "\t" + parent + "::prepare_game_class();\n" : String();
		r_definitions += "void " + name + "::initialize_static_fields(StaticFields &fields) {\n" + prepare_parent + p_static_initialization + "}\n\n";
		r_definitions += "void " + name + "::prepare_game_class() {\n\t(void)static_fields();\n}\n\n";
	}
	if (is_static) {
		return;
	}

	String protected_members;
	if (initialize_fields) {
		protected_members += "\tvirtual void initialize_fields()" + String(inherited.fields ? " override" : "") + ";\n";
		String initialization = lifecycle.prepare ? "\tprepare_game_class();\n" : String();
		if (inherited.fields) {
			initialization += "\t" + parent + "::initialize_fields();\n";
		}
		r_definitions += "void " + name + "::initialize_fields() {\n" + initialization + p_initialization + "}\n\n";
	}
	if (initializer && owner->node == node) {
		protected_members += "\tvirtual void initialize_default()" + String(inherited.constructor ? " override" : "") + ";\n";
		r_definitions += "void " + name + "::initialize_default() {\n";
		r_definitions += default_constructible ? "\tm_" + symbol(SNAME("_init")) + "();\n" : "\tERR_FAIL_MSG(\"This native class requires constructor arguments.\");\n";
		r_definitions += "}\n\n";
	}
	if (lifecycle.constructor && !inherited.constructor) {
		protected_members += "\tWGodotNative::Construction construction_mode;\n";
	}
	if (lifecycle.notifications && !inherited.notifications) {
		protected_members += "\tbool game_initialized = false;\n";
	}
	if (lifecycle.tasks && !inherited.tasks) {
		class_native_headers.insert("modules/wgodot/native/wgodot_native_task.h");
		protected_members += "\tWGodotNative::TaskOwner game_tasks;\n";
	}
	if (!protected_members.is_empty()) {
		r_declaration += "protected:\n" + protected_members + "public:\n";
	}
	if (lifecycle.constructor) {
		r_declaration += "\texplicit " + name + "(WGodotNative::Construction p_mode = WGodotNative::Construction::SCENE);\n";
		r_definitions += name + "::" + name + "(WGodotNative::Construction p_mode) : " + (inherited.constructor ? parent + "(p_mode)" : "construction_mode(p_mode)") + " {}\n";
	} else {
		r_declaration += "\t" + name + "();\n";
		r_definitions += name + "::" + name + "() = default;\n";
	}
	// Keep construction/destruction out of line: field types may only have forward
	// declarations in this header, notably Ref<T> inside native containers.
	r_declaration += "\t~" + name + "() override;\n";
	r_definitions += name + "::~" + name + "() = default;\n\n";
	const String instance_type = type(node->self_type, node);
	r_declaration += "\tstatic " + instance_type + " create(" + String(", ").join(factory_parameters) + ");\n";
	r_definitions += instance_type + " " + name + "::create(" + String(", ").join(parameters) + ") {\n";
	if (lifecycle.prepare) {
		r_definitions += "\tprepare_game_class();\n";
	}
	r_definitions += "\tauto instance = memnew(" + name + "(" + String(lifecycle.constructor ? "WGodotNative::Construction::EXPLICIT" : "") + "));\n";
	if (initializer) {
		r_definitions += "\tinstance->m_" + symbol(SNAME("_init")) + "(" + String(", ").join(arguments) + ");\n";
	}
	r_definitions += "\treturn instance;\n}\n\n";
}
