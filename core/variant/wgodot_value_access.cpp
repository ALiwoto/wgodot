// wgodot-changes::file
#include "wgodot_value_access.h"

#include "variant_internal.h"

Variant *WGodotValueAccess::borrow(Variant &r_owner, const Variant &p_key) {
	if (r_owner.get_type() == Variant::WRECORD) {
		WRecord *record = VariantInternal::get_wrecord(&r_owner);
		if (record->is_empty()) {
			return nullptr;
		}
		const int index = record->schema()->find_field(p_key);
		return index >= 0 ? &record->write(index) : nullptr;
	}
	if (r_owner.get_type() == Variant::ARRAY) {
		Array *array = VariantInternal::get_array(&r_owner);
		if (!array->wgodot_is_value() || array->is_read_only()) {
			return nullptr;
		}
		int64_t index = p_key;
		if (index < 0) {
			index += array->size();
		}
		return index >= 0 && index < array->size() ? &(*array)[index] : nullptr;
	}
	if (r_owner.get_type() == Variant::DICTIONARY) {
		Dictionary *dictionary = VariantInternal::get_dictionary(&r_owner);
		return dictionary->wgodot_is_value() && !dictionary->is_read_only() ? dictionary->getptr(p_key) : nullptr;
	}
	return nullptr;
}
