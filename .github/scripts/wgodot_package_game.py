# wgodot-changes::file
import argparse
import copy
import lzma
import os
import tarfile
import zipfile
from pathlib import Path


def report(source, destination):
    before = (
        int(source) if source.isdecimal() else sum(p.stat().st_size for p in Path(source).rglob("*") if p.is_file())
    )
    after = destination.stat().st_size
    saved = 100 * (1 - after / before) if before else 0
    row = f"| {destination.name} | {before:,} | {after:,} | {saved:.2f}% |"
    print(row, flush=True)
    summary = os.environ.get("GITHUB_STEP_SUMMARY")
    if summary:
        with open(summary, "a", encoding="utf-8") as stream:
            stream.write(
                "\n| Package | Input bytes | Output bytes | Saved |\n| --- | ---: | ---: | ---: |\n" + row + "\n"
            )


def repack_apk(source, destination):
    with zipfile.ZipFile(source) as original, zipfile.ZipFile(destination, "w") as packed:
        for entry in original.infolist():
            # Remove signatures invalidated by repacking, but retain other META-INF metadata.
            if entry.filename.startswith("META-INF/") and entry.filename.upper().endswith((
                ".SF",
                ".RSA",
                ".DSA",
                ".EC",
                "/MANIFEST.MF",
            )):
                continue
            info = copy.copy(entry)
            info.extra = b""  # zipalign rebuilds alignment padding.
            # AssetManager supports compressed assets. Preserve AAPT's storage decisions for
            # resources.arsc, native libraries and res/ files that may require direct mmap.
            if entry.filename.startswith("assets/") and not entry.is_dir():
                info.compress_type = zipfile.ZIP_DEFLATED
            packed.writestr(info, original.read(entry), compresslevel=9)
    # Verify all payloads using the ZIP reader before Android's Zopfli/alignment/signing step.
    with zipfile.ZipFile(destination) as packed:
        bad = packed.testzip()
        if bad:
            raise RuntimeError(f"Corrupt repacked APK entry: {bad}")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("tar-xz", "zip", "apk", "report"))
    parser.add_argument("source")
    parser.add_argument("destination", type=Path)
    parser.add_argument("--original-size", type=int)
    args = parser.parse_args()
    if args.mode == "apk":
        repack_apk(args.source, args.destination)
        return
    if args.mode == "tar-xz":
        # One solid stream retains matches across files; preset 9 uses a 64 MiB dictionary.
        with lzma.open(args.destination, "wb", preset=9 | lzma.PRESET_EXTREME) as compressed:
            with tarfile.open(fileobj=compressed, mode="w|") as archive:
                archive.add(args.source, arcname=".")
    elif args.mode == "zip":
        with zipfile.ZipFile(args.destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for path in sorted(Path(args.source).rglob("*")):
                if path.is_file():
                    # Brotli payloads are already compressed; avoid a second compression pass.
                    method = zipfile.ZIP_STORED if path.suffix == ".br" else zipfile.ZIP_DEFLATED
                    archive.write(path, path.relative_to(args.source), compress_type=method)
        with zipfile.ZipFile(args.destination) as archive:
            bad = archive.testzip()
            if bad:
                raise RuntimeError(f"Corrupt Web archive entry: {bad}")
    report(str(args.original_size) if args.original_size is not None else args.source, args.destination)


if __name__ == "__main__":
    main()
