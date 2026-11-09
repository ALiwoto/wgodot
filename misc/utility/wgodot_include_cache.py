# wgodot-changes::file
"""Cache include spellings, while SCons still resolves and checks every dependency."""

from __future__ import annotations

import atexit
import json
import os
from pathlib import Path
from threading import Lock
from typing import TypedDict, cast

from SCons.Node.FS import File
from SCons.Scanner import ClassicCPP
from SCons.Tool import SourceFileScanner


class Entry(TypedDict):
    revision: list[int]
    includes: list[list[str]]


class Cache(TypedDict):
    pattern: str
    files: dict[str, Entry]


class IncludeScanner(ClassicCPP):
    def __init__(self, prototype: ClassicCPP, path: Path) -> None:
        super().__init__(prototype.name, prototype.skeys, "CPPPATH", prototype.cre.pattern)
        self.cache_path: Path = path
        self.lock: Lock = Lock()
        self.previous: dict[str, Entry] = {}
        self.current: dict[str, Entry] = {}
        self.hits: int = 0
        self.misses: int = 0
        try:
            stored: Cache = cast(Cache, json.loads(path.read_text(encoding="utf-8")))
            if stored.get("pattern") == self.cre.pattern:
                self.previous = stored["files"]
        except (OSError, ValueError, KeyError):
            pass  # This cache is disposable; fall back to normal scanning.
        atexit.register(self.save)

    @staticmethod
    def revision(path: str) -> list[int]:
        info: os.stat_result = os.stat(path)
        return [info.st_size, info.st_mtime_ns, info.st_ctime_ns]

    def find_include_names(self, node: File) -> list[tuple[str, str, str]]:
        path: str = node.get_abspath()
        revision: list[int] = self.revision(path)
        with self.lock:
            cached: Entry | None = self.previous.get(path)
        if cached and cached["revision"] == revision:
            includes: list[tuple[str, str, str]] = [(entry[0], entry[1], entry[2]) for entry in cached["includes"]]
            with self.lock:
                self.hits += 1
        else:
            with self.lock:
                self.misses += 1
            includes = super().find_include_names(node)
            if self.revision(path) != revision:
                return includes  # Do not retain a file edited during scanning.
        with self.lock:
            self.current[path] = {"revision": revision, "includes": [list(entry) for entry in includes]}
        return includes

    def save(self) -> None:
        # Partial builds must not evict other targets' include spellings. Prune
        # deleted files; changed files are checked against their revision on use.
        retained: dict[str, Entry] = {
            path: entry for path, entry in self.previous.items()
            if path not in self.current and os.path.isfile(path)
        }
        retained.update(self.current)
        temporary: Path = self.cache_path.with_suffix(f".{os.getpid()}.tmp")
        print(f"C/C++ include cache: {self.hits} reused, {self.misses} scanned.")
        try:
            self.cache_path.parent.mkdir(parents=True, exist_ok=True)
            temporary.write_text(json.dumps({"pattern": self.cre.pattern, "files": retained}), encoding="utf-8")
            temporary.replace(self.cache_path)
        except OSError:
            pass  # A cache write cannot invalidate a completed build.


def install(source: File, path: Path) -> None:
    prototype: ClassicCPP = SourceFileScanner.select(source)
    scanner: IncludeScanner = IncludeScanner(prototype, path)
    for extension, existing in tuple(SourceFileScanner.function.items()):
        if existing is prototype:
            SourceFileScanner.add_scanner(extension, scanner)
