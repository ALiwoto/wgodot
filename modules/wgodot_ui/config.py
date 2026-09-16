# wgodot-changes::file


def can_build(env, platform):
    env.module_add_dependencies("wgodot_ui", ["gdscript"], optional=True)
    return True


def configure(env):
    pass
