# wgodot-changes::file
import json
import pathlib
import re


def packed_api(target, source, env):
    bindings = pathlib.Path(str(source[0])).read_text(encoding="utf-8")
    elements = dict(re.findall(r"\bVARCALL_ARRAY_GETTER_SETTER\((Packed\w+Array), (\w+)\)", bindings))
    member_aliases = dict(re.findall(r"static constexpr auto (\w+) = ([^;]+);", bindings))
    functions = {
        name: (result, arguments)
        for result, name, arguments in re.findall(r"\bstatic ([\w:<>]+) (func_Packed\w+)\(([^\n]*)\) \{", bindings)
    }
    # Expand the upstream accessors into inline typed functions. Bracket
    # indexing and these methods deliberately have different bounds semantics.
    accessor_definition = re.search(
        r"#define VARCALL_ARRAY_GETTER_SETTER\(m_packed_type, m_type\) (.*?)\n\n", bindings, re.DOTALL
    )
    accessor_macro = accessor_definition.group(1).replace("\\\n", "\n")
    accessors = []
    accessor_names = set()
    for owner, element in elements.items():
        expanded = accessor_macro.replace("m_packed_type", owner).replace("m_type", element).replace("##", "")
        accessor_names.update(re.findall(r"\b(func_\w+)\(", expanded))
        expanded = expanded.replace("static ", "inline ").replace("func_", "packed_")
        expanded = expanded.replace(f"{owner} *p_instance", f"{owner} &p_instance").replace(
            "p_instance->", "p_instance."
        )
        accessors.append("\n".join(line.removeprefix("\t").rstrip() for line in expanded.strip("\n").splitlines()))

    api = [
        "// wgodot-changes::file",
        "// Generated from Godot's packed-array bindings.",
        "static const struct { Variant::Type kind; const char *element; } packed_types[] = {",
    ]
    for owner, element in sorted(elements.items()):
        kind = re.sub(r"([a-z0-9])([A-Z])", r"\1_\2", owner).upper()
        api.append(f'\t{{ Variant::{kind}, "{element}" }},')
    api.extend([
        "};",
        "static const struct { const char *owner; const char *name; const char *member; const char *function; } packed_methods[] = {",
    ])
    declarations = [
        "// wgodot-changes::file",
        "// Generated typed entry points; no Variant method dispatch.",
        "#pragma once",
        '#include "core/variant/variant.h"',
        "namespace WGodotNative {",
    ]
    declarations.extend(accessors)
    definitions = [
        "// wgodot-changes::file",
        "// Included after _VariantCall, retaining the upstream implementations.",
        '#include "modules/wgodot/native/wgodot_native_packed_api.gen.h"',
        "namespace WGodotNative {",
    ]
    for binding, owner, name, tail in re.findall(
        r"\bbind_(methodv|method|functionnc|function)\((Packed\w+Array), (\w+), ([^\n]+)", bindings
    ):
        member = ""
        function = ""
        if binding == "method":
            member = f"&{owner}::{name}"
        elif binding == "methodv":
            pointer = tail.split(",", 1)[0]
            if pointer.startswith(f"&{owner}::"):
                member = pointer
            elif alias := re.fullmatch(r"(\w+)<(\w+)>", pointer):
                member = re.sub(r"\bT\b", alias.group(2), member_aliases[alias.group(1)])
            else:
                raise ValueError(f"Unsupported packed member binding: {pointer}")
        else:
            pointer = tail.split(",", 1)[0]
            if not pointer.startswith("_VariantCall::"):
                raise ValueError(f"Unsupported packed function binding: {pointer}")
            native_name = pointer.removeprefix("_VariantCall::")
            if native_name in accessor_names:
                function = native_name.replace("func_", "packed_", 1)
            else:
                result, arguments = functions[native_name]
                function = f"packed_{owner}_{name}"
                parameters = arguments.replace(f"{owner} *p_instance", f"{owner} &p_instance")
                forwarded = re.findall(r"\bp_\w+\b", arguments)
                forwarded[0] = "&" + forwarded[0]
                declarations.append(f"{result} {function}({parameters});")
                definitions.extend([
                    f"{result} {function}({parameters}) {{",
                    f"\treturn {pointer}({', '.join(forwarded)});",
                    "}",
                ])
        api.append(f'\t{{ "{owner}", "{name}", "{member}", "{function}" }},')
    api.append("};\n")
    declarations.append("} // namespace WGodotNative\n")
    definitions.append("} // namespace WGodotNative\n")
    for path, lines in zip(target, (api, declarations, definitions)):
        pathlib.Path(str(path)).write_text("\n".join(lines), encoding="utf-8")


