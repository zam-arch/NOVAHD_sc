"""Emit the existing static translation as C and print its compilation plan."""
import json
import sys
from pathlib import Path

from difftest import compile_objects
from lift import Lifter, Unsupported, entries
from macho import Image


def main():
    executable, work, clang, triple, *opt = sys.argv[1:]
    image = Image(Path(executable))
    functions = entries(image)
    lifter = Lifter(image, functions)
    sources = {}
    for address, function in functions.items():
        try:
            sources[address] = lifter.lift(function)
        except Unsupported:
            pass
    commands = compile_objects(image, functions, sources, Path(work),
                               target=(triple, opt, "unused", [clang]), variant="runtime", emit_only=True)
    print(json.dumps(dict(lifted=len(sources), entries=len(functions), commands=commands)))


if __name__ == "__main__":
    main()
