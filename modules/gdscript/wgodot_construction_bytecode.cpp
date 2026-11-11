// wgodot-changes::file
#include "gdscript_byte_codegen.h"

bool GDScriptByteCodeGenerator::ConstantComparator::compare(const Variant &p_left, const Variant &p_right) {
	if (p_left.get_type() != p_right.get_type()) {
		return false;
	}
	// Value equality ignores container declarations, including for empty values.
	// A pooled typed default must retain its own element/key/value types.
	if (p_left.get_type() == Variant::ARRAY && !Array(p_left).is_same_typed(Array(p_right))) {
		return false;
	}
	if (p_left.get_type() == Variant::DICTIONARY && !Dictionary(p_left).is_same_typed(Dictionary(p_right))) {
		return false;
	}
	return p_left.hash_compare(p_right);
}

void GDScriptByteCodeGenerator::write_construct_script_fields(const Address &p_target, const Address &p_script, const Vector<Address> &p_arguments, const Vector<int> &p_fields, const Vector<Address> &p_values) {
	append_opcode_and_argcount(GDScriptFunction::OPCODE_CONSTRUCT_SCRIPT_FIELDS, p_arguments.size() + p_values.size() + 2);
	for (const Address &argument : p_arguments) {
		append(argument);
	}
	for (const Address &value : p_values) {
		append(value);
	}
	append(p_script);
	CallTarget target = get_call_target(p_target);
	append(target.target);
	append(p_arguments.size());
	append(p_fields.size());
	for (int index : p_fields) {
		append(index);
	}
	target.cleanup();
}

int GDScriptByteCodeGenerator::write_skip_initialized_member(int p_member) {
	append_opcode(GDScriptFunction::OPCODE_SKIP_INITIALIZED_MEMBER);
	append(p_member);
	const int jump = opcodes.size();
	append(0);
	return jump;
}

void GDScriptByteCodeGenerator::write_end_member_initialization(int p_jump) {
	patch_jump(p_jump);
}

void GDScriptByteCodeGenerator::write_construct_packed_literal(const Address &p_target, Variant::Type p_type, const Vector<Address> &p_values) {
	append_opcode_and_argcount(GDScriptFunction::OPCODE_CONSTRUCT_PACKED_LITERAL, p_values.size() + 1);
	for (const Address &value : p_values) {
		append(value);
	}
	CallTarget target = get_call_target(p_target);
	append(target.target);
	append(p_values.size());
	append(p_type);
	target.cleanup();
}