def array_api(target, source, env):
    header = pathlib.Path(str(source[0])).read_text(encoding="utf-8").split("\npublic:", 1)[1]
    declaration = re.compile(
        r"^\t(?P<result>[\w:<>, &]+?)\b(?P<name>\w+)\((?P<arguments>[^()\n]*)\)"
        r"(?: const)?(?: -> (?P<trailing>[^\n{]+))?(?: \{|;| = delete;)",
        re.MULTILINE,
    )
    roles = {
        "T": "ELEMENT",
        "WArray": "ARRAY",
        "WArray<U>": "OTHER_ARRAY",
        "Predicate": "PREDICATE",
        "Compare": "COMPARATOR",
        "Mapper": "MAPPER",
        "Reducer": "REDUCER",
        "Accumulator": "ACCUMULATOR",
    }
    lines = [
        "// wgodot-changes::file",
        "// Generated from WArray's public native API.",
        "inline constexpr Method methods[] = {",
    ]
    names = set()
    for match in declaration.finditer(header):
        # An annotation belongs to the following declaration, across template lines.
        prefix = header[: match.start()].splitlines()
        annotation = ""
        while prefix and (prefix[-1].startswith("\t//") or prefix[-1].startswith("\ttemplate ")):
            line = prefix.pop()
            if line.startswith("\t// native-array: "):
                annotation = line.split("native-array: ", 1)[1]
        if annotation == "internal":
            continue
        name = match.group("name")
        arguments = [part.strip() for part in match.group("arguments").split(",") if part.strip()]
        argument_roles = []
        for argument in arguments:
            arg_type = re.sub(r"\s+p_\w+.*$", "", argument.replace("&", " ").replace("const ", "")).strip()
            argument_roles.append("Type::" + roles.get(arg_type, "BUILTIN"))
        result = roles.get(match.group("result").strip(), "BUILTIN")
        if match.group("trailing"):
            if not match.group("trailing").startswith("WArray<"):
                raise ValueError(f"Unrecognized native Array result declaration: {name}")
            result = "MAPPED_ARRAY"
        required = sum("=" not in argument for argument in arguments)
        copy = annotation.removeprefix("copy=") if annotation.startswith("copy=") else ""
        unsupported = annotation.removeprefix("unsupported=") if annotation.startswith("unsupported=") else ""
        lines.append(
            f'\t{{ "{name}", Type::{result}, {{ {", ".join(argument_roles)} }}, '
            f'{required}, {len(arguments)}, {str(annotation == "ordered").lower()}, '
            f'{json.dumps(copy)}, {json.dumps(unsupported)} }},'
        )
        names.add(name)
    bindings = pathlib.Path(str(source[1])).read_text(encoding="utf-8")
    exposed = set(re.findall(r"\bbind_(?:method|functionnc|function)\(Array, (\w+),", bindings))
    missing = exposed - names
    if missing:
        raise ValueError("Array methods without a native implementation or explicit rejection: " + ", ".join(sorted(missing)))
    lines.append("};\n")
    pathlib.Path(str(target[0])).write_text("\n".join(lines), encoding="utf-8")


