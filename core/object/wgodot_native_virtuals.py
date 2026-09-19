# wgodot-changes::file

def add_native_variant(prototype, compatibility, argcount, returns):
    if compatibility:
        return prototype

    # Preserve the native C++ types, including wrappers and nested enums, for
    # generated overrides. ClassDB's script types alone cannot describe them.
    aliases = []
    parameters = []
    arguments = []
    for i in range(1, argcount + 1):
        aliases.append(f"using _wgodot_native_##$VARNAME##_arg{i} = m_type{i};")
        parameters.append(f"m_type{i}")
        arguments.append(f"arg{i}")
    if returns:
        aliases.append("using _wgodot_native_##$VARNAME##_result = m_ret;")
        parameters.append("m_ret &")
        arguments.append("r_ret")

    declarations = aliases + [
        f"virtual bool _wgodot_native_##$VARNAME({', '.join(parameters)}) $CONST {{ return false; }}",
        "virtual bool _wgodot_native_##$VARNAME##_overridden() const { return false; }",
    ]
    native = prototype.replace(
        "\tmutable void *",
        "".join(f"\t{line}\\\n" for line in declarations) + "\tmutable void *",
        1,
    )
    native = native.replace(
        "_call($CALLARGS) $CONST {\\\n",
        "_call($CALLARGS) $CONST {\\\n"
        f"\t\tif (!_get_extension() && _wgodot_native_##$VARNAME({', '.join(arguments)})) {{ return true; }}\\\n",
        1,
    )
    native = native.replace(
        "_overridden() const {\\\n",
        "_overridden() const {\\\n"
        "\t\tif (!_get_extension() && _wgodot_native_##$VARNAME##_overridden()) { return true; }\\\n",
        1,
    )
    return "#ifdef WGODOT_NATIVE_GAME\n" + native + "#else\n" + prototype + "#endif\n\n"
