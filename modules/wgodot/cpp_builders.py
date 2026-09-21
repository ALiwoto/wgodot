# wgodot-changes::file
import json
import pathlib
import re


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
    comments = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*.*?\*/', re.DOTALL)
    for implementation in source:
        text = pathlib.Path(str(implementation)).read_text(encoding="utf-8")
        text = comments.sub(lambda match: match.group(1) or " ", text)
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
