#!/usr/bin/env python3

import pathlib
import sys


def function_body(source: str, name: str) -> str:
    start = source.index(f"int {name}(lua_State *L) {{")
    end = source.index("\nint ", start + 1)
    return source[start:end]


def check_order(body: str, name: str, ordered_fragments: list[str]) -> None:
    positions = [body.index(fragment) for fragment in ordered_fragments]
    if positions != sorted(positions):
        raise AssertionError(
            f"{name} must order " + " before ".join(ordered_fragments)
        )


def main() -> int:
    source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    check_order(
        function_body(source, "l_magnitude_angle_to_components"),
        "l_magnitude_angle_to_components",
        ["magnitude_angle_text(", "GcPause paused(L);", "Arena arena;", "magnitude_angle_parse("],
    )
    check_order(
        function_body(source, "l_components_to_magnitude_angle"),
        "l_components_to_magnitude_angle",
        [
            "vector_expression_text(",
            "angle_unit_field(",
            "GcPause paused(L);",
            "Arena arena;",
            "vector_expression_parse(",
        ],
    )
    print("lua bridge vector prologues: passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
