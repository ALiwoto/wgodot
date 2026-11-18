# wgodot-changes::file
"""Concurrent, resumable downloads for local build toolchains."""

import argparse
from concurrent.futures import ThreadPoolExecutor
import hashlib
from pathlib import Path
import shutil
import time
import urllib.error
import urllib.request


def open_request(url, headers, proxy, method="GET"):
    handler = urllib.request.ProxyHandler({"http": proxy, "https": proxy} if proxy else {})
    opener = urllib.request.build_opener(handler)
    return opener.open(urllib.request.Request(url, headers=headers, method=method), timeout=30)


def request_metadata(url, fallback_proxy):
    for attempt in range(6):
        proxy = fallback_proxy if attempt % 2 else None
        try:
            with open_request(url, {}, proxy, "HEAD") as response:
                return int(response.headers["Content-Length"]), response.headers.get("ETag", "")
        except (OSError, ValueError, urllib.error.URLError) as error:
            print(f"Metadata attempt {attempt + 1}: {error}", flush=True)
            time.sleep(min(attempt + 1, 5))
    raise RuntimeError("Could not retrieve download metadata; retry with the same destination.")


def download_part(url, path, first, last, fallback_proxy):
    expected = last - first + 1
    for attempt in range(8):
        received = path.stat().st_size if path.exists() else 0
        if received == expected:
            return
        if received > expected:
            raise RuntimeError(f"Invalid saved download segment: {path}")
        start = first + received
        proxy = fallback_proxy if attempt % 2 else None
        try:
            with open_request(url, {"Range": f"bytes={start}-{last}"}, proxy) as response:
                content_range = response.headers.get("Content-Range", "")
                if response.status != 206 or not content_range.startswith(f"bytes {start}-{last}/"):
                    raise RuntimeError(f"Server did not honor requested range {start}-{last}: {content_range}")
                with path.open("ab") as output:
                    remaining = expected - received
                    while remaining:
                        block = response.read(min(256 * 1024, remaining))
                        if not block:
                            raise OSError("Connection closed before completing the segment")
                        output.write(block)
                        remaining -= len(block)
            print(f"Completed segment {path.name} ({expected / 1024**2:.1f} MiB)", flush=True)
            return
        except (OSError, urllib.error.URLError) as error:
            print(f"Segment {path.name}, attempt {attempt + 1}: {error}", flush=True)
            time.sleep(min(attempt + 1, 5))
    raise RuntimeError(f"Download interrupted; partial data retained in {path}")


def verify(path, expected_hash):
    if not expected_hash:
        return
    with path.open("rb") as source:
        actual = hashlib.file_digest(source, "sha256").hexdigest()
    if actual.lower() != expected_hash.lower():
        raise RuntimeError(f"SHA256 mismatch for {path}: {actual}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("url")
    parser.add_argument("destination", type=Path)
    parser.add_argument("--connections", type=int, default=8)
    parser.add_argument("--sha256")
    parser.add_argument("--proxy", default="http://localhost:10809")
    args = parser.parse_args()
    if not 1 <= args.connections <= 32:
        parser.error("--connections must be between 1 and 32")
    destination = args.destination.resolve()
    destination.parent.mkdir(parents=True, exist_ok=True)
    if destination.is_file() and args.sha256:
        verify(destination, args.sha256)
        print(f"Reusing verified download: {destination}")
        return
    size, etag = request_metadata(args.url, args.proxy)
    parts = destination.with_name(destination.name + ".parts")
    parts.mkdir(exist_ok=True)
    identity = f"{args.url}\n{size}\n{etag}\n{args.connections}\n"
    metadata = parts / "identity.txt"
    if metadata.exists() and metadata.read_text(encoding="utf-8") != identity:
        raise RuntimeError(f"Download changed; choose a new destination instead of mixing segments: {parts}")
    metadata.write_text(identity, encoding="utf-8")
    segment_size = (size + args.connections - 1) // args.connections
    segments = []
    with ThreadPoolExecutor(max_workers=args.connections) as workers:
        futures = []
        for index, first in enumerate(range(0, size, segment_size)):
            path = parts / f"{index:03d}.part"
            segments.append(path)
            futures.append(workers.submit(download_part, args.url, path, first,
                                          min(first + segment_size, size) - 1, args.proxy))
        for future in futures:
            future.result()
    pending = destination.with_name(destination.name + ".pending")
    with pending.open("wb") as output:
        for segment in segments:
            with segment.open("rb") as source:
                shutil.copyfileobj(source, output, 1024 * 1024)
    verify(pending, args.sha256)
    pending.replace(destination)
    print(f"Downloaded {size / 1024**2:.1f} MiB: {destination}", flush=True)


if __name__ == "__main__":
    main()
