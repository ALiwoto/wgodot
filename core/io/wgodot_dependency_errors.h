// wgodot-changes::file
#pragma once

#include "core/variant/callable.h"

namespace WGodotDependencyErrors {

void queue(const Callable &p_report);
// Deliver worker reports before the editor checks a completed load for errors.
void flush();

} // namespace WGodotDependencyErrors