def native_binding_sources(source):
    """Expand simple binding macros using their source definitions, not API names."""
    comments = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*.*?\*/', re.DOTALL)
    definition = re.compile(r'^\s*#define[ \t]+(\w+)\(([^)\n]*)\)[ \t]*([^\n]*)', re.MULTILINE)
    texts = []
    definitions = {}
    for path in source:
        text = pathlib.Path(str(path)).read_text(encoding="utf-8").replace("\\\n", "")
        text = comments.sub(lambda match: match.group(1) or " ", text)
        for name, parameters, body in definition.findall(text):
            definitions.setdefault(name, set()).add((parameters, body))
        texts.append(re.sub(r'^[ \t]*#[^\n]*', '', text, flags=re.MULTILINE))

    # Only infer unambiguous definitions. Conditional/complex macros stay
    # unsupported instead of guessing a C++ binding at export time.
    macros = {name: next(iter(values)) for name, values in definitions.items() if len(values) == 1}
    stringizers = set()
    pending = True
    while pending:
        pending = False
        for name, (parameter, body) in macros.items():
            if name in stringizers or not parameter.isidentifier():
                continue
            forwarding = re.fullmatch(r'(\w+)\(\s*' + re.escape(parameter) + r'\s*\)', body)
            if re.fullmatch(r'#\s*' + re.escape(parameter), body) or (forwarding and forwarding[1] in stringizers):
                stringizers.add(name)
                pending = True

    quoted_or_word = re.compile(r'"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|\b\w+\b')
    stringize = re.compile(r'\b(' + '|'.join(sorted(map(re.escape, stringizers))) + r')\(\s*(\w+)\s*\)') if stringizers else None
    adjacent_strings = re.compile(r'("(?:\\.|[^"\\])*")\s*("(?:\\.|[^"\\])*")')
    for name, (parameters, body) in macros.items():
        if not re.search(r'\bClassDB::bind_(?:static_)?method\(', body):
            continue
        parameters = [part.strip() for part in parameters.split(',')]
        if not all(parameter.isidentifier() for parameter in parameters):
            continue
        # Flat arguments cover enum/class/member binding declarations. Nested
        # macro expressions are deliberately left for the unsupported diagnostic.
        invocation = re.compile(r'\b' + re.escape(name) + r'\(([^()\n]*)\)')

        def expand(match):
            arguments = [part.strip() for part in match[1].split(',')]
            if len(arguments) != len(parameters) or not all(re.fullmatch(r'\w+(?:::\w+)*', argument) for argument in arguments):
                return match[0]
            values = dict(zip(parameters, arguments))
            expanded = re.sub(r'(?<!#)#\s*(\w+)(?!#)', lambda token: json.dumps(values[token[1]]) if token[1] in values else token[0], body)
            expanded = quoted_or_word.sub(lambda token: values.get(token[0], token[0]), expanded)
            expanded = re.sub(r'\s*##\s*', '', expanded)
            if stringize:
                expanded = stringize.sub(lambda token: json.dumps(token[2]), expanded)
            while adjacent_strings.search(expanded):
                expanded = adjacent_strings.sub(lambda token: json.dumps(json.loads(token[1]) + json.loads(token[2])), expanded)
            return expanded

        texts = [invocation.sub(expand, text) for text in texts]
    return texts


