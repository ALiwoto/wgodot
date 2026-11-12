// wgodot-changes::file
#include "gdscript_byte_codegen.h"

#include "core/variant/variant_internal.h"

#include <array>
#include <cstring>

namespace {

struct ConstantMember {
	Variant::Type type;
	Variant::ValidatedGetter get;
};

using ConstantMembers = std::array<Vector<ConstantMember>, Variant::VARIANT_MAX>;

const ConstantMembers &constant_members() {
	// Local-static initialization is synchronized across parser workers. Only
	// immutable getter metadata is shared, after Variant registration finishes.
	static const ConstantMembers members = [] {
		ConstantMembers result;
		for (int type = 0; type < Variant::VARIANT_MAX; ++type) {
			List<StringName> names;
			Variant::get_member_list(Variant::Type(type), &names);
			for (const StringName &name : names) {
				result[type].push_back({
						Variant::get_member_type(Variant::Type(type), name),
						Variant::get_member_validated_getter(Variant::Type(type), name),
				});
			}
		}
		return result;
	}();
	return members;
}

template <typename T>
bool same_packed_bits(const Vector<T> &p_left, const Vector<T> &p_right) {
	return p_left.size() == p_right.size() && (p_left.is_empty() ||
			std::memcmp(p_left.ptr(), p_right.ptr(), p_left.size() * sizeof(T)) == 0);
}

bool same_constant(const Variant &p_left, const Variant &p_right, int p_depth = 0) {
	if (p_left.get_type() != p_right.get_type() || p_depth > Variant::MAX_RECURSION_DEPTH) {
		return false;
	}
	if (p_left.wgodot_is_value_container() != p_right.wgodot_is_value_container()) {
		return false;
	}
	const Variant::Type type = p_left.get_type();
	if (type == Variant::WRECORD) {
		const WRecord left = p_left;
		const WRecord right = p_right;
		if (left.id() == right.id()) {
			return true;
		}
		if (left.is_empty() || right.is_empty() || left.schema() != right.schema()) {
			return false;
		}
		for (int index = 0; index < left.schema()->field_count(); ++index) {
			if (!same_constant(left.get(index), right.get(index), p_depth + 1)) {
				return false;
			}
		}
		return true;
	}
	if (type == Variant::FLOAT) {
		const double left = p_left;
		const double right = p_right;
		return std::memcmp(&left, &right, sizeof(double)) == 0;
	}
	if (type == Variant::ARRAY) {
		const Array &left = *VariantInternal::get_array(&p_left);
		const Array &right = *VariantInternal::get_array(&p_right);
		if (left.id() == right.id()) {
			return true;
		}
		if (!left.is_same_typed(right) || left.size() != right.size()) {
			return false;
		}
		for (int index = 0; index < left.size(); ++index) {
			if (!same_constant(left[index], right[index], p_depth + 1)) {
				return false;
			}
		}
		return true;
	}
	if (type == Variant::DICTIONARY) {
		const Dictionary &left = *VariantInternal::get_dictionary(&p_left);
		const Dictionary &right = *VariantInternal::get_dictionary(&p_right);
		if (left.id() == right.id()) {
			return true;
		}
		if (!left.is_same_typed(right) || left.size() != right.size()) {
			return false;
		}
		// Dictionary equality ignores insertion order; compiled literals must not.
		for (auto a = left.begin(), b = right.begin(); a; ++a, ++b) {
			if (!same_constant(a->key, b->key, p_depth + 1) ||
					!same_constant(a->value, b->value, p_depth + 1)) {
				return false;
			}
		}
		return true;
	}
	if (type == Variant::PACKED_FLOAT32_ARRAY) {
		return same_packed_bits(*VariantInternal::get_float32_array(&p_left), *VariantInternal::get_float32_array(&p_right));
	}
	if (type == Variant::PACKED_FLOAT64_ARRAY) {
		return same_packed_bits(*VariantInternal::get_float64_array(&p_left), *VariantInternal::get_float64_array(&p_right));
	}
	if (Variant::is_packed_array_type(type)) {
		const uint64_t count = p_left.get_indexed_size();
		if (count != p_right.get_indexed_size()) {
			return false;
		}
		bool valid = false;
		bool out_of_bounds = false;
		for (uint64_t index = 0; index < count; ++index) {
			if (!same_constant(p_left.get_indexed(index, valid, out_of_bounds), p_right.get_indexed(index, valid, out_of_bounds), p_depth + 1)) {
				return false;
			}
		}
		return true;
	}
	const Vector<ConstantMember> &members = constant_members()[type];
	if (!members.is_empty()) {
		for (const ConstantMember &member : members) {
			Variant left;
			Variant right;
			VariantInternal::initialize(&left, member.type);
			VariantInternal::initialize(&right, member.type);
			member.get(&p_left, &left);
			member.get(&p_right, &right);
			if (!same_constant(left, right, p_depth + 1)) {
				return false;
			}
		}
		return true;
	}
	return p_left.hash_compare(p_right);
}

} // namespace

bool GDScriptByteCodeGenerator::ConstantComparator::compare(const Variant &p_left, const Variant &p_right) {
	// Arithmetic equality merges signed zero and container declarations. Pool
	// only constants with the same observable values, including nested members.
	return same_constant(p_left, p_right);
}
