// wgodot-changes::file
#pragma once

#include "core/string/ustring.h"
#include "core/templates/vector.h"
#include "core/variant/dictionary.h"

namespace WGodotCppExporter {
int run(const Vector<String> &p_arguments);
Dictionary execute(const Dictionary &p_options);
} //namespace WGodotCppExporter
