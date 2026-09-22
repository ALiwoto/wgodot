# wgodot-changes::file


def _set_rtti(env, enabled):
    rtti_flags = {"/GR", "/GR-", "-frtti", "-fno-rtti"}
    for variable in ("CCFLAGS", "CXXFLAGS"):
        env[variable] = [flag for flag in env.get(variable, []) if flag not in rtti_flags]
    flag = ("/GR" if enabled else "/GR-") if env.msvc else ("-frtti" if enabled else "-fno-rtti")
    env.AppendUnique(CXXFLAGS=[flag])


def configure_release(env):
    if env["target"] == "template_release":
        _set_rtti(env, False)
        env.AppendUnique(CPPDEFINES=["WGODOT_NO_RTTI"])


def enable_for_c_library(env):
    # Only the library's private clone may opt out. Godot wrappers keep the release flags.
    if "WGODOT_NO_RTTI" in env["CPPDEFINES"]:
        _set_rtti(env, True)
        env["CPPDEFINES"] = [define for define in env["CPPDEFINES"] if define != "WGODOT_NO_RTTI"]
