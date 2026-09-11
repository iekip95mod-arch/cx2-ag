#!/usr/bin/env python3

import argparse
import json
import os
import random
import re
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass, fields, is_dataclass
from pathlib import Path

try:
    import sympy
    from sympy import (
        E,
        Eq,
        Symbol,
        atan,
        cos,
        diff,
        exp,
        log,
        pi,
        simplify,
        sin,
        solve,
        sqrt,
        tan,
    )
    from sympy.integrals.manualintegrate import integral_steps, manualintegrate
    from sympy.parsing.sympy_parser import (
        convert_xor,
        implicit_multiplication_application,
        parse_expr,
        standard_transformations,
    )
except ImportError:
    print("oracle error: install SymPy 1.14.0 in nps/build/oracle-venv", file=sys.stderr)
    raise SystemExit(2)


REQUIRED_SYMPY = "1.14.0"
TRANSFORMATIONS = standard_transformations + (convert_xor, implicit_multiplication_application)
RULE_CLASSES = {
    "i.constant": {"ConstantRule"},
    "i.sum": {"AddRule"},
    "i.constant-multiple": {"ConstantTimesRule"},
    "i.power": {"PowerRule"},
    "i.reciprocal": {"ReciprocalRule"},
    "i.linear-substitution": {"URule"},
    "i.function": {"SinRule", "CosRule", "ExpRule"},
}
SAFE_GLOBALS = {
    "__builtins__": {},
    "Float": sympy.Float,
    "Integer": sympy.Integer,
    "Rational": sympy.Rational,
    "Symbol": Symbol,
}


@dataclass(frozen=True)
class Golden:
    name: str
    family: str
    problem: str
    variable: str
    outcome: str
    result: str
    rules: tuple[str, ...]


@dataclass(frozen=True)
class GeneratedCase:
    name: str
    operation: str
    problem: str
    variable: str = "x"


def scalar(text: str, variable: str = "x"):
    text = text.replace("π", "pi")
    if len(text) > 4096 or not re.fullmatch(r"[0-9A-Za-z_+*/^().,\s-]+", text):
        raise ValueError("expression contains unsupported characters")
    names = {
        variable: Symbol(variable),
        "C": Symbol("C"),
        "E": E,
        "pi": pi,
        "sin": sin,
        "cos": cos,
        "tan": tan,
        "exp": exp,
        "ln": log,
        "log": log,
        "sqrt": sqrt,
        "arctan": atan,
    }
    allowed = set(names)
    unknown = sorted(set(re.findall(r"[A-Za-z_][A-Za-z0-9_]*", text)) - allowed)
    if unknown:
        raise ValueError(f"unknown names: {', '.join(unknown)}")
    return parse_expr(
        text,
        local_dict=names,
        global_dict=SAFE_GLOBALS,
        transformations=TRANSFORMATIONS,
        evaluate=True,
    )


def equal(left, right) -> bool:
    return simplify(left - right) == 0


def antiderivatives_equal(left, right, variable) -> bool:
    return equal(left, right) or simplify(diff(left - right, variable)) == 0


def load_golden(path: Path) -> Golden:
    text = path.read_text(encoding="utf-8")
    fields: dict[str, str] = {}
    for line in text.splitlines():
        if line == "context":
            break
        if ": " in line:
            key, value = line.split(": ", 1)
            fields[key] = value
    rules = tuple(re.findall(r"^\s+rule:\s+([^,\s]+)", text, re.MULTILINE))
    family = path.stem.split("_", 1)[0]
    return Golden(
        path.stem,
        family,
        fields.get("problem", ""),
        fields.get("variable", "x"),
        fields.get("outcome", ""),
        fields.get("result", ""),
        rules,
    )


def rule_class_names(value) -> set[str]:
    found: set[str] = set()

    def visit(node) -> None:
        name = type(node).__name__
        module = type(node).__module__
        if module.startswith("sympy.integrals.manualintegrate") and name.endswith("Rule"):
            found.add(name)
        if isinstance(node, (list, tuple)):
            for child in node:
                visit(child)
            return
        if is_dataclass(node):
            for field in fields(node):
                visit(getattr(node, field.name))

    visit(value)
    return found


