# wgodot-changes::file
"""Static-analysis contracts attached to native builtin registrations."""

from pathlib import Path
import re


def builtin_effects(target, source, env):
    text = Path(str(source[0])).read_text(encoding="utf-8")
    entries = re.findall(
        r"// wgodot-fresh-result\s*\n\s*bind_method\((\w+),\s*(\w+),", text
    )
    lines = [
        "// wgodot-changes::file",
        "// Generated native analysis contracts; not runtime value metadata.",
        '#include "core/variant/type_info.h"',
        "static const struct { Variant::Type type; const char *method; } wgodot_fresh_builtin_results[] = {",
    ]
    for owner, method in entries:
        lines.append(f'\t{{ GetTypeInfo<{owner}>::VARIANT_TYPE, "{method}" }},')
    lines.append("};\n")
    Path(str(target[0])).write_text("\n".join(lines), encoding="utf-8")
