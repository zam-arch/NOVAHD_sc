"""Compile generated shards after their generator has released its memory."""
import concurrent.futures
import subprocess
from pathlib import Path


def compile_plan(commands, workers: int, *, progress: bool = False) -> list[Path]:
    def compile_one(command):
        output = Path(command[-1])
        if not output.is_file():
            result = subprocess.run(command, capture_output=True, text=True)
            if result.returncode:
                raise RuntimeError(f"compile of {output.name} failed:\n{result.stderr[-4000:]}")
        if progress:
            print(f"ARM64 {output.name}", flush=True)
        return output

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        return list(pool.map(compile_one, commands))