def native_methods(target, source, env):
    bindings = {}
    forwarded = {}
    parents = {}
    registered = set()
    binding_arguments = (
        r'\(\s*D_METHOD\(\s*"(\w+)"[^)]*\)\s*,'
        r'[^;]*?&\s*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*::\s*(\w+)\s*[,)]'
    )
    declaration = re.compile(
        r'\bClassDB::bind_(?:static_)?method\([^;]*?\bD_METHOD\(\s*"(\w+)"[^)]*\)\s*,'
        # Overload bindings can cast the member pointer before naming it.
        r'[^;]*?&\s*([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)\s*::\s*(\w+)\s*[,)]'
    )
    template_binding = re.compile(r'\b\w+<[^;{}()]+>\s*' + binding_arguments)
    inheritance = re.compile(r'\bclass\s+(\w+)\s*(?:final\s*)?:\s*([^;{}]+)\{')
    for text in native_binding_sources(source):
        for name, owner, method in declaration.findall(text):
            owner = owner.rsplit("::", 1)[-1]  # ClassDB uses the unqualified class name.
            bindings.setdefault((owner, name), set()).add(method)
        # Shared C++ templates can bind their members on a concrete GDCLASS.
        # Read the forwarding declarations and follow C++ inheritance, which
        # may include template bases absent from the ClassDB inheritance tree.
        for name, owner, method in template_binding.findall(text):
            forwarded.setdefault(owner.rsplit("::", 1)[-1], {}).setdefault(name, set()).add(method)
        registered.update(re.findall(r'\bGDCLASS\(\s*(\w+)\s*,', text))
        for owner, bases in inheritance.findall(text):
            depth = 0
            names = ""
            for char in bases:
                if char == "<":
                    depth += 1
                elif char == ">":
                    depth -= 1
                elif depth == 0:
                    names += char
            for base in names.split(","):
                base = re.sub(r'\b(public|protected|private|virtual)\b', '', base).strip().rsplit("::", 1)[-1]
                if base.isidentifier():
                    parents.setdefault(owner, set()).add(base)
    for owner in registered:
        pending = list(parents.get(owner, ()))
        visited = set()
        inherited = {}
        while pending:
            base = pending.pop()
            if base in visited or base in registered:
                continue
            visited.add(base)
            for name, methods in forwarded.get(base, {}).items():
                inherited.setdefault(name, set()).update(methods)
            pending.extend(parents.get(base, ()))
        for name, methods in inherited.items():
            bindings.setdefault((owner, name), methods)
    lines = [
        "// wgodot-changes::file",
        "// Generated native binding/C++ method map. Editor only.",
        "static const struct { const char *owner; const char *name; const char *method; } native_cpp_methods[] = {",
    ]
    for (owner, name), methods in sorted(bindings.items()):
        if len(methods) == 1:
            method = next(iter(methods))
            lines.append(f'\t{{ "{owner}", "{name}", "{method}" }},')
    lines.append("};\n")
    pathlib.Path(str(target[0])).write_text("\n".join(lines), encoding="utf-8")


def native_headers(target, source, env):
    root = pathlib.Path(env["wgodot_source_root"])
    classes = {}
    # Track lexical namespace scopes without mistaking strings/comments for C++.
    # ClassDB names omit namespaces (for example OS is CoreBind::OS in C++).
    tokens = re.compile(
        r'''(?P<ignored>"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|//[^\n]*|/\*.*?\*/)'''
        r"|\bnamespace\s+(?P<namespace>[\w:]+)\s*\{"
        r"|\bGD(?:VIRTUAL)?CLASS\(\s*(?P<class>\w+)\s*,\s*\w+\s*\)"
        r"|(?P<brace>[{}])",
        re.DOTALL,
    )
    for header in source:
        path = pathlib.Path(str(header)).resolve()
        relative = path.relative_to(root).as_posix()
        scopes = []
        for token in tokens.finditer(path.read_text(encoding="utf-8")):
            if token.group("namespace"):
                scopes.append(token.group("namespace"))
            elif token.group("brace") == "{":
                scopes.append("")
            elif token.group("brace") == "}":
                if scopes:
                    scopes.pop()
            elif token.group("class"):
                name = token.group("class")
                qualified = "::".join([scope for scope in scopes if scope] + [name])
                classes.setdefault(name, []).append((relative, qualified))
    # Object uses GDCLASS's underlying machinery directly.
    classes["Object"] = [("core/object/object.h", "Object")]
    lines = [
        "// wgodot-changes::file",
        "// Generated native type/header map. Editor only.",
        "static const struct { const char *name; const char *header; const char *cpp_type; } native_cpp_headers[] = {",
    ]
    for name, headers in sorted(classes.items()):
        if len(headers) == 1:
            header, qualified = headers[0]
            lines.append(f'\t{{ "{name}", "{header}", "{qualified}" }},')
    lines.append("};\n")
    pathlib.Path(str(target[0])).write_text("\n".join(lines), encoding="utf-8")
