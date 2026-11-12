// wgodot-changes::file
#include "variant.h"
#include "variant_internal.h"

void Variant::wgodot_make_value_container() {
	if (type == ARRAY) {
		VariantInternal::get_array(this)->wgodot_make_value();
	} else if (type == DICTIONARY) {
		VariantInternal::get_dictionary(this)->wgodot_make_value();
	} else if (is_packed_array_type(type)) {
		PackedArrayRefBase *previous = _data.packed_array;
		if (previous->value_semantics) {
			return;
		}
		if (previous->refcount.get() > 1) {
			_data.packed_array = previous->value_copy();
			PackedArrayRefBase::destroy(previous);
		} else {
			previous->value_semantics = true;
		}
	} else {
		ERR_FAIL_MSG("Value-container storage requires an array or dictionary.");
	}
}

bool Variant::wgodot_is_value_container() const {
	if (type == ARRAY) {
		return VariantInternal::get_array(this)->wgodot_is_value();
	}
	if (type == DICTIONARY) {
		return VariantInternal::get_dictionary(this)->wgodot_is_value();
	}
	return is_packed_array_type(type) && _data.packed_array->value_semantics;
}
