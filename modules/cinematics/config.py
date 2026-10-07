# wgodot-changes::file

def can_build(env, platform):
    return not env["disable_3d"]


def configure(env):
    pass


def get_icons_path():
    return "icons"
