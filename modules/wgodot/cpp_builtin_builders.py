# wgodot-changes::file
"""Native mappings derived from the engine's builtin registrations."""

import json
import pathlib
import re


def arguments(text):
    """Split a C++ macro/template argument list without splitting nested types."""
    result = []
    start = 0
    depth = 0
    for index, char in enumerate(text):
        if char in "(<[{":
            depth += 1
        elif char in ")>]}":
            depth -= 1
        elif char == "," and depth == 0:
            result.append(text[start:index].strip())
            start = index + 1
    result.append(text[start:].strip())
    return result


def builtin_api(target, source, env):
    bindings, constructors, utilities = [pathlib.Path(str(path)).read_text(encoding="utf-8") for path in source]
    lines = [
        "// wgodot-changes::file",
        "// Generated from Godot's builtin registrations. Used only by the exporter.",
        '#include "core/variant/type_info.h"',
        '#include "modules/wgodot/native/wgodot_native_utility_traits.h"',
        "#include <type_traits>",
        "#include <utility>",
        "static const struct { const char *owner; const char *name; const char *receiver; const char *pointer; bool is_static; } builtin_methods[] = {",
    ]
    for match in re.finditer(
        r"^\s*bind_(string_methodv|string_method|static_methodv|static_method|methodv|method)\((.*)\);$",
        bindings,
        re.MULTILINE,
    ):
        binding, text = match.groups()
        args = arguments(text)
        string_method = binding.startswith("string_")
        owner = "String" if string_method else args.pop(0)
        if owner in ("Array", "Dictionary", "Callable", "Signal") or owner.startswith("Packed"):
            continue
        name = args[0]
        pointer = args[1] if binding.endswith("v") else f"&{owner}::{name}"
        owners = ("String", "StringName") if string_method else (owner,)
        for exposed_owner in owners:
            values = ", ".join(json.dumps(value) for value in (exposed_owner, name, owner, pointer))
            lines.append(f"\t{{ {values}, {'true' if binding.startswith('static_') else 'false'} }},")
    lines.extend([
        "};",
        "static const struct { Variant::Type kind; int index; const char *parameters; bool from_string; } builtin_constructors[] = {",
    ])
    indices = {}
    for match in re.finditer(
        r"add_constructor<(VariantConstructNoArgs|VariantConstructor|VariantConstructorFromString)<(.*?)>>\(sarray\(",
        constructors,
    ):
        family, parameters = match.groups()
        types = arguments(parameters)
        owner = types[0]
        # Containers and callbacks have their own ownership-aware lowering.
        if owner in ("Array", "Dictionary", "Callable", "Signal") or owner.startswith("Packed"):
            continue
        index = indices.get(owner, 0)
        indices[owner] = index + 1
        lines.append(
            f"\t{{ GetTypeInfo<{owner}>::VARIANT_TYPE, {index}, {json.dumps(parameters)}, {'true' if family == 'VariantConstructorFromString' else 'false'} }},"
        )
    lines.extend([
        "};",
        "static const struct { const char *name; const char *function; const char *invoke; WGodotNative::NumericUtility numeric; } builtin_utilities[] = {",
    ])
    for binding, name in re.findall(
        r"^\s*(FUNCBINDR?|FUNCBINDVR[23]?|FUNCBINDVARARG)\((\w+),", utilities, re.MULTILINE
    ):
        invoke = (
            "invoke_vararg"
            if binding == "FUNCBINDVARARG"
            else "invoke_checked"
            if binding.startswith("FUNCBINDVR")
            else "invoke_static"
        )
        exposed = name.removeprefix("_")
        lines.append(
            f"\t{{ {json.dumps(exposed)}, {json.dumps(name)}, {json.dumps(invoke)}, WGodotNative::numeric_utility<&VariantUtilityFunctions::{name}> }},"
        )
    lines.extend([
        "};",
        "static const struct { const char *name; Variant::Type kind; const char *type; const char *expression; Variant::Type result; } unary_utilities[] = {",
    ])
    # Simple one-argument math branches already contain the authoritative typed
    # operation. More complex/error-producing branches keep their engine helper.
    for name in re.findall(r"^\s*FUNCBINDVR\((\w+),.*UTILITY_FUNC_TYPE_MATH", utilities, re.MULTILINE):
        function = re.search(
            rf"Variant VariantUtilityFunctions::{name}\(const Variant &p_x, Callable::CallError &r_error\) \{{(.*?)\n\}}",
            utilities,
            re.DOTALL,
        )
        if not function:
            continue
        for kind, expression in re.findall(r"case Variant::(\w+): \{\s*return ([^;]+);\s*\} break;", function[1]):
            accessor = re.fullmatch(r"(.*)VariantInternalAccessor<(\w+)>::get\(&p_x\)(.*)", expression)
            if accessor:
                prefix, cpp_type, suffix = accessor.groups()
                expression = prefix + "{value}" + suffix
                result = (
                    f"GetTypeInfo<std::decay_t<decltype({prefix}std::declval<{cpp_type}>(){suffix})>>::VARIANT_TYPE"
                )
                lines.append(
                    f"\t{{ {json.dumps(name)}, Variant::{kind}, {json.dumps(cpp_type)}, {json.dumps(expression)}, {result} }},"
                )
    lines.append("};\n")
    pathlib.Path(str(target[0])).write_text("\n".join(lines), encoding="utf-8")


