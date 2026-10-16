// wgodot-changes::file
#pragma once

#include "core/string/string_name.h"
#include "core/templates/cowdata.h"
#include "core/templates/hash_map.h"

// DataType is copied throughout analysis. Enum members are built once, then
// read through those copies; detach only when a copy actually changes a member.
class WGodotGDScriptEnumValues {
	using Values = HashMap<StringName, int64_t>;
	CowData<Values> values;

public:
	void insert(const StringName &p_name, int64_t p_value) {
		if (!values.ptr()) {
			values.resize<true>(1);
		}
		values.ptrw()->insert(p_name, p_value);
	}

	bool has(const StringName &p_name) const {
		return values.ptr() && values.ptr()->has(p_name);
	}

	int64_t operator[](const StringName &p_name) const {
		ERR_FAIL_COND_V(!values.ptr(), 0);
		return (*values.ptr())[p_name];
	}

	Values::ConstIterator begin() const {
		return values.ptr() ? values.ptr()->begin() : Values::ConstIterator();
	}

	Values::ConstIterator end() const {
		return Values::ConstIterator();
	}
};
