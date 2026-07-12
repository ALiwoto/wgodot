# wgodot-changes::file


def can_build(env, platform):
    env.module_add_dependencies("wgodot", ["gdscript"])
    return env.editor_build


def configure(env):
    pass
