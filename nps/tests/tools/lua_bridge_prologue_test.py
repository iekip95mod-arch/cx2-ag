#!/usr/bin/env python3

# AGENTS.md: raising Lua arguments are validated before GcPause or any other request-owned C++
# object, because a raise is a longjmp on the device and skips the destructor. Every bridge function
# that constructs one is found here rather than listed, so an entry point added later is in scope
# the day it is written.

import pathlib
import sys

# A Lua API call that can raise on a bad argument or through a metamethod the caller supplied.
RAISING_CALLS = [
    "luaL_check",
    "luaL_opt",
    "luaL_argerror",
    "luaL_typerror",
    "luaL_error(",
    "lua_error(",
    "lua_getfield(",
    "lua_gettable(",
    "lua_call(",
]

OWNED_OBJECTS = ["GcPause paused(L);", "Arena arena;"]

SIGNATURE_SKIP = ("#", "/", "}", " ", "\t", "namespace", "struct", "class", "enum", "union",
                  "static_assert", "template", "using", "extern", "typedef")


class Function:
    def __init__(self, name: str, signature: str, body: str) -> None:
        self.name = name
        self.signature = signature
        self.body = body


def functions(source: str) -> list[Function]:
    lines = source.split("\n")
    found = []
    index = 0
    while index < len(lines):
        line = lines[index]
        if (line and not line.startswith(SIGNATURE_SKIP) and "(" in line and "{" in line
                and line.rstrip().endswith("}")):
            # A definition that opens and closes on its own line.
            name = line[:line.index("(")].split()[-1].lstrip("*&")
            found.append(Function(name, line, line[line.index("{") + 1:line.rindex("}")]))
        elif (line and not line.startswith(SIGNATURE_SKIP) and "(" in line
                and not line.rstrip().endswith(";")):
            last = index
            signature = line
            while (not lines[last].rstrip().endswith(("{", ";"))
                   and last + 1 < len(lines) and last < index + 6):
                last += 1
                signature += " " + lines[last].strip()
            if lines[last].rstrip().endswith("{"):
                close = last + 1
                while close < len(lines) and lines[close] != "}":
                    close += 1
                name = line[:line.index("(")].split()[-1].lstrip("*&")
                found.append(Function(name, signature, "\n".join(lines[last + 1:close])))
                index = close
        index += 1
    return found


def is_identifier(character: str) -> bool:
    return character.isalnum() or character == "_"


def calls(text: str, name: str) -> bool:
    position = text.find(name + "(")
    while position >= 0:
        if position == 0 or not is_identifier(text[position - 1]):
            return True
        position = text.find(name + "(", position + 1)
    return False


def raising_functions(defined: list[Function]) -> set[str]:
    raising = {f.name for f in defined if any(call in f.body for call in RAISING_CALLS)}
    changed = True
    while changed:
        changed = False
        for f in defined:
            if f.name not in raising and any(
                    calls(f.body, other) for other in raising if other != f.name):
                raising.add(f.name)
                changed = True
    return raising


def block_end(body: str, start: int) -> int:
    # Braces inside string and character literals and comments do not count.
    depth = 0
    position = start
    while position < len(body):
        character = body[position]
        if body.startswith("//", position):
            newline = body.find("\n", position)
            position = len(body) if newline < 0 else newline
            continue
        if body.startswith("/*", position):
            close = body.find("*/", position + 2)
            position = len(body) if close < 0 else close + 2
            continue
        if character in "\"'":
            position += 1
            while position < len(body) and body[position] != character:
                position += 2 if body[position] == "\\" else 1
        elif character == "{":
            depth += 1
        elif character == "}":
            if depth == 0:
                return position
            depth -= 1
        position += 1
    return len(body)


def violations(f: Function, raising: set[str]) -> list[str]:
    found = []
    for owned in OWNED_OBJECTS:
        start = f.body.find(owned)
        while start >= 0:
            lifetime = f.body[start:block_end(f.body, start)]
            for call in RAISING_CALLS:
                if call in lifetime:
                    found.append(f"{f.name} calls {call} while {owned} is alive")
            for helper in sorted(raising):
                if helper != f.name and calls(lifetime, helper):
                    found.append(f"{f.name} calls {helper}, which can raise, while {owned} is alive")
            start = f.body.find(owned, start + 1)
    return found


def bridge_functions(defined: list[Function]) -> list[Function]:
    return [f for f in defined if "(lua_State *L" in f.signature
            and any(owned in f.body for owned in OWNED_OBJECTS)]


def hoisted(f: Function) -> Function:
    # The mutation the guard exists for, the first owned object moved above everything else.
    first = min(f.body.find(o) for o in OWNED_OBJECTS if o in f.body)
    line_start = f.body.rfind("\n", 0, first) + 1
    line_end = f.body.find("\n", first)
    line = f.body[line_start:line_end]
    body = f.body[:line_start] + f.body[line_end + 1:]
    return Function(f.name, f.signature, line + "\n" + body)


def check_order(body: str, name: str, ordered_fragments: list[str]) -> None:
    positions = [body.index(fragment) for fragment in ordered_fragments]
    if positions != sorted(positions):
        raise AssertionError(
            f"{name} must order " + " before ".join(ordered_fragments)
        )


def body_of(defined: list[Function], name: str) -> str:
    return next(f.body for f in defined if f.name == name)


def main() -> int:
    source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
    defined = functions(source)
    raising = raising_functions(defined)
    bridge = bridge_functions(defined)

    # Enumerating nothing would pass every check below, so the sweep has to find the known sites.
    names = {f.name for f in bridge}
    for expected in ["l_magnitude_angle_to_components", "l_components_to_magnitude_angle",
                     "l_forces", "l_density", "solve_into", "kinematics_into", "l_walkthrough"]:
        if expected not in names:
            raise AssertionError(f"the sweep did not find {expected}")

    problems = [problem for f in bridge for problem in violations(f, raising)]
    if problems:
        raise AssertionError("\n".join(problems))

    # Every site that validates a raising argument first must fail once that order is reversed.
    watched = 0
    for f in bridge:
        first = min(f.body.find(o) for o in OWNED_OBJECTS if o in f.body)
        prologue = f.body[:first]
        if not (any(call in prologue for call in RAISING_CALLS)
                or any(calls(prologue, helper) for helper in raising if helper != f.name)):
            continue
        if not violations(hoisted(f), raising):
            raise AssertionError(f"{f.name} with its owned object hoisted was not reported")
        watched += 1

    check_order(
        body_of(defined, "l_magnitude_angle_to_components"),
        "l_magnitude_angle_to_components",
        ["magnitude_angle_text(", "GcPause paused(L);", "Arena arena;", "magnitude_angle_parse("],
    )
    check_order(
        body_of(defined, "l_components_to_magnitude_angle"),
        "l_components_to_magnitude_angle",
        [
            "vector_expression_text(",
            "angle_unit_field(",
            "GcPause paused(L);",
            "Arena arena;",
            "vector_expression_parse(",
        ],
    )
    print(f"lua bridge prologues: {len(bridge)} functions with request-owned objects, "
          f"{watched} hoisting mutations reported, passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
