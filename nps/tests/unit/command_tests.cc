#include "nps/steps/command.h"
#include "nps/core/matrix.h"
#include "nps/core/print.h"
#include "unit/adapter_tests.h"

namespace nps {

void run_command_tests(TestSink &t) {
    for (const char *name : {"det", "ref", "rref"}) {
        for (const char *operand : {"[[0,2][3,4]]", "[[0,2] [3,4]]", "[[0,2],[3,4]]"}) {
            Arena arena;
            const Command command = parse_command(arena, std::string(name) + "(" + operand + ")", "x");
            t.check(command.status == CommandStatus::Ready && command.operand_text == operand &&
                        print(arena, command.expression) == "[[0, 2], [3, 4]]" &&
                        read_exact_matrix(arena, command.expression),
                    std::string("TI matrix rows retain their shape through command parsing: ") + name + "(" + operand + ")");
        }
    }
    for (const char *operand : {"[[1/2,0][-3,2^3][4,5]]", "[[1][2][3]]", "[[1,2][3,4],[5,6]]"}) {
        Arena arena;
        const ParseResult parsed = parse(arena, operand);
        t.check(parsed.ok() && read_exact_matrix(arena, parsed.root),
                std::string("TI row adjacency supports rectangular matrices and exact arithmetic: ") + operand);
        if (parsed.ok()) {
            const std::string printed = print(arena, parsed.root);
            const ParseResult roundtrip = parse(arena, printed);
            t.check(roundtrip.ok() && print(arena, roundtrip.root) == printed,
                    "TI matrices round trip through the ordinary comma-separated spelling");
        }
    }
    for (const char *operand : {"[[1][2,3]]", "[[1.0][2]]", "[[0*0.5][2]]", "[[x][2]]", "[[][2]]",
                               "[[[1]][[2]]]"}) {
        Arena arena;
        const Command command = parse_command(arena, "rref(" + std::string(operand) + ")", "x");
        t.check(command.status == CommandStatus::Ready && !read_exact_matrix(arena, command.expression),
                std::string("TI row syntax preserves shape and exactness refusals at matrix admission: ") + operand);
    }
    {
        Arena arena;
        const Command command = parse_command(arena, "rref([[1]2])", "x");
        t.check(command.status == CommandStatus::Ready &&
                    print(arena, command.expression) == "[([1] * 2)]" &&
                    !read_exact_matrix(arena, command.expression),
                "a number after a row multiplies it rather than starting a second row");
    }
    for (const char *text : {"det([[1],])", "det([[1][2])", "det([[1][,2]])",
                            "det([[1]x[2]])", "det([1[2]])", "det([[1]+[2][3]])", "[1][2]"}) {
        Arena arena;
        t.check(!parse(arena, text).ok(), std::string("TI row adjacency does not repair malformed expressions: ") + text);
    }
    {
        Arena arena;
        const ParseResult malformed = parse(arena, "det([[0,2][3,@]])");
        t.check(!malformed.ok() && malformed.offset == 13,
                "TI row syntax preserves the original byte offset of a malformed cell");
    }
    {
        Limits limits;
        limits.max_nodes = 3;
        Arena arena(limits);
        const Command command = parse_command(arena, "det([[0,2][3,4]])", "x");
        t.check(command.status == CommandStatus::ResourceExceeded && arena.failed(),
                "TI row syntax preserves terminal arena resource refusal");
    }
    for (const char *text : {"lim(1/x,x,-\xE2\x88\x9E)", "lim(1/x,x,\xE2\x88\x9E)",
                             "lim(1/x,x,0,1)", "lim(1/x,x,0,-1)", " lim(1/x,x,0,\xE2\x88\x92" "1)"}) {
        Arena arena;
        const Command command = parse_command(arena, text, "y");
        t.check(command.kind == CommandKind::Limit && command.status == CommandStatus::Ready &&
                    command.variable_name == "x" && command.operand_text == "1/x",
                "TI editor limit spelling preserves the operation, operand and explicit variable");
    }
    for (const char *text : {"d(x^2,x)", "integrate(x,x,0,1)", "\xE2\x88\xAB(x,x,0,1)",
                             "\xE2\x88\xAB(x,x,\xE2\x88\x92" "1,0)",
                             "\xEF\x80\x88(x^2,x,1)", " ((\xEF\x80\x88(x^2,x))) "}) {
        Arena arena;
        const Command command = parse_command(arena, text, "x");
        t.check(command.status == CommandStatus::Ready && command.variable_name == "x" &&
                    (command.operand_text == "x" || command.operand_text == "x^2"),
                "TI calculus notation retains its complete operand and variable");
    }
    for (const char *name : {"ref", "rref"}) {
        Arena arena;
        const std::string text = std::string(name) + "([[1,2],[3,4]])";
        const Command command = parse_command(arena, text, "unused + variable");
        t.check(command.status == CommandStatus::Ready && command.expression != kNoNode &&
                    command.variable == kNoNode && command.operand_text == "[[1,2],[3,4]]" &&
                    command_kind_name(command.kind) == std::string(name),
                "matrix commands preserve row lists without requesting a scalar variable");
        for (const char *arguments : {"", "[[1]],x", "[[1]],0,1"}) {
            Arena invalid;
            const Command refused = parse_command(invalid, std::string(name) + "(" + arguments + ")", "x");
            t.check(refused.status != CommandStatus::Ready && refused.status != CommandStatus::Unhandled,
                    "unsupported matrix signatures cannot fall through to ordinary evaluation");
        }
    }
    for (const char *operand : {"[[1,2],[3,4]]", "[[1/2,0],[0,-3/4]]", "A", "[[1,2,3],[4,5,6]]"}) {
        Arena arena;
        const std::string text = "det(" + std::string(operand) + ")";
        const Command command = parse_command(arena, text, "unused + variable");
        t.check(command.status == CommandStatus::Ready && command.expression != kNoNode &&
                    command.variable == kNoNode && command.operand_text == operand &&
                    command_kind_name(command.kind) == std::string("determinant"),
                "determinant dispatch preserves one complete operand for native envelope admission");
    }
    for (const char *text : {"det()", "det([[1]],x)", "det([[1]],0,1)", "det([[1],])"}) {
        Arena arena;
        const Command command = parse_command(arena, text, "x");
        t.check(command.status != CommandStatus::Ready && command.status != CommandStatus::Unhandled &&
                    !command.detail.empty(),
                std::string("determinant signature refusals cannot fall through to CAS: ") + text);
    }
    {
        Arena arena;
        const Command command = parse_command(arena, "linsolve([x + y = 3, x - y = 1], [x, y])", "z");
        t.check(command.status == CommandStatus::Ready &&
                    command_kind_name(command.kind) == std::string("linear system") &&
                    command.expression != kNoNode && arena.at(command.expression).kind == Kind::List &&
                    command.variable != kNoNode && arena.at(command.variable).kind == Kind::List,
                "linsolve dispatches its list of equations and its list of unknowns");
    }
    for (const char *text : {"linsolve([x = 1])", "linsolve([x = 1], [x], 2)", "linsolve()"}) {
        Arena arena;
        const Command command = parse_command(arena, text, "x");
        t.check(command.status == CommandStatus::Unsupported && !command.detail.empty(),
                std::string("a linsolve without exactly two arguments is refused rather than sent to CAS: ") + text);
    }
    const char *commands[] = {"solve(2*x+5=13,x)", "diff(sin(x^2),x)", "int(x^2,x)",
                              "diff(x^3,x,1)", "int(x^2,x,0,1)", "limit((x^2-1)/(x-1),x,1)",
                              "limit(1/x,x,0,1)", "limit(1/x,x,0,-1)",
                              "simplify(2+3)", "expand((x+1)*(x+2))", "factor(x^2-1)",
                              "rearrange(v=u+a*t,a)"};
    for (const char *text : commands) {
        Arena arena;
        const Command command = parse_command(arena, text, "x");
        t.check(command.status == CommandStatus::Ready && command.expression != kNoNode &&
                    command.variable != kNoNode && !command.operand_text.empty(),
                std::string("a complete mathematical command is dispatched: ") + text);
    }
    {
        Arena arena;
        const Command command = parse_command(arena, " diff( sin(x + y), y ) ", "x");
        t.check(command.kind == CommandKind::Differentiate && command.variable_name == "y" &&
                    command.operand_text == " sin(x + y)",
                "nested calls preserve the operand spelling and explicit variable");
    }
    {
        Arena arena;
        const Command command = parse_command(arena, " ((diff(x^2,x))) ", "y");
        t.check(command.status == CommandStatus::Ready && command.variable_name == "x" &&
                    command.operand_text == "x^2",
                "parentheses around a complete call do not hide the command");
    }
    {
        Arena arena;
        const Command command = parse_command(arena, "diff(f(x,y),y)", "x");
        t.check(command.status == CommandStatus::Ready && command.operand_text == "f(x,y)",
                "a comma inside a nested call does not split the command operand");
    }
    for (const char *operand : {"[1,2]", "[[1,2],[3,4]]", "x=[1,2]",
                                "[f(x,y),[1,2]]", "0*[1,2]"}) {
        Arena arena;
        const Command command = parse_command(arena, std::string("diff(") + operand + ",x)", "y");
        t.check(command.status == CommandStatus::Ready && command.operand_text == operand &&
                    command.variable_name == "x",
                std::string("list commas preserve the complete command operand: ") + operand);
    }
    for (const char *text : {"normal(x/x)", "determinant(A)", "det(A)+1", "sin(x)", "1+diff(x,x)",
                             "solve(x=1,x)+2", "factorial(5)+1", "solve"}) {
        Arena arena;
        t.check(parse_command(arena, text, "x").status == CommandStatus::Unhandled,
                std::string("ordinary CAS input is left intact: ") + text);
    }
    for (const char *text : {"iquo(17,5)", "irem(17,5)", "factorial(5)", "perm(5,2)",
                             "comb(5,2)", "is_prime(17)", "nextprime(17)",
                             "powmod(2,10,17)", "ifactor(60)", "gcd(-48,18)"}) {
        Arena arena;
        const Command command = parse_command(arena, text, "unused + variable");
        t.check(command.status == CommandStatus::Ready && command.expression != kNoNode &&
                    command.variable == kNoNode && command.operand_text == text,
                std::string("integer commands preserve the complete call without a variable: ") + text);
    }
    for (const char *text : {"iquo(17)", "irem(17,5,2)", "factorial(5,2)", "perm(5)",
                             "comb(5,2,1)", "is_prime()", "nextprime(17,1)",
                             "powmod(2,10)", "ifactor(60,2)", "gcd(48)", "gcd(48,18,6)"}) {
        Arena arena;
        const Command command = parse_command(arena, text, "x");
        t.check(command.status != CommandStatus::Ready && command.status != CommandStatus::Unhandled &&
                    !command.detail.empty(),
                std::string("integer argument counts cannot fall through to CAS: ") + text);
    }
    for (const char *text : {"diff(x,x,2)", "int(x,x,0)", "solve(x=1,x,y)",
                             "limit(x,x)", "limit(x,x,0,2)", "limit(x,x,0,x)",
                             "lim(x,x)", "lim(x,x,0,2)", "lim(x,x,0,x)",
                             "simplify(x,x)", "factor(x,2)", "rearrange(x=1)",
                             "diff(x,x+1)", "solve(x=1,2)", "int(x,x=2)",
                             "solve(", "diff(x,,x)", "factor()"}) {
        Arena arena;
        const Command command = parse_command(arena, text, "x");
        t.check(command.status != CommandStatus::Ready && command.status != CommandStatus::Unhandled &&
                    !command.detail.empty(),
                std::string("recognized invalid or extended signatures cannot fall through: ") + text);
    }
    {
        Arena arena;
        t.check(parse_command(arena, "diff(x)", "x + y").status == CommandStatus::Invalid,
                "the default variable is one identifier, not an expression");
    }
    {
        Limits limits;
        limits.max_input_bytes = 7;
        Arena arena(limits);
        t.check(parse_command(arena, "diff(x)", "abcdefgh").status == CommandStatus::Invalid,
                "the default variable honors the command arena input-byte limit");
    }
    {
        Arena arena;
        const Command command = parse_command(arena, std::string("diff(x\0,y)", 10), "x");
        t.check(command.status == CommandStatus::Invalid, "embedded NUL does not truncate a command");
    }
    {
        Limits limits;
        limits.max_input_bytes = 8;
        Arena arena(limits);
        t.check(parse_command(arena, "diff(x^2,x)", "x").status == CommandStatus::ResourceExceeded,
                "a recognized command retains parser resource-limit classification");
    }
    for (const char *text : {"diff(x,x,3-2)", "limit(x,x,0,3-2)"}) {
        for (size_t nodes = 1; nodes < 32; ++nodes) {
            Limits limits;
            limits.max_nodes = nodes;
            Arena arena(limits);
            const Command command = parse_command(arena, text, "x");
            t.check(!arena.failed() || command.status == CommandStatus::ResourceExceeded,
                    "command option folding preserves terminal arena failure");
        }
    }
}

}