def finding(findings: list[dict[str, str]], case: str, kind: str, detail: str) -> None:
    findings.append({"case": case, "kind": kind, "detail": detail})


def check_derivative(golden: Golden, findings: list[dict[str, str]]) -> None:
    variable = Symbol(golden.variable)
    expected = diff(scalar(golden.problem, golden.variable), variable)
    if golden.result:
        observed = scalar(golden.result, golden.variable)
        if not equal(observed, expected):
            finding(findings, golden.name, "value-disagreement", f"NPS={observed}, SymPy={expected}")
    elif golden.outcome == "unsupported form":
        finding(findings, golden.name, "capability-gap", f"SymPy differentiates to {expected}")


def check_integral(golden: Golden, findings: list[dict[str, str]]) -> None:
    variable = Symbol(golden.variable)
    integrand = scalar(golden.problem, golden.variable)
    expected = manualintegrate(integrand, variable)
    steps = integral_steps(integrand, variable)
    classes = rule_class_names(steps)
    if golden.result:
        observed = scalar(golden.result, golden.variable).subs(Symbol("C"), 0)
        if not antiderivatives_equal(observed, expected, variable):
            finding(findings, golden.name, "value-disagreement", f"NPS={observed}, SymPy={expected}")
        if not equal(diff(observed, variable), integrand):
            finding(findings, golden.name, "derivative-check", "NPS antiderivative does not differentiate to the integrand")
    elif golden.outcome == "unsupported form":
        finding(findings, golden.name, "capability-gap", f"SymPy integrates to {expected}")
    for rule in golden.rules:
        expected_classes = RULE_CLASSES.get(rule)
        if expected_classes and classes.isdisjoint(expected_classes):
            names = ", ".join(sorted(expected_classes))
            finding(findings, golden.name, "rule-tree-disagreement", f"{rule} expected one of {names}, SymPy used {sorted(classes)}")


def split_equation(text: str, variable: str):
    if text.count("=") != 1:
        raise ValueError("not one equation")
    left, right = text.split("=", 1)
    return scalar(left, variable), scalar(right, variable)


def check_linear(golden: Golden, findings: list[dict[str, str]]) -> None:
    if golden.outcome in {"cancelled", "resource exceeded", "not an equation"}:
        return
    variable = Symbol(golden.variable)
    left, right = split_equation(golden.problem, golden.variable)
    difference = simplify(left - right)
    solutions = solve(Eq(left, right), variable)
    if golden.result:
        observed = scalar(golden.result, golden.variable)
        if not any(equal(observed, candidate) for candidate in solutions):
            finding(findings, golden.name, "value-disagreement", f"NPS={observed}, SymPy={solutions}")
        return
    if golden.outcome == "no solution" and solutions:
        finding(findings, golden.name, "solution-set-disagreement", f"NPS has none, SymPy={solutions}")
    elif golden.outcome == "true for every value" and difference != 0:
        finding(findings, golden.name, "solution-set-disagreement", f"NPS has all values, residual={difference}")
    elif golden.outcome not in {"no solution", "true for every value"} and solutions:
        finding(findings, golden.name, "capability-gap", f"NPS={golden.outcome}, SymPy={solutions}")


def check_goldens(directory: Path) -> tuple[int, list[dict[str, str]]]:
    findings: list[dict[str, str]] = []
    count = 0
    for path in sorted(directory.glob("*.txt")):
        golden = load_golden(path)
        if golden.family not in {"differentiate", "integrate", "linear"}:
            continue
        count += 1
        try:
            if golden.family == "differentiate":
                check_derivative(golden, findings)
            elif golden.family == "integrate":
                check_integral(golden, findings)
            else:
                check_linear(golden, findings)
        except Exception as error:
            finding(findings, golden.name, "oracle-error", f"{type(error).__name__}: {error}")
    return count, findings


def signed(value: int) -> str:
    return f" + {value}" if value >= 0 else f" - {-value}"


