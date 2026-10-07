# wgodot-changes::file
from pathlib import Path


def is_enabled():
    return False


def can_build(env, platform):
    return platform in ("windows", "linuxbsd", "macos") and (env.editor_build or env["wgodot_target"] == "server")


def get_opts(platform):
    return [("postgresql_path", "PostgreSQL installation containing include/ and lib/", "")]


def configure(env):
    root = env["postgresql_path"]
    if root:
        if not (Path(root) / "include" / "libpq-fe.h").is_file():
            raise RuntimeError("postgresql_path must contain include/libpq-fe.h")
        env.Append(LIBPATH=[str(Path(root) / "lib")])
        if env["platform"] == "windows":
            # A File node keeps Godot's object/library suffix off the SDK import library.
            env.Append(LIBS=[env.File(str(Path(root) / "lib" / "libpq.lib"))])
        else:
            env.Append(LIBS=["pq"])
    elif env["platform"] == "windows":
        raise RuntimeError("The PostgreSQL module requires postgresql_path on Windows")
    else:
        env.ParseConfig("pkg-config --libs libpq")
