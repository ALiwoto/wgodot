# wgodot-changes::file
"""Compiler/linker timings, independent of SCons dependency and scheduling timings."""

from pathlib import Path

from SCons.Environment import Environment
from SCons.Node import Node

from methods import using_clang


def _trace_path(target: list[Node], env: Environment, for_signature: bool) -> str:
    output = Path(str(target[0].abspath))
    root = Path(env.Dir("#").abspath)
    try:
        relative = output.relative_to(root)
    except ValueError:
        # External native modules can live outside the engine checkout.
        relative = Path("external", output.drive.replace(":", "").replace("\\", "_"), *output.parts[1:])
    path = Path(env["llvm_diagnostics"]) / relative.with_name(relative.name + ".json")
    if not for_signature:
        path.parent.mkdir(parents=True, exist_ok=True)
    return path.as_posix()


def _compiler_trace(target: list[Node], source: list[Node], env: Environment, for_signature: bool) -> list[str]:
    prefix = "/clang:" if env.msvc else ""
    return [f"{prefix}-ftime-trace={_trace_path(target, env, for_signature)}"]


def _linker_trace(target: list[Node], source: list[Node], env: Environment, for_signature: bool) -> list[str]:
    return [f"--time-trace={_trace_path(target, env, for_signature)}"]


def configure(env: Environment) -> None:
    if not using_clang(env):
        raise ValueError("llvm_diagnostics requires Clang")
    env["llvm_diagnostics"] = str(Path(env["llvm_diagnostics"]).absolute())
    env["WGODOT_COMPILER_TRACE"] = _compiler_trace
    prefix = "/clang:" if env.msvc else ""
    # Timers change diagnostic output, not object code. SCons' signature-only
    # exclusion prevents enabling/moving reports from rebuilding every object.
    env.Append(CCFLAGS=["$(", "$WGODOT_COMPILER_TRACE", prefix + "-ftime-report", "$)"])
    if env.msvc:
        if env.subst("$LINK") != "lld-link":
            raise ValueError("LLVM linker diagnostics require lld-link with clang-cl")
        env["WGODOT_LINKER_TRACE"] = _linker_trace
        env.Append(LINKFLAGS=["$(", "/time", "$WGODOT_LINKER_TRACE", "$)"])
    print(f"LLVM timing traces: {env['llvm_diagnostics']}")
