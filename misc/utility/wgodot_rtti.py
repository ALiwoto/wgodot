# wgodot-changes::file

from methods import using_clang


def _set_rtti(env, enabled):
    rtti_flags = {"/GR", "/GR-", "-frtti", "-fno-rtti", "/clang:-frtti", "/clang:-fno-rtti"}
    for variable in ("CCFLAGS", "CXXFLAGS"):
        env[variable] = [flag for flag in env.get(variable, []) if flag not in rtti_flags]
    # clang-cl's /GR- only disables RTTI data; -fno-rtti disables the language feature too.
    if env.msvc and not using_clang(env):
        flag = "/GR" if enabled else "/GR-"
    else:
        flag = "-frtti" if enabled else "-fno-rtti"
        if env.msvc:
            flag = "/clang:" + flag
    env.AppendUnique(CXXFLAGS=[flag])


def configure_runtime(env):
    if not env.editor_build:
        _set_rtti(env, False)
        env.AppendUnique(CPPDEFINES=["WGODOT_NO_RTTI"])


def enable_for_c_library(env):
    # Only the library's private clone may opt out. Godot wrappers keep the runtime flags.
    if "WGODOT_NO_RTTI" in env["CPPDEFINES"]:
        _set_rtti(env, True)
        env["CPPDEFINES"] = [define for define in env["CPPDEFINES"] if define != "WGODOT_NO_RTTI"]
