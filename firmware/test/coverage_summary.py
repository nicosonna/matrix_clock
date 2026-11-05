#!/usr/bin/env python3
"""Aggregates gcov coverage (JSON format) of the firmware's pure components.

Without gcovr: runs `gcov --json-format` on every object of the instrumented
build, merges lines per source file (a line is covered if AT LEAST one
compilation unit executed it — needed for inline headers and templates compiled
in several units) and prints a table plus a total.

Usage: python coverage_summary.py <build-dir> <component> [<component> ...]
        (paths of the pure components, e.g. components/display)
Exit code 1 if the total is below the target (95 %).
"""
import gzip
import json
import os
import subprocess
import sys
from pathlib import Path

TARGET = 95.0


def main() -> int:
    build = Path(sys.argv[1]).resolve()
    roots = [Path(a).resolve() for a in sys.argv[2:]]
    if not roots:
        print("at least one component to measure", file=sys.stderr)
        return 2
    parent = Path(os.path.commonpath([str(r) for r in roots])) if len(roots) > 1 else roots[0].parent
    objects = sorted(build.rglob("*.cpp.obj"))
    if not objects:
        print(f"no instrumented object under {build}", file=sys.stderr)
        return 2

    # file -> {line: max count}
    lines: dict[str, dict[int, int]] = {}
    for obj in objects:
        subprocess.run(
            ["gcov", "--json-format", "-o", str(obj.parent), str(obj)],
            cwd=build, check=True, capture_output=True,
        )
        for js in build.glob("*.gcov.json.gz"):
            with gzip.open(js, "rt", encoding="utf-8") as f:
                data = json.load(f)
            js.unlink()
            for file in data.get("files", []):
                path = Path(file["file"])
                if not path.is_absolute():
                    path = (build / path).resolve()
                path = path.resolve()
                if not any(path.is_relative_to(r) for r in roots):
                    continue  # outside the pure components (tests, libstdc++)
                target = lines.setdefault(path.relative_to(parent).as_posix(), {})
                for l in file["lines"]:
                    n = l["line_number"]
                    target[n] = max(target.get(n, 0), l["count"])

    total_l = total_c = 0
    print(f"{'file':60} {'lines':>7} {'cov.':>7}")
    for name in sorted(lines):
        d = lines[name]
        n = len(d)
        c = sum(1 for v in d.values() if v > 0)
        total_l += n
        total_c += c
        missing = sorted(k for k, v in d.items() if v == 0)
        extra = f"  (not executed: {missing})" if missing else ""
        print(f"{name:60} {n:7d} {100.0 * c / n:6.1f}%{extra}")
    pct = 100.0 * total_c / total_l if total_l else 0.0
    print(f"{'TOTAL':60} {total_l:7d} {pct:6.1f}%  (target >= {TARGET:.0f} %)")
    return 0 if pct >= TARGET else 1


if __name__ == "__main__":
    sys.exit(main())
