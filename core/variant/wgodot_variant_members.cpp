// wgodot-changes::file
#include "variant.h"

int Variant::get_member_constructor(Type p_type) {
	List<StringName> members;
	get_member_list(p_type, &members);
	if (members.is_empty()) {
		return -1;
	}
	for (int constructor = 0; constructor < get_constructor_count(p_type); constructor++) {
		if (get_constructor_argument_count(p_type, constructor) != members.size()) {
			continue;
		}
		bool matches = true;
		for (int argument = 0; argument < members.size(); argument++) {
			const StringName name = get_constructor_argument_name(p_type, constructor, argument);
			if (!has_member(p_type, name) || get_member_type(p_type, name) != get_constructor_argument_type(p_type, constructor, argument)) {
				matches = false;
				break;
			}
		}
		if (matches) {
			return constructor;
		}
	}
	return -1;
}
