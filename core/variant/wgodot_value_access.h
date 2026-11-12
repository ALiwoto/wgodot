// wgodot-changes::file
#pragma once

#include "core/variant/variant.h"

namespace WGodotValueAccess {
// Only value-owned records and containers expose borrowable slots. Callers
// retain each owner after obtaining its slot, keeping reentrant writes safe.
Variant *borrow(Variant &r_owner, const Variant &p_key);
} // namespace WGodotValueAccess
