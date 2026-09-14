#!/usr/bin/env bash
# Makefile.ki's parser and lexer generator selection, checked on whatever host is running this.
# The package build shells out to that makefile, so a tool named there that this host does not have
# stops the ARM package stage after the whole Giac tree has already compiled.
set -u

giac_src=${1:?giac source directory}
work=${2:?scratch directory}

passed=0
failed=0

pass() {
    passed=$((passed + 1))
    printf 'PASS: %s\n' "$1"
}

fail() {
    failed=$((failed + 1))
    printf 'FAIL: %s\n' "$1" >&2
    [ -z "${2:-}" ] || printf '%s\n' "$2" >&2
}

# Decided here rather than by asking the makefile, because the makefile's own answer is the thing
# under test. Bison 2.3 cannot read this grammar, so a major below 3 does not count as present.
usable_bison() {
    [ -n "$1" ] || return 1
    local major
    major=$("$1" --version 2>/dev/null | sed -n '1s/^.*)[^0-9]*\([0-9][0-9]*\).*$/\1/p') || return 1
    [ -n "$major" ] || return 1
    [ "$major" -ge 3 ] 2>/dev/null
}

available=""
for candidate in "$(command -v bison || true)" /opt/homebrew/opt/bison/bin/bison; do
    if usable_bison "$candidate"; then
        available=$candidate
        break
    fi
done

if [ -z "$available" ]; then
    printf 'SKIP: no bison 3 or newer on this host, so Makefile.ki generator selection goes untested here\n'
    exit 0
fi

rm -rf "$work"
mkdir -p "$work" || exit 2
for source in Makefile.ki input_parser.yy input_lexer.ll; do
    cp "$giac_src/$source" "$work/$source" || exit 2
done
printf 'print-%%:\n\t@echo $($*)\n' > "$work/query.mk"

selected=$(make -s -C "$work" -f Makefile.ki -f query.mk print-BISON 2>&1)
if [ -n "$selected" ] && command -v "$selected" >/dev/null 2>&1; then
    pass "Makefile.ki selects a bison this host has, $selected"
else
    fail 'Makefile.ki selects a bison this host does not have' "selected: $selected, available: $available"
fi

if parser_output=$(make -C "$work" -f Makefile.ki input_parser.cc 2>&1); then
    if [ -s "$work/input_parser.cc" ] && [ -s "$work/input_parser.h" ]; then
        pass 'Makefile.ki generates the parser and its header'
    else
        fail 'Makefile.ki reported success without both parser outputs' "$parser_output"
    fi
    # The prefix is what makes the generated parser link against input_lexer.h's declarations, so a
    # generator swap that lost it would still produce a file and still break the package link.
    if grep -q 'giac_yyparse' "$work/input_parser.h"; then
        pass 'the generated parser keeps the giac_yy prefix'
    else
        fail 'the generated parser lost the giac_yy prefix' "$(head -40 "$work/input_parser.h")"
    fi
else
    fail 'Makefile.ki cannot generate the parser on this host' "$parser_output"
fi

selected_lex=$(make -s -C "$work" -f Makefile.ki -f query.mk print-LEX 2>&1)
if [ -n "$selected_lex" ] && command -v "$selected_lex" >/dev/null 2>&1; then
    pass "Makefile.ki selects a lexer generator this host has, $selected_lex"
    if lexer_output=$(make -C "$work" -f Makefile.ki input_lexer.cc 2>&1); then
        if grep -q 'giac_yylex' "$work/input_lexer.cc"; then
            pass 'the generated lexer keeps the giac_yy prefix'
        else
            fail 'the generated lexer lost the giac_yy prefix' "$lexer_output"
        fi
    else
        fail 'Makefile.ki cannot generate the lexer on this host' "$lexer_output"
    fi
elif command -v flex >/dev/null 2>&1; then
    fail 'Makefile.ki selects a lexer generator this host does not have' "selected: $selected_lex"
else
    printf 'SKIP: no flex on this host, so the lexer rule goes untested here\n'
fi

printf 'giac parser tools: %d passed, %d failed\n' "$passed" "$failed"
[ "$failed" -eq 0 ]