def operator_api(target, source, env):
    bindings = pathlib.Path(str(source[0])).read_text(encoding="utf-8")
    registrations = re.findall(r"register_op<(.*?)>\(Variant::(\w+), Variant::(\w+), Variant::(\w+)\);", bindings)
    for family, operation in re.findall(r"^\s*register_string_op\((\w+), Variant::(\w+)\);", bindings, re.MULTILINE):
        for left, left_kind in (("String", "STRING"), ("StringName", "STRING_NAME")):
            for right, right_kind in (("String", "STRING"), ("StringName", "STRING_NAME")):
                registrations.append((f"{family}<{left}, {right}>", operation, left_kind, right_kind))
    for right, right_kind in re.findall(
        r"^\s*register_string_modulo_op\(([^,]+), Variant::(\w+)\);", bindings, re.MULTILINE
    ):
        for left, left_kind in (("String", "STRING"), ("StringName", "STRING_NAME")):
            registrations.append((
                f"OperatorEvaluatorStringFormat<{left}, {right}>",
                "OP_MODULE",
                left_kind,
                right_kind,
            ))
    binary = {
        "Add": "+",
        "Sub": "-",
        "Mul": "*",
        "Div": "/",
        "Equal": "==",
        "NotEqual": "!=",
        "Less": "<",
        "LessEqual": "<=",
        "Greater": ">",
        "GreaterEqual": ">=",
        "BitOr": "|",
        "BitAnd": "&",
        "BitXor": "^",
    }
    lines = [
        "// wgodot-changes::file",
        "// Typed expressions for the engine's registered operator signatures.",
        "static const struct { Variant::Operator operation; Variant::Type left; Variant::Type right; const char *expression; } builtin_operators[] = {",
    ]
    registered = {(op, left, right): evaluator for evaluator, op, left, right in registrations}
    for (operation, left_kind, right_kind), evaluator in registered.items():
        if any(
            kind in ("ARRAY", "DICTIONARY", "OBJECT") or kind.startswith("PACKED_") for kind in (left_kind, right_kind)
        ):
            continue
        match = re.fullmatch(r"OperatorEvaluator(\w+)(?:<(.*)>)?", evaluator)
        if not match:
            continue
        family, params = match.groups()
        types = arguments(params) if params else []
        left, right = "({left})", "({right})"
        # These upstream specializations promote the integer vector before
        # multiplying/dividing, avoiding the integer vector's scalar overload.
        if family in ("Mul", "DivNZ") and len(types) == 3 and types[0] in ("Vector2", "Vector3", "Vector4"):
            if types[1] == types[0] + "i":
                left = f"{types[0]}({left})"
            if types[2] == types[0] + "i":
                right = f"{types[0]}({right})"
        if family in binary:
            expression = f"({left} {binary[family]} {right})"
        elif family == "Neg":
            expression = f"(-{left})"
        elif family == "Pos":
            expression = left
        elif family == "BitNeg":
            expression = f"(~{left})"
        elif family in ("DivNZ", "ModNZ"):
            helper = "divide" if family == "DivNZ" else "modulo"
            expression = f"WGodotNative::{helper}({left}, {right})"
        elif family in ("ShiftLeft", "ShiftRight"):
            expression = f"WGodotNative::shift<{str(family == 'ShiftLeft').lower()}>({left}, {right})"
        elif family == "Pow":
            expression = f"{types[0]}(Math::pow(double({left}), double({right})))"
        elif family == "XForm":
            expression = f"{left}.xform({right})"
        elif family == "XFormInv":
            expression = f"{right}.xform_inv({left})"
        elif family == "InStringFind":
            expression = f"(String({right}).find(String({left})) != -1)"
        elif family == "StringConcat":
            expression = f"(String({left}) + String({right}))"
        elif family == "StringFormat":
            if right_kind in ("CALLABLE", "SIGNAL"):
                continue
            expression = f"WGodotNative::format_string_value(String({left}), {right})"
        elif family in ("AlwaysTrue", "AlwaysFalse"):
            expression = f"((void){left}, (void){right}, {str(family == 'AlwaysTrue').lower()})"
        else:
            continue
        lines.append(
            f"\t{{ Variant::{operation}, Variant::{left_kind}, Variant::{right_kind}, {json.dumps(expression)} }},"
        )
    lines.append("};\n")
    pathlib.Path(str(target[0])).write_text("\n".join(lines), encoding="utf-8")