def generated_cases(total: int, seed: int) -> list[GeneratedCase]:
    rng = random.Random(seed)
    cases: list[GeneratedCase] = []
    operations = ("differentiate_local", "integrate_local", "solve_local")
    for index in range(total):
        operation = operations[index % len(operations)]
        a = rng.choice((-5, -4, -3, -2, 2, 3, 4, 5))
        b = rng.randint(-7, 7)
        power = rng.randint(1, 6)
        variant = (index // len(operations)) % 6
        if operation == "differentiate_local":
            expressions = (
                f"x^{power}",
                f"{a}*x^{power}",
                f"sin({a}*x{signed(b)})",
                f"cos({a}*x{signed(b)})",
                f"exp({a}*x{signed(b)})",
                f"x^{power}*sin(x)",
            )
            problem = expressions[variant]
        elif operation == "integrate_local":
            expressions = (
                f"x^{power}",
                f"{a}*x^{power}",
                f"sin({a}*x{signed(b)})",
                f"cos({a}*x{signed(b)})",
                f"exp({a}*x{signed(b)})",
                f"1/({a}*x{signed(b)})",
            )
            problem = expressions[variant]
        else:
            slope = rng.choice((-5, -4, -3, -2, 2, 3, 4, 5))
            while slope == a:
                slope = rng.choice((-5, -4, -3, -2, 2, 3, 4, 5))
            answer = rng.randint(-9, 9)
            right_constant = (a - slope) * answer + b
            problem = f"{a}*x{signed(b)} = {slope}*x{signed(right_constant)}"
        cases.append(GeneratedCase(f"generated-{index + 1:04d}", operation, problem))
    return cases


def lua_string(value: str) -> str:
    return json.dumps(value, ensure_ascii=True)


def lua_driver(cases: list[GeneratedCase]) -> str:
    rows = []
    for case in cases:
        rows.append(
            "  {%s, %s, %s, %s},"
            % tuple(lua_string(value) for value in (case.name, case.operation, case.problem, case.variable))
        )
    return "\n".join(
        [
            "package.cpath = arg[1] .. ';' .. package.cpath",
            "local nps = require('nps_split')",
            "local cases = {",
            *rows,
            "}",
            "local function field(value)",
            "  value = tostring(value or '')",
            "  return value:gsub('[\\t\\r\\n]', ' ')",
            "end",
            "for _, case in ipairs(cases) do",
            "  local ok, reply = pcall(nps[case[2]], case[3], case[4])",
            "  if not ok then",
            "    print(case[1] .. '\\terror\\t\\t0\\t\\t' .. field(reply))",
            "  else",
            "    local answer = reply.canonical or reply.result or ''",
            "    print(case[1] .. '\\tok\\t' .. field(reply.outcome) .. '\\t' ..",
            "          (reply.solved and '1' or '0') .. '\\t' .. field(answer) .. '\\t' .. field(reply.detail))",
            "  end",
            "end",
            "",
        ]
    )


def sanitizer_environment() -> dict[str, str]:
    environment = os.environ.copy()
    compiler = shutil.which(os.environ.get("CXX", "c++"))
    if not compiler:
        return environment
    query = subprocess.run(
        [compiler, "-print-file-name=libclang_rt.asan_osx_dynamic.dylib"],
        capture_output=True,
        text=True,
        check=False,
    )
    runtime = Path(query.stdout.strip())
    if query.returncode == 0 and runtime.is_file():
        existing = environment.get("DYLD_INSERT_LIBRARIES")
        environment["DYLD_INSERT_LIBRARIES"] = str(runtime) + (":" + existing if existing else "")
    return environment


def run_generated(cases: list[GeneratedCase], module: Path, luajit: str) -> dict[str, tuple[str, str, str, str, str]]:
    with tempfile.TemporaryDirectory(prefix="nps-oracle-") as directory:
        driver = Path(directory) / "driver.lua"
        driver.write_text(lua_driver(cases), encoding="utf-8")
        completed = subprocess.run(
            [luajit, str(driver), str(module)],
            capture_output=True,
            text=True,
            timeout=120,
            env=sanitizer_environment(),
            check=False,
        )
    if completed.returncode != 0:
        raise RuntimeError(f"Lua bridge exited {completed.returncode}: {completed.stderr.strip()}")
    replies: dict[str, tuple[str, str, str, str, str]] = {}
    for line in completed.stdout.splitlines():
        fields = line.split("\t", 5)
        if len(fields) == 6:
            replies[fields[0]] = (fields[1], fields[2], fields[3], fields[4], fields[5])
    if len(replies) != len(cases):
        raise RuntimeError(f"Lua bridge returned {len(replies)} of {len(cases)} cases")
    return replies


def check_generated(cases: list[GeneratedCase], replies: dict[str, tuple[str, str, str, str, str]]) -> list[dict[str, str]]:
    findings: list[dict[str, str]] = []
    variable = Symbol("x")
    for case in cases:
        state, outcome, solved_flag, answer, detail = replies[case.name]
        if state != "ok":
            finding(findings, case.name, "bridge-error", detail)
            continue
        if solved_flag != "1" or not answer:
            finding(findings, case.name, "generated-capability-gap", f"{case.operation} returned {outcome}: {detail}")
            continue
        try:
            observed = scalar(answer).subs(Symbol("C"), 0)
            if case.operation == "differentiate_local":
                expected = diff(scalar(case.problem), variable)
            elif case.operation == "integrate_local":
                expected = manualintegrate(scalar(case.problem), variable)
            else:
                left, right = split_equation(case.problem, case.variable)
                candidates = solve(Eq(left, right), variable)
                if any(equal(observed, candidate) for candidate in candidates):
                    continue
                expected = candidates
                finding(findings, case.name, "generated-value-disagreement", f"{case.problem}: NPS={observed}, SymPy={expected}")
                continue
            if case.operation == "integrate_local":
                agrees = antiderivatives_equal(observed, expected, variable)
            else:
                agrees = equal(observed, expected)
            if not agrees:
                finding(findings, case.name, "generated-value-disagreement", f"{case.problem}: NPS={observed}, SymPy={expected}")
        except Exception as error:
            finding(findings, case.name, "oracle-error", f"{type(error).__name__}: {error}")
    return findings


def arguments() -> argparse.Namespace:
    root = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description="Compare StepCAS fixtures and generated cases with SymPy")
    parser.add_argument("--fixtures", type=Path, default=root / "tests/golden/fixtures")
    parser.add_argument("--module", type=Path, default=root / "build/san/nps_split.so")
    parser.add_argument("--luajit", default=shutil.which("luajit") or "luajit")
    parser.add_argument("--generated", type=int, default=96)
    parser.add_argument("--seed", type=int, default=20260904)
    parser.add_argument("--json", action="store_true")
    return parser.parse_args()

