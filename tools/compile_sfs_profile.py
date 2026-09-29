"""Compile a local ShaderSwap folder for compatibility auditing.

These are profile replacements before common runtime injection, not a complete
stereo shader cache. Source game shaders and compiled outputs stay user-local.
"""
import argparse
import concurrent.futures
import hashlib
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--compiler", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if not args.profile.is_dir() or not args.compiler.is_file():
        parser.error("Profile directory and glslang compiler must exist")
    args.output.mkdir(parents=True, exist_ok=True)
    files = sorted(p for p in args.profile.iterdir() if p.suffix in (".vert", ".frag", ".comp"))
    if not files:
        parser.error("No profile shaders found")

    def compile_one(source):
        target = args.output / (source.name + ".spv")
        process = subprocess.run(
            [str(args.compiler), "-V", "--target-env", "vulkan1.1", str(source), "-o", str(target)],
            capture_output=True, text=True, timeout=60,
        )
        return {"file": source.name, "success": process.returncode == 0,
                "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "log": process.stdout + process.stderr}

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        results = list(pool.map(compile_one, files))
    manifest = {"purpose": "pre-injection compatibility audit only", "results": results}
    (args.output / "compile-audit.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    passed = sum(r["success"] for r in results)
    print(f"{passed}/{len(results)} profile shaders compiled")
    for result in results:
        if not result["success"]:
            print(result["file"], result["log"])
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
