#include <iostream>

#define NPS_GIAC 1
#define NPS_RELEASE_MANIFEST 1
#include "../target/luax_host.cc"

#include "giac.h"

int TCT_Local_Control_Interrupts(int mask) {
    static int current = 0;
    const int previous = current;
    current = mask;
    return previous;
}
extern "C" void reset_gc() {}
extern "C" const char *giac_caseval(const char *expression) {
    giac::vx_var = giac::identificateur("x");
    return giac::caseval(expression);
}

namespace nps {
int show_native_menu(const char *, const char *const *, std::size_t) { return 0; }
}

int main(int argc, char **argv) {
    giac::os_shell = false;
    lua_State *L = luaL_newstate();
    if (!L)
        return 1;
    luaL_openlibs(L);
    luaopen_nps_split(L);
    lua_setglobal(L, "nps");
    if (argc == 2) {
        lua_pushstring(L, argv[1]);
        lua_setglobal(L, "determinant_record_path");
    }
    const char *script = R"lua(
local checks = 0
local function check(ok, what)
    checks = checks + 1
    assert(ok, what)
end
for _, fixture in ipairs({
    {"int(x^2,x,0,1)", "1/3"},
    {"int(x^2,x,1,0)", "-1/3"},
    {"int(sin(x),x,0,1)", "1-cos(1)"},
    {"int(1/x,x,-2,-1)", "-ln(2)"},
    {"int(1/x,x,-1,-2)", "ln(2)"},
    {"int(1/(2*x+1),x,-2,-1)", "-ln(3)/2"},
    {"int(1/(1-2*x),x,1,2)", "-ln(3)/2"},
    {"int(1/x+1/(x+3),x,-2,-1)", "0"},
    {"int(1/t,t,-3/2,-1/2)", "-ln(3)"},
    {"int(ln(x),x,1,2)", "2*ln(2)-1"},
    {"int(ln(x),x,2,1)", "1-2*ln(2)"},
    {"int(ln(2*x+1),x,0,1/2)", "ln(2)-1/2"},
    {"int(ln(1-2*x),x,-1/2,0)", "ln(2)-1/2"},
    {"int(ln(3*t+1),t,0,1/3)", "(2*ln(2)-1)/3"},
    {"int(ln(x)+ln(x+1),x,1,2)", "3*ln(3)-2"},
    {"int(sqrt(x),x,0,4)", "16/3"},
    {"int(sqrt(x),x,4,0)", "-16/3"},
    {"int(sqrt(2*x+1),x,-1/2,3/2)", "8/3"},
    {"int(sqrt(1-2*x),x,-3/2,1/2)", "8/3"},
    {"int(sqrt(3*t+1),t,0,1)", "14/9"},
    {"int(sqrt(x),x,0,2)", "4*sqrt(2)/3"},
    {"limit((x^2-1)/(x-1),x,1)", "2"},
    {"limit(sin(x)/x,x,0)", "1"},
    {"limit((1-cos(x))/x^2,x,0)", "1/2"},
    {"limit((exp(x)-1-x)/x^2,x,0,1)", "1/2"},
    {"limit((sin(x)-x)/x^3,x,0)", "-1/6"},
    {"limit(ln(1+x)/x,x,0)", "1"},
    {"limit((sqrt(1+x)-1)/x,x,0)", "1/2"},
    {"limit(sqrt(x),x,0,1)", "0"},
    {"limit(sqrt(-x),x,0,-1)", "0"},
    {"limit(sqrt(x^2),x,0)", "0"},
    {"limit(sqrt((x-2)^4*(3-x)),x,2)", "0"},
    {"limit(sqrt((2*t-1)^3),t,1/2,1)", "0"},
    {"limit(sqrt(x-x),x,0)", "0"},
    {"limit(exp(sqrt(x)),x,0,1)", "1"},
    {"limit(sqrt(x)+sqrt(x^2),x,0,1)", "0"},
    {"limit((x-1)^2/(x^2-2*x+1),x,1)", "1"},
    {"limit(1/x,x,infinity)", "0"},
    {"lim(1/x,x,-\226\136\158)", "0"},
    {"lim((3*x^2+1)/(2*x^2-5),x,\226\136\158)", "3/2"},
    {"limit(pi,pi,0)", "0"},
    {"diff(sin(x^2),x,1)", "2*x*cos(x^2)"},
}) do
    local record = nps.walkthrough(fixture[1], "x", "exact")
    check(record.solved and record.result and not record.answer_only and record.agrees,
          "actual Giac agrees with native calculus: " .. fixture[1] .. ": " .. tostring(record.detail))
    check(nps.caseval("simplify((" .. record.result .. ")-(" .. fixture[2] .. "))") == "0",
          "actual calculus result agrees with an independently specified answer: " .. fixture[1] ..
          ": " .. tostring(record.result))
end
for _, fixture in ipairs({
    {"limit(sin(x)/sin(2*x),x,0)", "1/2"},
    {"limit(sin(1/x),x,infinity)", "0"},
    {"limit(sin(1/x),x,-infinity)", "0"},
    {"int(x*sin(x),x,0,1)", "sin(1)-cos(1)"},
}) do
    local record = nps.walkthrough(fixture[1], "x", "exact")
    check(record.has_result and record.answer_only and not record.solved,
          "unsupported native calculus keeps its CAS answer distinct")
    check(nps.caseval("normal((" .. record.result .. ")-(" .. fixture[2] .. "))") == "0",
          "actual fallback calculus agrees with an independently specified answer")
end
for _, fixture in ipairs({
    {"ln(x)", "x", "x*ln(x)-x"},
    {"ln(2*x+1)", "x", "(2*x+1)*ln(2*x+1)/2-x"},
    {"ln(1-2*x)", "x", "-(1-2*x)*ln(1-2*x)/2-x"},
    {"-3*ln(x/2)", "x", "-3*x*ln(x/2)+3*x"},
    {"ln(x)+ln(x+1)", "x", "x*ln(x)+(x+1)*ln(x+1)-2*x"},
    {"ln(a*x+b)", "x", "(a*x+b)*ln(a*x+b)/a-x"},
    {"ln(3*t+1)", "t", "(3*t+1)*ln(3*t+1)/3-t"},
}) do
    local record = nps.integrate(fixture[1], fixture[2], "exact")
    check(record.solved and record.status == "solved and verified" and record.agrees and
          not record.answer_only and record.giac_calls == 3,
          "native logarithm steps pass actual Giac derivative verification: " .. fixture[1])
    check(nps.caseval("simplify(diff((" .. record.result .. ")-(" .. fixture[3] .. ")," .. fixture[2] .. "))") == "0",
          "logarithm primitive agrees with the independent formula up to a constant")
    check(record.assumptions and record.assumptions:find("> 0", 1, true),
          "the real logarithm domain remains visible after actual Giac verification")
end
for _, fixture in ipairs({
    {"sqrt(x)", "x", "2*x*sqrt(x)/3"},
    {"sqrt(2*x+1)", "x", "(2*x+1)*sqrt(2*x+1)/3"},
    {"sqrt(1-2*x)", "x", "-(1-2*x)*sqrt(1-2*x)/3"},
    {"sqrt(a*x+b)", "x", "2*(a*x+b)*sqrt(a*x+b)/(3*a)"},
    {"sqrt(3*t+1)", "t", "2*(3*t+1)*sqrt(3*t+1)/9"},
    {"3*sqrt(x)+sqrt(x+1)", "x", "2*x*sqrt(x)+2*(x+1)*sqrt(x+1)/3"},
}) do
    local record = nps.integrate(fixture[1], fixture[2], "exact")
    check(record.solved and record.status == "solved and verified" and record.agrees and not record.answer_only,
          "native square-root steps pass actual Giac derivative verification: " .. fixture[1])
    check(nps.caseval("simplify(diff((" .. record.result .. ")-(" .. fixture[3] .. ")," .. fixture[2] .. "))") == "0",
          "square-root primitive agrees with the independent formula up to a constant")
    check(record.assumptions and record.assumptions:find(">= 0", 1, true),
          "square-root primitive retains its real-domain condition")
end
local fixtures = {
    {"ref([[2,4,6],[0,3,6]])", "[[1,2,3],[0,1,2]]"},
    {"rref([[2,4,6],[0,3,6]])", "[[1,0,-1],[0,1,2]]"},
    {"ref([[0,1,2],[1,0,3]])", "[[1,0,3],[0,1,2]]"},
    {"rref([[1,2,3],[1,2,3]])", "[[1,2,3],[0,0,0]]"},
    {"ref([[-1/2,1]])", "[[1,-2]]"},
    {"rref([[0,2]])", "[[0,1]]"},
}
local installed = {}
for _, module in ipairs(nps.capability_manifest().installed_modules) do
    if module.kind == "solver" then installed[module.id] = true end
end
for _, fixture in ipairs(fixtures) do
    local record = nps.walkthrough(fixture[1], "unused + variable", "exact")
    check(type(record) == "table" and record.solved and record.has_result,
          "an actual Giac matrix walkthrough crosses the Lua boundary: " .. fixture[1])
    check(record.status == "solved and verified" and not record.answer_only,
          "a complete verified walkthrough identifies its answer correctly")
    check(record.request_expression == fixture[1] and record.numeric_mode == "exact",
          "the complete request and numeric mode survive command dispatch")
    check(installed["matrix." .. record.mode .. ".rational"],
          "a working matrix command is listed by the module capability manifest")
    check(nps.canonical(record.result) == nps.canonical(fixture[2]),
          "the Lua matrix answer matches the independent expected matrix")
    check(record.steps[1].kind == "plan", "the solution plan precedes the row operations")
    local transformations = 0
    collectgarbage("collect")
    for _, step in ipairs(record.steps) do
        if step.kind == "transformation" then
            transformations = transformations + 1
            check(type(step.action) == "string" and #step.action > 0,
                  "Do identifies a concrete row operation")
            check(type(step.after) == "string" and step.after:find("[[", 1, true),
                  "Write carries the complete resulting matrix")
            check(type(step.short) == "string" and #step.short > 0,
                  "Why explains the reversible operation")
            check(not step.failed, "no incorrect row operation crosses the bridge")
        end
    end
    check(transformations > 0, "changing matrices expose their actual intermediate operations")
end
for _, command in ipairs({"ref([[0]])", "rref([[1,0],[0,1]])"}) do
    local record = nps.walkthrough(command, "x")
    check(record.solved and record.has_result, "an already reduced matrix has a verified answer")
    for _, step in ipairs(record.steps) do
        check(step.kind ~= "transformation", "an identity does not invent mathematical progress")
    end
end
for _, command in ipairs({"ref([[x]])", "rref([[1.0]])", "ref([[1],[2,3]])",
                           "rref([[1],[2],[3],[4],[5]])", "ref([[1]],x)"}) do
    local record = nps.walkthrough(command, "x")
    check(type(record) == "table" and not record.solved and not record.has_result and not record.result,
          "unsupported matrix inputs return a refusal instead of ordinary CAS evaluation")
end
local decimal = nps.walkthrough("rref([[1]])", "x", "decimal")
check(decimal.outcome == "unsupported form" and not decimal.solved,
      "matrix walkthroughs keep exact and decimal modes distinct")
local bounded = nps.walkthrough("rref([[9223372036854775807,1],[1,9223372036854775807]])", "x")
check(bounded.outcome == "unsupported form" and bounded.status == "unsupported" and
      not bounded.solved and not bounded.has_result,
      "an intermediate rational beyond the checked range is an unsupported form, not a missing backend: " ..
      tostring(bounded.outcome) .. ": " .. tostring(bounded.detail))
nps.test_escape_pressed(true)
local cancelled = nps.walkthrough("rref([[1,2],[3,4]])", "x")
nps.test_escape_pressed(false)
check(cancelled.outcome == "cancelled" and not cancelled.solved and not cancelled.has_result,
      "Escape before a matrix request withholds its answer")
check(nps.walkthrough("rref([[1]])", "x").solved, "a cancelled request permits a later solve")
for _, fixture in ipairs({
    {"det([[5]])", "5"},
    {"det([[-3/2]])", "-3/2"},
    {"det([[0,2],[3,4]])", "-6"},
    {"det([[1/2,1/3],[0,1/4]])", "1/8"},
    {"det([[1,2],[2,4]])", "0"},
    {"det([[0]])", "0"},
    {"det([[1,0],[0,1]])", "1"},
    {"det([[1,1,1,1],[1,-1,1,-1],[1,1,-1,-1],[1,-1,-1,1]])", "16"},
    {"det([[3037000500,3037000499],[3037000501,3037000500]])", "1"},
    {"det([[3037000500,0],[0,3037000500]])", "9223372037000250000"},
}) do
    local record = nps.walkthrough(fixture[1], "unused + variable", "exact")
    check(type(record) == "table" and record.solved and record.has_result,
          "an actual Giac determinant walkthrough crosses Lua: " .. fixture[1])
    check(record.mode == "determinant" and record.outcome == "determined" and
          record.status == "solved and verified" and not record.answer_only and record.giac_calls == 1,
          "a determinant is a verified native scalar walkthrough")
    check(installed["matrix.det.rational"], "the determinant family is installed")
    check(type(record.result) == "string" and not record.result:find("[", 1, true) and
          nps.canonical(record.result) == nps.canonical(fixture[2]),
          "the scalar determinant matches its independent exact expected value")
    check(record.request_expression == fixture[1] and record.numeric_mode == "exact" and
          record.steps[1].kind == "plan", "det preserves the request and begins with a plan")
    collectgarbage("collect")
    local conclusion = false
    for _, step in ipairs(record.steps) do
        check(not step.failed, "det exposes only verified recorded guidance")
        if step.kind == "transformation" then
            check(type(step.action) == "string" and #step.action > 0 and
                  type(step.short) == "string" and #step.short > 0 and
                  type(step.after) == "string" and #step.after > 0,
                  "each determinant transformation supplies Do, Write and Why")
        end
        if step.rule == "matrix.det-correction" then conclusion = true end
    end
    check(conclusion, "det retains its recorded final certificate")
end
for _, command in ipairs({"det([])", "det([1,2])", "det([[1],[2,3]])", "det([[1,2,3],[4,5,6]])",
                           "det([[1],[2],[3],[4],[5]])", "det([[x]])", "det([[1.0]])",
                           "det([[1]],x)", "det([[1,3037000500],[-3037000500,1]])"}) do
    local record = nps.walkthrough(command, "x")
    check(type(record) == "table" and not record.solved and not record.has_result and not record.result,
          "unsupported determinant input has no scalar or ordinary CAS fallback: " .. command)
end
local determinant_decimal = nps.walkthrough("det([[1]])", "x", "decimal")
check(determinant_decimal.outcome == "unsupported form" and not determinant_decimal.solved and
      not determinant_decimal.has_result, "determinants refuse Decimal mode")
nps.test_escape_pressed(true)
local determinant_cancelled = nps.walkthrough("det([[1,2],[3,4]])", "x")
nps.test_escape_pressed(false)
check(determinant_cancelled.outcome == "cancelled" and not determinant_cancelled.solved and
      not determinant_cancelled.has_result, "Escape withholds a determinant answer")
check(nps.walkthrough("det([[1]])", "x").solved, "a cancelled determinant permits the next request")
if determinant_record_path then
    local function encode(value)
        if type(value) == "string" then return string.format("%q", value) end
        if type(value) ~= "table" then return tostring(value) end
        local entries = {}
        for key, child in pairs(value) do
            entries[#entries + 1] = "[" .. encode(key) .. "]=" .. encode(child)
        end
        table.sort(entries)
        return "{" .. table.concat(entries, ",") .. "}"
    end
    local file = assert(io.open(determinant_record_path, "w"))
    assert(file:write("return ", encode({
        swap = nps.walkthrough("det([[0,2],[3,4]])", "x", "exact"),
        fraction = nps.walkthrough("det([[1/2,1/3],[0,1/4]])", "x", "exact"),
    }), "\n"))
    assert(file:close())
end
print("actual Giac Lua matrix: " .. checks .. " checks, 0 failures")
)lua";
    const int status = luaL_dostring(L, script);
    if (status != 0)
        std::cerr << lua_tostring(L, -1) << '\n';
    lua_close(L);
    return status == 0 ? 0 : 1;
}