def main() -> int:
    options = arguments()
    if sympy.__version__ != REQUIRED_SYMPY:
        print(f"oracle error: SymPy {REQUIRED_SYMPY} required, found {sympy.__version__}", file=sys.stderr)
        return 2
    if options.generated < 0:
        print("oracle error: --generated must be non-negative", file=sys.stderr)
        return 2
    if not options.fixtures.is_dir():
        print(f"oracle error: fixture directory not found: {options.fixtures}", file=sys.stderr)
        return 2
    if options.generated and not options.module.is_file():
        print(f"oracle error: Lua bridge module not found: {options.module}", file=sys.stderr)
        return 2
    if options.generated and not shutil.which(options.luajit):
        print(f"oracle error: LuaJIT not found: {options.luajit}", file=sys.stderr)
        return 2

    golden_count, findings = check_goldens(options.fixtures)
    generated = generated_cases(options.generated, options.seed)
    if generated:
        try:
            replies = run_generated(generated, options.module.resolve(), options.luajit)
            findings.extend(check_generated(generated, replies))
        except Exception as error:
            print(f"oracle error: {error}", file=sys.stderr)
            return 2

    report = {
        "sympy": sympy.__version__,
        "goldens": golden_count,
        "generated": len(generated),
        "seed": options.seed,
        "findings": findings,
    }
    if options.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    else:
        print(
            f"oracle: SymPy {sympy.__version__}, {golden_count} goldens, "
            f"{len(generated)} generated, {len(findings)} findings"
        )
        for item in findings:
            print(f"FINDING {item['case']} [{item['kind']}]: {item['detail']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
