"""Build helpers for embedded wgodot dead-code templates."""

import json
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3]))
import methods


def _read_nonempty_templates(source, prefix):
    templates = []
    for filepath in sorted(str(path) for path in source):
        basename = os.path.basename(filepath)
        if not basename.startswith(prefix):
            continue
        with open(filepath, "r", encoding="utf-8") as file:
            text = file.read().strip("\ufeff")
        text = text.replace("_wgdc_", "_wgodot_dc_WGODOT_DC_ID_")
        if text.strip():
            templates.append((basename, text))
    return templates


def _make_template_array(name, templates):
    template_count = len(templates)
    entries = []
    for template_name, text in templates:
        entries.append(f"\t{{ {json.dumps(template_name)}, {json.dumps(text)} }},")
    if not entries:
        entries.append("\t{ nullptr, nullptr },")
    entries_text = "\n".join(entries)
    return f"""\
inline constexpr int {name}_DEAD_CODE_TEMPLATE_COUNT = {template_count};
inline constexpr int {name}_DEAD_CODE_TEMPLATE_STORAGE_COUNT = {name}_DEAD_CODE_TEMPLATE_COUNT > 0 ? {name}_DEAD_CODE_TEMPLATE_COUNT : 1;
static const DeadCodeTemplate {name}_DEAD_CODE_TEMPLATES[{name}_DEAD_CODE_TEMPLATE_STORAGE_COUNT] = {{
{entries_text}
}};
"""


def make_deadcode_header(target, source, env):
    in_class_templates = _read_nonempty_templates(source, "deadcode")
    static_in_class_templates = _read_nonempty_templates(source, "static_deadcode")
    in_class_array = _make_template_array("IN_CLASS", in_class_templates)
    static_in_class_array = _make_template_array("STATIC_IN_CLASS", static_in_class_templates)

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(
            f"""\
#include "core/string/ustring.h"

namespace WGodotGDScriptDeadCodeTemplates {{

struct DeadCodeTemplate {{
\tconst char *name;
\tconst char *source;
}};

{in_class_array}

{static_in_class_array}

}} // namespace WGodotGDScriptDeadCodeTemplates
"""
        )
