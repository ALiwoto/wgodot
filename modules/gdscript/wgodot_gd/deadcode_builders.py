"""Build helpers for embedded wgodot dead-code templates."""

import json
import os
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[3]))
import methods


def _read_nonempty_templates(source):
    templates = []
    for filepath in sorted(str(path) for path in source):
        with open(filepath, "r", encoding="utf-8") as file:
            text = file.read().strip("\ufeff")
        text = text.replace("_wgdc_", "_wgodot_dc_WGODOT_DC_ID_")
        if text.strip():
            templates.append((os.path.basename(filepath), text))
    return templates


def make_deadcode_header(target, source, env):
    in_class_templates = _read_nonempty_templates(source)
    template_count = len(in_class_templates)
    entries = []
    for name, text in in_class_templates:
        entries.append(f"\t{{ {json.dumps(name)}, {json.dumps(text)} }},")
    if not entries:
        entries.append("\t{ nullptr, nullptr },")
    entries_text = "\n".join(entries)

    with methods.generated_wrapper(str(target[0])) as file:
        file.write(
            f"""\
#include "core/string/ustring.h"

namespace WGodotGDScriptDeadCodeTemplates {{

struct DeadCodeTemplate {{
\tconst char *name;
\tconst char *source;
}};

inline constexpr int IN_CLASS_DEAD_CODE_TEMPLATE_COUNT = {template_count};
inline constexpr int IN_CLASS_DEAD_CODE_TEMPLATE_STORAGE_COUNT = IN_CLASS_DEAD_CODE_TEMPLATE_COUNT > 0 ? IN_CLASS_DEAD_CODE_TEMPLATE_COUNT : 1;
static const DeadCodeTemplate IN_CLASS_DEAD_CODE_TEMPLATES[IN_CLASS_DEAD_CODE_TEMPLATE_STORAGE_COUNT] = {{
{entries_text}
}};

}} // namespace WGodotGDScriptDeadCodeTemplates
"""
        )
