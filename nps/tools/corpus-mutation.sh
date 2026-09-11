#!/usr/bin/env bash
# The same standard section 19.9 sets for the golden fixtures, applied to the acceptance corpus: a
# corpus nothing compares would pass whatever the engines did. So each kind of claim a case makes is
# broken in turn and the audit has to notice.
#
# Five things are proven, and the last two are the ones particular to this corpus. Section 22.1
# counts minimum distinct semantic cases and excludes cosmetic variants, so the gate runs on
# signatures. Padding the corpus with renumbered copies of a case it already holds, or with the same
# case under a different letter, must move the record count and leave the distinct count where it
# was. Otherwise the minimums could be reached without adding any coverage at all.
#
# Nothing in the checkout is touched: the corpus is copied first and the audit is pointed at the copy.
set -u

if [ "$#" -lt 2 ]; then
    echo "usage: corpus-mutation.sh <nps_acceptance_corpus> <corpus dir>"
    exit 2
fi

audit="$1"
corpus="$2"

if [ ! -x "$audit" ]; then
    echo "corpus mutation: skipped, no audit binary at $audit"
    exit 0
fi
if [ ! -d "$corpus" ]; then
    echo "corpus mutation: no corpus at $corpus"
    exit 1
fi

work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT
mkdir -p "$work/live"
cp "$corpus"/*.txt "$work/live/" || exit 1

faults=0

# The ids the audit reported a fault against, one per line and each named once.
faulted_ids() {
    "$audit" "$work/live" "$work/report.md" 2>&1 |
        grep -F 'acceptance corpus: ' |
        awk '$3 ~ /\.txt:[0-9]+$/ { print $4 }' |
        sed 's/:$//' |
        sort -u
}

distinct_for() {
    grep -F "| $1 |" "$work/report.md" | awk -F '|' '{ gsub(/ /, "", $4); print $4 }'
}

records_for() {
    grep -F "| $1 |" "$work/report.md" | awk -F '|' '{ gsub(/ /, "", $5); print $5 }'
}

case_ids() {
    cat "$work/live"/*.txt | awk '$1 == "case" { print $2 }' | sort -u
}

restore() {
    rm -f "$work/live"/*.txt
    cp "$corpus"/*.txt "$work/live/"
}

report() {
    echo "corpus mutation: $1"
    faults=$((faults + 1))
}

total_cases=$(case_ids | wc -l | tr -d ' ')

# A case whose declared outcome is wrong is exactly what this is looking for, so make one and require
# it to be caught before anything else is trusted to the audit. Without this the run would report
# success on an audit that compares nothing at all.
selftest() {
    {
        echo "case zz.selftest.wrong-outcome"
        echo "  family linear"
        echo "  input 2x + 5 = 13"
        echo "  unknown x"
        echo "  expect no solution"
    } > "$work/live/zz_selftest.txt"
    local caught
    caught=$(faulted_ids | grep -c '^zz.selftest.wrong-outcome$')
    rm -f "$work/live/zz_selftest.txt"
    if [ "$caught" -ne 1 ]; then
        echo "corpus mutation: SELFTEST FAILED, a case with the wrong declared outcome was not caught"
        return 1
    fi
    return 0
}

selftest || exit 1

# Every declared outcome is replaced by one no engine returns, so every case has to be reported.
mutate_outcomes() {
    local file
    for file in "$work/live"/*.txt; do
        sed 's/^  expect .*$/  expect no-engine-returns-this/' "$file" > "$file.mutated"
        mv "$file.mutated" "$file"
    done
    local caught
    caught=$(faulted_ids | wc -l | tr -d ' ')
    if [ "$caught" -ne "$total_cases" ]; then
        report "$caught of $total_cases cases were reported with every declared outcome corrupted"
    fi
    restore
}

# Every declared answer is replaced. The outcome lines are left alone, so each case reaches its
# result comparison rather than stopping at the outcome.
mutate_results() {
    local file expected
    expected=$(cat "$work/live"/*.txt | grep -c '^  result ')
    for file in "$work/live"/*.txt; do
        sed 's/^  result .*$/  result no_engine_returns_this/' "$file" > "$file.mutated"
        mv "$file.mutated" "$file"
    done
    local caught
    caught=$(faulted_ids | wc -l | tr -d ' ')
    if [ "$caught" -lt "$expected" ]; then
        report "$caught of $expected cases with a declared answer were reported with every answer corrupted"
    fi
    restore
}

# A family that loses its cases has to lose its distinct count, since that count is what the gate
# reads. A corpus quietly shrinking must not look the same as one that did not. Every family a file
# contributes to is checked rather than the first one it names, because numeric_mode.txt carries a
# dimension that cuts across four families and the old reading saw only the first of them.
mutate_removal() {
    local file family before after
    for file in "$corpus"/*.txt; do
        for family in $(awk '$1 == "family" { print $2 }' "$file" | sort -u); do
            faulted_ids >/dev/null
            before=$(distinct_for "$family")
            rm -f "$work/live/$(basename "$file")"
            faulted_ids >/dev/null
            after=$(distinct_for "$family")
            restore
            if [ "$before" = "0" ] || [ "$after" -ge "$before" ]; then
                report "removing $(basename "$file") left $family reporting $after distinct cases, from $before"
            fi
        done
    done
}

# Five more two-step linear equations, each with a different pair of numbers and each landing on an
# integer. Section 22.1 calls these cosmetic variants of a case the corpus already holds, so the
# record count has to rise by five and the distinct count has to stay exactly where it was.
mutate_padding() {
    local before_distinct before_records after_distinct after_records
    faulted_ids >/dev/null
    before_distinct=$(distinct_for linear)
    before_records=$(records_for linear)
    {
        echo "case zz.padding.one"
        echo "  family linear"
        echo "  input 4x + 6 = 14"
        echo "  unknown x"
        echo "  expect solved"
        echo "  result 2"
        echo ""
        echo "case zz.padding.two"
        echo "  family linear"
        echo "  input 10x + 3 = 23"
        echo "  unknown x"
        echo "  expect solved"
        echo "  result 2"
        echo ""
        echo "case zz.padding.three"
        echo "  family linear"
        echo "  input 7x + 1 = 22"
        echo "  unknown x"
        echo "  expect solved"
        echo "  result 3"
        echo ""
        echo "case zz.padding.four"
        echo "  family linear"
        echo "  input 6x + 12 = 30"
        echo "  unknown x"
        echo "  expect solved"
        echo "  result 3"
        echo ""
        echo "case zz.padding.five"
        echo "  family linear"
        echo "  input 9x + 5 = 41"
        echo "  unknown x"
        echo "  expect solved"
        echo "  result 4"
    } > "$work/live/zz_padding.txt"
    faulted_ids >/dev/null
    after_distinct=$(distinct_for linear)
    after_records=$(records_for linear)
    rm -f "$work/live/zz_padding.txt"
    if [ "$after_records" != "$((before_records + 5))" ]; then
        report "five padding cases moved the linear record count from $before_records to $after_records"
    fi
    if [ "$after_distinct" != "$before_distinct" ]; then
        report "five renumbered copies of a case already in the corpus moved the linear distinct count from $before_distinct to $after_distinct"
    fi
    restore
}

# The same five derivative cases the corpus already holds, written in u instead of x. Which letter a
# problem uses is cosmetic in exactly the way a different pair of numbers is.
mutate_renaming() {
    local before_distinct before_records after_distinct after_records
    faulted_ids >/dev/null
    before_distinct=$(distinct_for derivative)
    before_records=$(records_for derivative)
    {
        echo "case zz.renamed.square"
        echo "  family derivative"
        echo "  input u^2"
        echo "  variable u"
        echo "  expect differentiated"
        echo "  result 2*u"
        echo ""
        echo "case zz.renamed.sine"
        echo "  family derivative"
        echo "  input sin(u)"
        echo "  variable u"
        echo "  expect differentiated"
        echo "  result cos(u)"
        echo ""
        echo "case zz.renamed.exponential"
        echo "  family derivative"
        echo "  input exp(u)"
        echo "  variable u"
        echo "  expect differentiated"
        echo "  result exp(u)"
        echo ""
        echo "case zz.renamed.product"
        echo "  family derivative"
        echo "  input u*sin(u)"
        echo "  variable u"
        echo "  expect differentiated"
        echo "  result sin(u) + u*cos(u)"
        echo ""
        echo "case zz.renamed.milestone"
        echo "  family derivative"
        echo "  input u^2*sin(u)"
        echo "  variable u"
        echo "  expect differentiated"
        echo "  result u^2*cos(u) + 2*u*sin(u)"
    } > "$work/live/zz_renamed.txt"
    faulted_ids >/dev/null
    after_distinct=$(distinct_for derivative)
    after_records=$(records_for derivative)
    rm -f "$work/live/zz_renamed.txt"
    if [ "$after_records" != "$((before_records + 5))" ]; then
        report "five renamed cases moved the derivative record count from $before_records to $after_records"
    fi
    if [ "$after_distinct" != "$before_distinct" ]; then
        report "five cases the corpus already holds under a different letter moved the derivative distinct count from $before_distinct to $after_distinct"
    fi
    restore
}

# The same property on the one family whose cases never reach an engine. Five copies of the shipped
# case invalid.parse.trailing-operator with the number changed, which is a renumbering in exactly the
# sense mutate_padding uses, so the record count has to rise by five and the distinct count has to
# stay where it was.
mutate_refusal_padding() {
    local before_distinct before_records after_distinct after_records n
    faulted_ids >/dev/null
    before_distinct=$(distinct_for invalid)
    before_records=$(records_for invalid)
    {
        for n in 2 3 4 5 6; do
            echo "case zz.refusal.trailing-operator-$n"
            echo "  family invalid"
            echo "  mode derivative"
            echo "  input $n +"
            echo "  variable x"
            echo "  expect not parsed"
            echo ""
        done
    } > "$work/live/zz_refusal.txt"
    faulted_ids >/dev/null
    after_distinct=$(distinct_for invalid)
    after_records=$(records_for invalid)
    rm -f "$work/live/zz_refusal.txt"
    if [ "$after_records" != "$((before_records + 5))" ]; then
        report "five refusal padding cases moved the invalid record count from $before_records to $after_records"
    fi
    if [ "$after_distinct" != "$before_distinct" ]; then
        report "five renumbered copies of a refusal the corpus already holds moved the invalid distinct count from $before_distinct to $after_distinct"
    fi
    restore
}

# The same door with the spacing changed instead of the number, since a signature taken from the
# input text counts whitespace as content and a signature taken from the refusal cannot.
mutate_refusal_spacing() {
    local before_distinct before_records after_distinct after_records n spaces
    faulted_ids >/dev/null
    before_distinct=$(distinct_for invalid)
    before_records=$(records_for invalid)
    {
        spaces=" "
        for n in 2 3 4 5 6; do
            spaces="$spaces "
            echo "case zz.spacing.trailing-operator-$n"
            echo "  family invalid"
            echo "  mode derivative"
            echo "  input 1$spaces+"
            echo "  variable x"
            echo "  expect not parsed"
            echo ""
        done
    } > "$work/live/zz_spacing.txt"
    faulted_ids >/dev/null
    after_distinct=$(distinct_for invalid)
    after_records=$(records_for invalid)
    rm -f "$work/live/zz_spacing.txt"
    if [ "$after_records" != "$((before_records + 5))" ]; then
        report "five respaced refusal cases moved the invalid record count from $before_records to $after_records"
    fi
    if [ "$after_distinct" != "$before_distinct" ]; then
        report "five respaced copies of a refusal the corpus already holds moved the invalid distinct count from $before_distinct to $after_distinct"
    fi
    restore
}

mutate_outcomes
mutate_results
mutate_removal
mutate_padding
mutate_renaming
mutate_refusal_padding
mutate_refusal_spacing

echo "corpus mutation: $total_cases cases, 7 mutations, $faults that the audit did not notice"
[ "$faults" -eq 0 ]
