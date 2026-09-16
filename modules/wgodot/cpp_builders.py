# wgodot-changes::file
import pathlib
import re


def native_methods(target, source, env):
    bindings = {}
    declaration = re.compile(
        r'\bClassDB::bind_(?:static_)?method\([^;]*?\bD_METHOD\(\s*"(\w+)"[^)]*\)\s*,\s*&([A-Za-z_]\w*)::(\w+)\s*[,)]'
    )
    comments = re.compile(r'("(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\')|//[^\n]*|/\*.*?\*/', re.DOTALL)
    for implementation in source:
        text = pathlib.Path(str(implementation)).read_text(encoding="utf-8")
        text = comments.sub(lambda match: match.group(1) or " ", text)
        for name, owner, method in declaration.findall(text):
            bindings.setdefault((owner, name), set()).add(method)
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
