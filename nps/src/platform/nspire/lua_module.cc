// The narrow bridge of PRD section 12.2's native-core option: Lua gets the UI, this gets the maths.
//
// The trust boundary of section 12.3 lives here. Lua hands over the user's text and nothing else, so
// every step entry parses it into a validated AST under the arena's limits before anything acts on
// it, and no step entry will carry a Giac command that Lua wrote.
//
// The unified build adds one exception, and it is the shell's rather than the solver's: caseval is
// the channel Ki V1 had through luagiac, and the shell exists to evaluate what the user typed. It
// is marked out below and reaches Giac directly, never the adapter.

#include <algorithm>
#include <array>
#if NPS_RETAINED_UI
#include "nps/ui/retained_menu.h"
#include <optional>
#endif
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <string_view>
#include <unistd.h>
#include <utility>
#include <vector>

#include "nps/core/capability_manifest.h"
#include "nps/core/canonical.h"
#include "nps/core/evaluate.h"
#include "nps/steps/derivation.h"
#include "nps/steps/command.h"
#include "nps/steps/calculus.h"
#include "nps/steps/separable.h"
#include "nps/ui/canvas.h"
#include "nps/ui/bitmap.h"
#include "nps/steps/integer.h"
#include "nps/steps/matrix.h"
#include "nps/steps/rewrite.h"
#include "nps/steps/rearrange.h"
#include "nps/steps/solve_task.h"
#include "nps/steps/differentiate.h"
#include "nps/cas/giac_adapter.h"
#include "nps/steps/integrate.h"
#include "nps/physics/catch_up.h"
#include "nps/physics/density.h"
#include "nps/physics/kinematics.h"
#include "nps/physics/optics.h"
#include "nps/physics/planar_kinematics.h"
#include "nps/physics/relative_motion.h"
#include "nps/physics/unit_conversion.h"
#include "nps/physics/vector_addition.h"
#include "nps/physics/vector_components.h"
#include "nps/physics/vector_cross.h"
#include "nps/physics/forces.h"
#include "nps/physics/work.h"
#include "nps/platform/nspire/device_identity.h"
#include "nps/platform/nspire/integrity.h"
#include "nps/platform/nspire/measurement.h"
#include "nps/platform/nspire/native_menu.h"
#include "nps/steps/linear.h"
#include "nps/steps/quadratic.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"

#include <os.h>

// A real ndl syscall with a stub in libsyscalls, but no declaration anywhere in the sdk headers.
extern "C" unsigned nl_osid();

// Ki V4 links Giac into this module, so -DSTEPCAS_GIAC=1 makes a backend call an ordinary call.
// The define and the include it gates both live out here, not down beside the class that uses them:
// an include inside the anonymous namespace below would nest namespace nps inside it, and the
// names would be there but unreachable.
#ifndef NPS_GIAC
#define NPS_GIAC 0
#endif

#ifndef NPS_RELEASE_MANIFEST
#define NPS_RELEASE_MANIFEST NPS_GIAC
#endif

#ifndef NPS_INTEGRITY_TESTING
#define NPS_INTEGRITY_TESTING 0
#endif

#ifndef NPS_RESOURCE_PROFILE
#define NPS_RESOURCE_PROFILE 0
#endif

#if NPS_RESOURCE_PROFILE
#include "nps/platform/nspire/allocation_probe.h"
#endif

#if NPS_GIAC
#include "nps/cas/giac_typed.h"
#include "luabridge.h"
#endif

using namespace nps;

namespace {

void set_field(lua_State *L, const char *key, const std::string &value);
void set_field(lua_State *L, const char *key, const char *value);
void set_field(lua_State *L, const char *key, bool value);
void push_no_steps(lua_State *L);

IntegrityStatus module_integrity_status = IntegrityStatus::Verified;

// Each build answers for its own artifact: the name a document asks nrequire for, and the package
// basename its own bytes have to match.
#if NPS_GIAC
constexpr const char *kModuleName = "nps_nspire";
constexpr const char *kModulePackageBasename = kUnifiedPackageBasename;
#else
constexpr const char *kModuleName = "nps_split";
constexpr const char *kModulePackageBasename = kSplitPackageBasename;
#endif

// Native solves run synchronously, so their budget polls the keypad matrix directly.
bool escape_pressed(void *) { return isKeyPressed(KEY_NSPIRE_ESC); }

Budget interactive_budget() {
    Budget budget;
    budget.poll = escape_pressed;
    return budget;
}

constexpr bool cross_check_allowed(DerivationStatus status) {
    return status != DerivationStatus::Cancelled &&
           status != DerivationStatus::ResourceLimitReached;
}

constexpr bool cross_check_allowed(SolveOutcome outcome) {
    return outcome != SolveOutcome::Cancelled && outcome != SolveOutcome::ResourceExceeded;
}

constexpr bool cross_check_allowed(DiffOutcome outcome) {
    return outcome != DiffOutcome::Cancelled && outcome != DiffOutcome::ResourceExceeded;
}

constexpr bool cross_check_allowed(IntegrateOutcome outcome) {
    return outcome != IntegrateOutcome::Cancelled &&
           outcome != IntegrateOutcome::ResourceExceeded;
}

static_assert(!cross_check_allowed(SolveOutcome::Cancelled));
static_assert(!cross_check_allowed(SolveOutcome::ResourceExceeded));
static_assert(cross_check_allowed(SolveOutcome::NotLinear));
static_assert(!cross_check_allowed(DiffOutcome::Cancelled));
static_assert(!cross_check_allowed(DiffOutcome::ResourceExceeded));
static_assert(cross_check_allowed(DiffOutcome::UnsupportedForm));
static_assert(!cross_check_allowed(IntegrateOutcome::Cancelled));
static_assert(!cross_check_allowed(IntegrateOutcome::ResourceExceeded));
static_assert(cross_check_allowed(IntegrateOutcome::UnsupportedForm));

// Diagnostic instrumentation (raw memory reads, watchdog pokes, flash tracing) is off unless the
// build says -DNPS_DIAG=1, which is what make DIAG=1 does.
#ifndef NPS_DIAG
#define NPS_DIAG 0
#endif

#if NPS_DIAG
// Diagnostic, matching the marker file the Lua side writes. The Lua trace narrowed the reset to
// somewhere inside this module's second call, which is as far as a marker outside it can see.
// Opened and closed per line so the bytes are on flash before the fault, not in a buffer that dies
// with the process. Removed once the reset is understood.
// Set to 1 to write the marker file. Off by default: this opens, writes and closes a file on flash
// on every call, roughly thirty times per run, from inside a Lua callback and around code that
// masks interrupts. That is a heavy thing to have running while chasing a fault, and heavy enough
// to be a fault of its own, so the fixed code has to be measured without it.
#define NPS_TRACE 0

void trace(const char *what) {
    if (!NPS_TRACE)
        return;
    FILE *f = fopen("/documents/ndl/nps_trace.txt.tns", "a");
    if (!f)
        return;
    fputs("    cc: ", f);
    fputs(what, f);
    fputc('\n', f);
    fclose(f);
}

void trace_mem(lua_State *L, const char *what) {
    char line[128];
    snprintf(line, sizeof(line), "%s: lua %d kb, largest block %u kb", what,
             lua_gc(L, LUA_GCCOUNT, 0), allocator_headroom_kb());
    trace(line);
}
#else
inline void trace(const char *) {}
inline void trace_mem(lua_State *, const char *) {}
#endif

// Giac reached through the module Ki V1 already installs. The command still comes from the adapter,
// so this only carries a string the native side built.
class LuaGiacBackend : public Backend {
  public:
    // The keypad poll the solver budget already uses, lent to the adapter so giac's combined
    // interruption and stack overflow message can be read as the stop the learner asked for.
    explicit LuaGiacBackend(lua_State *L) : L_(L) { watch_stop(escape_pressed, nullptr); }

    // Raw, because getglobal and getfield run __index, and a raise here longjmps past a live arena.
    static bool push_caseval(lua_State *L) {
        lua_pushstring(L, "luagiac");
        lua_rawget(L, LUA_GLOBALSINDEX);
        if (!lua_istable(L, -1)) {
            lua_pop(L, 1);
            return false;
        }
        lua_pushstring(L, "caseval");
        lua_rawget(L, -2);
        if (!lua_isfunction(L, -1)) {
            lua_pop(L, 2);
            return false;
        }
        lua_remove(L, -2);
        return true;
    }

    static bool available(lua_State *L) {
        if (!push_caseval(L))
            return false;
        lua_pop(L, 1);
        return true;
    }

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        const int base = lua_gettop(L_);
        if (!push_caseval(L_)) {
            lua_settop(L_, base);
            *error = "luagiac.caseval is not available";
            return false;
        }
        lua_pushlstring(L_, command.data(), command.size());
        if (lua_pcall(L_, 1, 1, 0) != 0) {
            const char *msg = lua_tostring(L_, -1);
            *error = msg ? msg : "the call into Giac failed";
            lua_settop(L_, base);
            return false;
        }
        // lua_tolstring would quietly turn a number into text, so the type is checked first.
        if (lua_type(L_, -1) != LUA_TSTRING) {
            lua_settop(L_, base);
            *error = "Giac returned something that is not a string";
            return false;
        }
        size_t n = 0;
        const char *s = lua_tolstring(L_, -1, &n);
        out->assign(s, n);
        lua_settop(L_, base);
        return true;
    }

  private:
    lua_State *L_;
};

#if NPS_GIAC
// Giac is in this image, so the adapter hands it objects rather than a command string. The typed
// path lives in giac_typed.cc, which is the only file that includes both this project's headers and
// Giac's. Nothing else changes: both builds ask Giac first and build the Lua table afterwards, so a
// defect in one shows up in the other. This wrapper carries the one thing the header cannot: the
// lua_State constructor and the availability answer the two builds share.
class DirectGiacBackend : public TypedGiacBackend {
  public:
    explicit DirectGiacBackend(lua_State *) { watch_stop(escape_pressed, nullptr); }

    // Giac is linked in, so there is nothing to look for and nothing that can be missing.
    static bool available(lua_State *) { return true; }
};

typedef DirectGiacBackend GiacBackend;
#else
typedef LuaGiacBackend GiacBackend;
#endif

class UnavailableBackend : public Backend {
  public:
    bool eval(const std::string &, std::string *, std::string *error) override {
        *error = "Giac backend is unavailable";
        return false;
    }
};

bool is_literal_zero(const Arena &arena, NodeId id) {
    if (id == kNoNode)
        return false;
    const Node &n = arena.at(id);
    return n.kind == Kind::Integer && n.small_valid && n.small == 0;
}

// Holds the collector still for as long as it is in scope.
//
// These entry points build a nested table over many steps and, in the middle of that, call back into
// the interpreter to reach Giac. Lua's collector is incremental, so it can run inside that re-entrant
// call and again on any allocation afterwards. Measured on the handheld: the steps array reads back
// from C as four sound tables the instant it is built, and Lua faults on the fourth entry moments
// later, only on invocations that went through Giac. That is a live object being collected, and the
// last one added is the one at risk.
//
// Restarting in the destructor matters, because there are several returns and one of them is an
// early exit for a parse failure. Leaving the collector stopped would be a far worse bug than the
// one being fixed.
class GcPause {
  public:
    explicit GcPause(lua_State *L) : L_(L) { lua_gc(L_, LUA_GCSTOP, 0); }
    ~GcPause() { lua_gc(L_, LUA_GCRESTART, 0); }

  private:
    GcPause(const GcPause &);
    GcPause &operator=(const GcPause &);
    lua_State *L_;
};

// What the cross-check found, as plain C++ with nothing on the Lua stack.
struct CrossCheck {
    std::string tag = "unavailable";
    ResultForm form = ResultForm::NoResult;
    std::string raw;
    std::string detail;
    std::string comparison_tag;
    ResultForm comparison_form = ResultForm::NoResult;
    std::string comparison_raw;
    std::string comparison_detail;
    std::string value;
    bool has_answer = false;
    bool comparison_attempted = false;
    bool compared = false;
    bool agrees = false;
    bool cancelled = false;
    size_t calls = 0;
};

// Whether Giac's answer may be shown when ours withheld one. Both statuses mean the engine declined
// to offer a result for a form it has no rule for, which is the case section 12.1 segregates rather
// than suppresses. Partially solved is the same refusal that kept the part it did check, so a
// student who gets further is the last one who should lose the answer: they see their prefix and the
// segregated answer, not one or the other.
constexpr bool answer_only_allowed(DerivationStatus status) {
    return status == DerivationStatus::Unsupported ||
           status == DerivationStatus::PartiallySolved;
}

enum class IntegrateCrossCheckRoute : uint8_t { None, Differentiate, Integrate };

constexpr IntegrateCrossCheckRoute integrate_cross_check_route(bool cross,
                                                               IntegrateOutcome outcome,
                                                               NodeId particular,
                                                               DerivationStatus status) {
    if (!cross || !cross_check_allowed(outcome))
        return IntegrateCrossCheckRoute::None;
    if (particular != kNoNode)
        return IntegrateCrossCheckRoute::Differentiate;
    if (answer_only_allowed(status))
        return IntegrateCrossCheckRoute::Integrate;
    return IntegrateCrossCheckRoute::None;
}

// MATH-015 for the answer the viewer shows, which is the backend's whenever we withheld our own.
constexpr ResultForm primary_result_form(bool has_local_result, bool answer_only,
                                         DerivationStatus status, ResultForm backend_form) {
    if (answer_only)
        return backend_form;
    if (has_local_result)
        return ResultForm::ElementaryClosedForm;
    if (backend_form != ResultForm::NoResult)
        return backend_form;
    return status == DerivationStatus::Unsupported ? ResultForm::UnsupportedSymbolicForm
                                                   : ResultForm::NoResult;
}

// What a solve reached, whichever family answered it, so one table-building path serves both.
struct Attempt {
    const char *outcome = "refused";
    std::string detail;
    std::string answer;
    NodeId comparable = kNoNode;
    std::vector<NodeId> solutions;
    bool finite_solutions = false;
    DerivationStatus status = DerivationStatus::NotRecorded;
    bool solved = false;
    // Whether the rule reached a stated answer about the solution set, which an empty set and every
    // value both are and neither of which has a value to print. Without it the viewer reads a
    // missing result as no answer and puts NO RESULT over a derivation it also calls verified.
    bool has_result = false;
    bool ask_backend = false;
    Cost cost;
};

// Whether the square-root rule answered the equation rather than handing it back. Its two ways of
// handing one back are the only outcomes the linear rule gets a turn after.
constexpr bool quadratic_family(QuadraticOutcome outcome) {
    return outcome != QuadraticOutcome::NotPureQuadratic &&
           outcome != QuadraticOutcome::NotAnEquation;
}

static_assert(!quadratic_family(QuadraticOutcome::NotPureQuadratic));
static_assert(!quadratic_family(QuadraticOutcome::NotAnEquation));
static_assert(quadratic_family(QuadraticOutcome::OutsideEnvelope));
static_assert(quadratic_family(QuadraticOutcome::NoRealSolution));

void add_cost(Cost *into, const Cost &extra) {
    into->rewrites += extra.rewrites;
    into->steps += extra.steps;
    into->branches += extra.branches;
    into->backend_calls += extra.backend_calls;
    into->states += extra.states;
}

Attempt linear_attempt(const Arena &arena, const SolveResult &r) {
    Attempt a;
    a.outcome = solve_outcome_name(r.outcome);
    a.detail = r.detail;
    if (r.solution != kNoNode)
        a.answer = print(arena, r.solution);
    a.comparable = r.solution;
    a.status = r.status;
    a.solved = r.outcome == SolveOutcome::Solved;
    a.has_result = a.solved || r.outcome == SolveOutcome::NoSolution ||
                   r.outcome == SolveOutcome::AllValues;
    a.ask_backend = cross_check_allowed(r.outcome);
    a.cost = r.cost;
    return a;
}

Attempt quadratic_attempt(const Arena &arena, const QuadraticResult &r) {
    Attempt a;
    a.outcome = quadratic_outcome_name(r.outcome);
    a.detail = r.detail;
    // Every root rather than the first, because an answer showing one of two agrees with the engine
    // that dropped the other.
    for (size_t i = 0; i < r.solutions.size(); ++i) {
        if (!a.answer.empty())
            a.answer += " and ";
        a.answer += print(arena, r.solutions[i]);
    }
    a.solutions = r.solutions;
    a.finite_solutions = r.outcome == QuadraticOutcome::Solved ||
                         r.outcome == QuadraticOutcome::NoRealSolution;
    a.status = r.status;
    a.solved = r.outcome == QuadraticOutcome::Solved;
    a.has_result = a.solved || r.outcome == QuadraticOutcome::NoRealSolution;
    a.ask_backend = r.outcome != QuadraticOutcome::Cancelled &&
                    r.outcome != QuadraticOutcome::ResourceExceeded;
    a.cost = r.cost;
    return a;
}

// Giac supplies an independent answer. Finite rational sets are compared without sampling.
//
// This asks Giac and returns what it said. It deliberately writes nothing into a Lua table, because
// reaching Giac means lua_pcall back into the interpreter, and this C function is itself running as
// a Lua call. Building the result table across that re-entry left our half-filled table on the
// stack while another Lua call ran on top of it, and the calculator reset on the run after, at a
// different statement in every build. Whatever the exact mechanism, holding a partly built Lua
// value across a re-entrant pcall is not something to be clever about: the fix is to have nothing
// of ours on the stack while Giac runs, and to build the table afterwards from these fields.
bool rational_roots(const Arena &arena, const std::vector<NodeId> &roots,
                    std::vector<Rational> *values, CrossCheck *check) {
    if (escape_pressed(nullptr)) {
        check->cancelled = true;
        return false;
    }
    for (NodeId root : roots) {
        if (escape_pressed(nullptr)) {
            check->cancelled = true;
            return false;
        }
        Rational value;
        if (!evaluate_rational(arena, root, {}, &value))
            return false;
        values->push_back(value);
    }
    std::sort(values->begin(), values->end(), [](const Rational &left, const Rational &right) {
        return left.num != right.num ? left.num < right.num : left.den < right.den;
    });
    values->erase(std::unique(values->begin(), values->end(), rational_equal), values->end());
    return true;
}

void compare_root_sets(const Arena &arena, const std::vector<NodeId> &ours, const Response &reply,
                       CrossCheck *check) {
    check->comparison_attempted = true;
    if (reply.shape != ResultShape::FiniteSolutions || reply.tag != ResultTag::Exact) {
        check->comparison_tag = tag_name(reply.tag);
        check->comparison_form = result_form(reply);
        check->comparison_detail = "the backend did not return an exact finite solution set";
        return;
    }
    std::vector<Rational> native_roots, backend_roots;
    if (!rational_roots(arena, ours, &native_roots, check) ||
        !rational_roots(arena, reply.values, &backend_roots, check)) {
        check->comparison_tag = "unsupported operation";
        check->comparison_form = ResultForm::UnsupportedSymbolicForm;
        check->comparison_detail = check->cancelled
                                       ? "the finite solution set comparison was cancelled"
                                       : "a root is outside the exact rational comparison envelope";
        return;
    }
    check->comparison_tag = "exact";
    check->comparison_form = ResultForm::ElementaryClosedForm;
    check->compared = true;
    check->agrees = native_roots.size() == backend_roots.size() &&
                    std::equal(native_roots.begin(), native_roots.end(), backend_roots.begin(),
                               rational_equal);
    check->comparison_detail = check->agrees
                                   ? "the complete finite rational solution sets agree"
                                   : "the complete finite rational solution sets differ";
}

// engine is the backend the solver was handed, where there was one. Building a fresh one here gives
// the request a second Giac with no memory of the first giving up, and a backend is not called again
// after it reports a terminal resource or cancellation status.
CrossCheck ask_giac(lua_State *L, Arena &arena, DerivationStatus status, Op op, NodeId target,
                    NodeId variable, NodeId ours, bool scalar_only = true,
                    const std::vector<NodeId> *our_solutions = nullptr, Backend *engine = nullptr) {
    CrossCheck out;
    if (!cross_check_allowed(status) || !GiacBackend::available(L))
        return out;

    GiacBackend fresh(L);
    Backend &backend = engine ? *engine : static_cast<Backend &>(fresh);
    Adapter adapter(arena, backend);

    Request req;
    req.op = op;
    req.target = target;
    req.variable = variable;
    Response r = adapter.run(req);

    out.tag = tag_name(r.tag);
    out.form = result_form(r);
    out.raw = r.raw;
    out.detail = r.detail;
    out.calls = adapter.call_count();
    // A backend that stopped because the learner asked it to reaches the same field the escape
    // check writes, so where the stop was noticed does not decide what it is called.
    if (r.tag == ResultTag::Cancelled)
        out.cancelled = true;
    if (!r.usable())
        return out;
    const NodeId single = r.single_value();
    if (single != kNoNode) {
        out.value = print(arena, single);
    } else if (!scalar_only && r.shape == ResultShape::FiniteSolutions) {
        out.value = "[";
        for (NodeId root : r.values) {
            if (out.value.size() > 1)
                out.value += ", ";
            out.value += print(arena, root);
        }
        out.value += "]";
    } else {
        out.detail = "this operation requires exactly one backend solution";
        return out;
    }
    out.has_answer = true;
    if (our_solutions) {
        compare_root_sets(arena, *our_solutions, r, &out);
        return out;
    }
    if (ours == kNoNode)
        return out;

    if (single == kNoNode) {
        compare_root_sets(arena, {ours}, r, &out);
        return out;
    }
    NodeId negated = arena.binary(Kind::Mul, arena.integer("-1"), single);
    Request zero;
    zero.op = Op::IsZero;
    zero.target = arena.binary(Kind::Add, ours, negated);
    Response z = adapter.run(zero);
    out.calls = adapter.call_count();
    if (z.tag == ResultTag::Cancelled)
        out.cancelled = true;
    out.comparison_attempted = true;
    out.comparison_tag = tag_name(z.tag);
    out.comparison_form = result_form(z);
    out.comparison_raw = z.raw;
    out.comparison_detail = z.detail;
    if (r.tag == ResultTag::Exact && z.tag == ResultTag::Exact && z.value != kNoNode) {
        out.compared = true;
        out.agrees = is_literal_zero(arena, z.value);
    }
    return out;
}

bool dependency_failure_tag(const std::string &tag) {
    return tag == "unavailable" || tag == "backend error";
}

// A cancellation leads, because the learner asking to stop is the one terminal condition that is
// not a statement about the backend. It is read first because PRD PERF-009 forbids a cancel that
// leaves a mislabeled derivation, and AGENTS.md keeps cancellation distinct: a stop nobody recorded
// reads as a check that merely could not run, and the two are different facts.
DerivationStatus cross_checked_status(DerivationStatus local, const CrossCheck &check) {
    if (check.cancelled)
        return DerivationStatus::Cancelled;
    if (dependency_failure_tag(check.tag) ||
        (check.comparison_attempted && dependency_failure_tag(check.comparison_tag)))
        return DerivationStatus::DependencyUnavailable;
    if (check.compared)
        return check.agrees ? local : DerivationStatus::VerificationFailed;
    // An inconclusive backend comparison does not confirm the local derivation. A timeout or a
    // resource refusal is one of those rather than a stronger case, because our answer was reached
    // and kept before the check was asked, which is the reading calculus.cc already takes.
    return DerivationStatus::SolvedButUnchecked;
}

// The other half, run once the table exists and Giac is finished with.
void set_cross_check(lua_State *L, const CrossCheck &c, bool include_answer) {
    set_field(L, "giac_tag", c.tag);
    set_field(L, "giac_form", result_form_name(c.form));
    set_field(L, "giac_raw", c.raw);
    if (!c.detail.empty())
        set_field(L, "giac_detail", c.detail);
    if (c.comparison_attempted) {
        set_field(L, "giac_compare_tag", c.comparison_tag);
        set_field(L, "giac_compare_form", result_form_name(c.comparison_form));
        set_field(L, "giac_compare_raw", c.comparison_raw);
        if (!c.comparison_detail.empty())
            set_field(L, "giac_compare_detail", c.comparison_detail);
    }
    if (include_answer && c.has_answer)
        set_field(L, "giac", c.value);
    if (c.compared)
        set_field(L, "agrees", c.agrees);
}

void set_field(lua_State *L, const char *key, const std::string &value) {
    lua_pushstring(L, key);
    lua_pushlstring(L, value.data(), value.size());
    lua_settable(L, -3);
}

// Without this a string literal converts to bool and lands as true, silently.
void set_field(lua_State *L, const char *key, const char *value) {
    set_field(L, key, std::string(value));
}

void set_field(lua_State *L, const char *key, bool value) {
    lua_pushstring(L, key);
    lua_pushboolean(L, value);
    lua_settable(L, -3);
}

void set_field(lua_State *L, const char *key, int value) {
    lua_pushstring(L, key);
    lua_pushinteger(L, value);
    lua_settable(L, -3);
}

int absolute_index(lua_State *L, int index) {
    return index > 0 ? index : lua_gettop(L) + index + 1;
}

constexpr lua_Number kLuaNumberAbiValue = 13.25;
constexpr lua_Number kLuaNumberAbiDefault = -7.5;

constexpr bool lua_number_abi_matches(lua_Number converted, lua_Number checked,
                                      lua_Number optional, lua_Number defaulted) {
    return converted == kLuaNumberAbiValue && checked == kLuaNumberAbiValue &&
           optional == kLuaNumberAbiValue && defaulted == kLuaNumberAbiDefault;
}

bool lua_number_abi_works(lua_State *L) {
    const int top = lua_gettop(L);
    if (!lua_checkstack(L, 1))
        return false;

    lua_pushnumber(L, kLuaNumberAbiValue);
    if (lua_gettop(L) != top + 1 || lua_type(L, -1) != LUA_TNUMBER) {
        lua_settop(L, top);
        return false;
    }

    const lua_Number converted = lua_tonumber(L, -1);
    const lua_Number checked = luaL_checknumber(L, -1);
    const lua_Number optional = luaL_optnumber(L, -1, kLuaNumberAbiDefault);
    const lua_Number defaulted = luaL_optnumber(L, top + 2, kLuaNumberAbiDefault);
    lua_settop(L, top);
    return lua_number_abi_matches(converted, checked, optional, defaulted);
}

int read_field(lua_State *L) {
    lua_gettable(L, 1);
    return 1;
}

bool field_value(lua_State *L, int table_index, const char *key, std::string *why) {
    const int table = absolute_index(L, table_index);
    lua_pushcfunction(L, read_field);
    lua_pushvalue(L, table);
    lua_pushstring(L, key);
    // Contain inherited-field errors before they can skip native request destructors.
    if (lua_pcall(L, 2, 1, 0) == 0)
        return true;
    const char *error = lua_type(L, -1) == LUA_TSTRING ? lua_tostring(L, -1) : nullptr;
    *why = std::string(key) + " lookup failed" + (error ? ": " + std::string(error) : "");
    lua_pop(L, 1);
    return false;
}

bool string_field(lua_State *L, int table_index, const char *key, std::string *value,
                  std::string *why) {
    if (!field_value(L, table_index, key, why))
        return false;
    if (lua_type(L, -1) != LUA_TSTRING) {
        *why = std::string(key) + " must be a string";
        lua_pop(L, 1);
        return false;
    }
    size_t size = 0;
    const char *text = lua_tolstring(L, -1, &size);
    if (size > Limits().max_input_bytes) {
        *why = std::string(key) + " is too long";
        lua_pop(L, 1);
        return false;
    }
    value->assign(text, size);
    lua_pop(L, 1);
    return true;
}

bool integer_field(lua_State *L, int table_index, const char *key, int *value,
                   std::string *why) {
    const int table = absolute_index(L, table_index);
    if (!field_value(L, table, key, why))
        return false;
    if (lua_type(L, -1) != LUA_TNUMBER) {
        *why = std::string(key) + " must be an integer";
        lua_pop(L, 1);
        return false;
    }
    const lua_Number number = lua_tonumber(L, -1);
    if (number != number || number < 0 ||
        number > static_cast<lua_Number>(std::numeric_limits<uint16_t>::max())) {
        *why = std::string(key) + " is outside the supported range";
        lua_pop(L, 1);
        return false;
    }
    const lua_Integer integer = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (static_cast<lua_Number>(integer) != number) {
        *why = std::string(key) + " must be an integer";
        return false;
    }
    *value = static_cast<int>(integer);
    return true;
}

bool precision_field(lua_State *L, int table_index, Precision *precision, std::string *why) {
    const int table = absolute_index(L, table_index);
    if (!field_value(L, table, "precision", why))
        return false;
    if (lua_type(L, -1) != LUA_TTABLE) {
        *why = "precision must be a table";
        lua_pop(L, 1);
        return false;
    }
    const int precision_table = lua_gettop(L);
    std::string kind;
    int digits = 0;
    if (!string_field(L, precision_table, "kind", &kind, why) ||
        !integer_field(L, precision_table, "significant_digits", &digits, why)) {
        lua_pop(L, 1);
        return false;
    }
    lua_pop(L, 1);

    Precision parsed;
    if (kind == "exact") {
        if (digits != 0) {
            *why = "exact precision requires zero significant_digits";
            return false;
        }
        parsed.kind = NumberKind::Exact;
    } else if (kind == "measured") {
        if (digits == 0) {
            *why = "measured precision requires significant_digits";
            return false;
        }
        parsed.kind = NumberKind::Measured;
        parsed.significant_digits = static_cast<uint16_t>(digits);
    } else {
        *why = "precision kind must be exact or measured";
        return false;
    }
    *precision = parsed;
    return true;
}

bool vector_metadata(lua_State *L, int table_index, uint8_t *rank, Frame *frame, Unit *unit,
                     Precision *precision, std::string *why) {
    int parsed_rank = 0;
    if (!integer_field(L, table_index, "rank", &parsed_rank, why))
        return false;
    if (parsed_rank != 2 && parsed_rank != 3) {
        *why = "rank must be 2 or 3";
        return false;
    }
    if (!string_field(L, table_index, "frame", &frame->name, why))
        return false;
    if (frame->name.empty()) {
        *why = "frame must not be empty";
        return false;
    }
    std::string unit_text;
    if (!string_field(L, table_index, "unit", &unit_text, why) ||
        !parse_unit(unit_text, unit, why))
        return false;
    if (!precision_field(L, table_index, precision, why))
        return false;
    *rank = static_cast<uint8_t>(parsed_rank);
    return true;
}

bool rational_field(lua_State *L, int table_index, const char *key, Rational *value,
                    Precision *precision, std::string *why) {
    std::string text;
    if (!string_field(L, table_index, key, &text, why))
        return false;
    Quantity parsed;
    std::string parse_error;
    if (!parse_quantity(text, &parsed, &parse_error) || !parsed.unit.text.empty()) {
        *why = std::string(key) + " must be an exact integer or decimal string";
        return false;
    }
    *value = parsed.value;
    *precision = parsed.precision;
    return true;
}

bool quantity_field(lua_State *L, int table_index, const char *key, Quantity *value,
                    std::string *why) {
    std::string text;
    if (!string_field(L, table_index, key, &text, why))
        return false;
    std::string parse_error;
    if (!parse_quantity(text, value, &parse_error)) {
        *why = std::string(key) + ": " + parse_error;
        return false;
    }
    return true;
}

bool catch_up_body_table(lua_State *L, int table_index, CatchUpBody *body,
                         std::string *why) {
    if (lua_type(L, table_index) != LUA_TTABLE) {
        *why = "body must be a table";
        return false;
    }
    const int table = absolute_index(L, table_index);
    CatchUpBody parsed;
    std::string motion;
    if (!string_field(L, table, "name", &parsed.name, why) ||
        !string_field(L, table, "frame", &parsed.frame.name, why) ||
        !quantity_field(L, table, "position", &parsed.position_at_start, why) ||
        !quantity_field(L, table, "velocity", &parsed.velocity_at_start, why) ||
        !quantity_field(L, table, "start_time", &parsed.start_time, why) ||
        !string_field(L, table, "motion", &motion, why)) {
        return false;
    }

    if (motion == "constant_velocity") {
        parsed.motion = CatchUpMotionModel::ConstantVelocity;
        if (!field_value(L, table, "acceleration", why))
            return false;
        const bool has_acceleration = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (has_acceleration) {
            *why = "acceleration is only valid for constant_acceleration motion";
            return false;
        }
    } else if (motion == "constant_acceleration") {
        parsed.motion = CatchUpMotionModel::ConstantAcceleration;
        if (!quantity_field(L, table, "acceleration", &parsed.acceleration, why))
            return false;
    } else {
        *why = "motion must be constant_velocity or constant_acceleration";
        return false;
    }
    *body = std::move(parsed);
    return true;
}

bool vector_table(lua_State *L, int table_index, Vector *vector, std::string *why) {
    if (lua_type(L, table_index) != LUA_TTABLE) {
        *why = "vector must be a table";
        return false;
    }
    const int table = absolute_index(L, table_index);
    Vector parsed;
    Precision x_precision;
    Precision y_precision;
    if (!vector_metadata(L, table, &parsed.rank, &parsed.frame, &parsed.unit, &parsed.precision,
                          why) ||
        !rational_field(L, table, "x", &parsed.x, &x_precision, why) ||
        !rational_field(L, table, "y", &parsed.y, &y_precision, why))
        return false;
    Precision component_precision = precision_combine(x_precision, y_precision);
    if (parsed.rank == 3) {
        Precision z_precision;
        if (!rational_field(L, table, "z", &parsed.z, &z_precision, why))
            return false;
        component_precision = precision_combine(component_precision, z_precision);
    } else {
        if (!field_value(L, table, "z", why))
            return false;
        const bool has_z = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (has_z) {
            *why = "rank 2 vectors must not contain z";
            return false;
        }
    }
    if (parsed.precision.kind == NumberKind::Measured &&
        component_precision.kind == NumberKind::Measured) {
        parsed.precision.last_significant_decimal_place =
            component_precision.last_significant_decimal_place;
    }
    *vector = std::move(parsed);
    return true;
}

bool parsed_expression(Arena &arena, const char *key, const std::string &text, NodeId *value,
                       std::string *why) {
    const ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        char offset[24];
        snprintf(offset, sizeof(offset), "%u", static_cast<unsigned>(parsed.offset + 1));
        *why = std::string(key) + " is " + status_name(parsed.status) + " at character " + offset;
        return false;
    }
    *value = parsed.root;
    return true;
}

bool expression_field(lua_State *L, int table_index, const char *key, Arena &arena, NodeId *value,
                      std::string *why) {
    std::string text;
    if (!string_field(L, table_index, key, &text, why))
        return false;
    return parsed_expression(arena, key, text, value, why);
}

// A planar vector leaves z empty and rejects an explicitly supplied z field.
bool vector_expression_text(lua_State *L, int table_index, VectorExpr *vector, std::string *x,
                            std::string *y, std::string *z, std::string *why) {
    if (lua_type(L, table_index) != LUA_TTABLE) {
        *why = "vector must be a table";
        return false;
    }
    const int table = absolute_index(L, table_index);
    if (!vector_metadata(L, table, &vector->rank, &vector->frame, &vector->unit, &vector->precision,
                         why))
        return false;
    if (!string_field(L, table, "x", x, why) || !string_field(L, table, "y", y, why))
        return false;
    if (vector->rank == 3)
        return string_field(L, table, "z", z, why);
    if (!field_value(L, table, "z", why))
        return false;
    const bool has_z = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (has_z) {
        *why = "rank 2 vectors must not contain z";
        return false;
    }
    z->clear();
    return true;
}

// The other half, which touches the Arena and never Lua.
bool vector_expression_parse(Arena &arena, const std::string &x, const std::string &y,
                            const std::string &z, VectorExpr *vector, Vector *exact,
                            bool *has_exact, std::string *why) {
    const ParseResult parsed_x = parse(arena, x);
    const ParseResult parsed_y = parse(arena, y);
    if (!parsed_x.ok() || !parsed_y.ok()) {
        const ParseResult &failed = parsed_x.ok() ? parsed_y : parsed_x;
        const char *field = parsed_x.ok() ? "y" : "x";
        char offset[24];
        snprintf(offset, sizeof(offset), "%u", static_cast<unsigned>(failed.offset + 1));
        *why = std::string(field) + " is " + status_name(failed.status) + " at character " + offset;
        return false;
    }
    vector->x = parsed_x.root;
    vector->y = parsed_y.root;

    Vector rational;
    rational.rank = vector->rank;
    rational.frame = vector->frame;
    rational.unit = vector->unit;
    rational.precision = vector->precision;
    bool rational_components = rational_from_text(x, &rational.x) &&
                               rational_from_text(y, &rational.y);
    if (vector->rank == 3) {
        const ParseResult parsed_z = parse(arena, z);
        if (!parsed_z.ok()) {
            char offset[24];
            snprintf(offset, sizeof(offset), "%u", static_cast<unsigned>(parsed_z.offset + 1));
            *why = std::string("z is ") + status_name(parsed_z.status) + " at character " + offset;
            return false;
        }
        vector->z = parsed_z.root;
        rational_components = rational_components && rational_from_text(z, &rational.z);
    } else {
        vector->z = arena.integer("0");
    }
    *exact = std::move(rational);
    *has_exact = rational_components;
    return true;
}

bool angle_unit_field(lua_State *L, int table_index, AngleUnit *unit, std::string *why) {
    std::string text;
    if (!string_field(L, table_index, "angle_unit", &text, why))
        return false;
    if (text == "radians")
        *unit = AngleUnit::Radians;
    else if (text == "degrees")
        *unit = AngleUnit::Degrees;
    else {
        *why = "angle_unit must be degrees or radians";
        return false;
    }
    return true;
}

// Validate table fields before allocating expression nodes.
bool magnitude_angle_text(lua_State *L, int table_index, MagnitudeAngleExpr *value,
                          std::string *magnitude, std::string *angle, std::string *why) {
    if (lua_type(L, table_index) != LUA_TTABLE) {
        *why = "magnitude-angle input must be a table";
        return false;
    }
    const int table = absolute_index(L, table_index);
    uint8_t rank = 0;
    if (!vector_metadata(L, table, &rank, &value->frame, &value->unit, &value->precision, why))
        return false;
    if (rank != 2) {
        *why = "magnitude-angle conversion requires rank 2";
        return false;
    }
    return string_field(L, table, "magnitude", magnitude, why) &&
           string_field(L, table, "angle", angle, why) &&
           angle_unit_field(L, table, &value->angle_unit, why);
}

bool magnitude_angle_parse(Arena &arena, const std::string &magnitude, const std::string &angle,
                          MagnitudeAngleExpr *value, std::string *why) {
    return parsed_expression(arena, "magnitude", magnitude, &value->magnitude, why) &&
           parsed_expression(arena, "angle", angle, &value->angle, why);
}

const char *angle_unit_name(AngleUnit unit) {
    return unit == AngleUnit::Degrees ? "degrees" : "radians";
}

void set_precision(lua_State *L, const Precision &precision) {
    lua_pushstring(L, "precision");
    lua_newtable(L);
    set_field(L, "kind", precision.kind == NumberKind::Measured ? "measured" : "exact");
    set_field(L, "significant_digits", static_cast<int>(precision.significant_digits));
    set_field(L, "last_significant_decimal_place",
              static_cast<int>(precision.last_significant_decimal_place));
    lua_settable(L, -3);
}

void set_quantity_result(lua_State *L, const char *key, const Quantity &quantity,
                         const std::string &value, const std::string &unit) {
    lua_pushstring(L, key);
    lua_newtable(L);
    set_field(L, "value", value);
    set_field(L, "exact_value", rational_text(quantity.value));
    set_field(L, "unit", unit);
    set_precision(L, quantity.precision);
    lua_settable(L, -3);
}

int typed_failure(lua_State *L, const char *outcome, const char *status, const std::string &detail) {
    lua_newtable(L);
    set_field(L, "outcome", outcome);
    set_field(L, "detail", detail);
    set_field(L, "solved", false);
    set_field(L, "has_result", false);
    set_field(L, "answer_only", false);
    set_field(L, "status", status);
    set_field(L, "result_form", result_form_name(ResultForm::NoResult));
    set_field(L, "nodes", 0);
    set_field(L, "step_count", 0);
    set_field(L, "rewrites", 0);
    set_field(L, "giac_calls", 0);
    push_no_steps(L);
    return 1;
}

#if NPS_RELEASE_MANIFEST
int l_capability_manifest(lua_State *L) {
    if (!lua_checkstack(L, 8))
        return luaL_error(L, "not enough Lua stack for the capability manifest");

    const CapabilityManifest manifest = capability_manifest();
    lua_newtable(L);
    set_field(L, "id", manifest.id);
    set_field(L, "artifact", manifest.artifact);
    set_field(L, "schema_version", static_cast<int>(manifest.schema_version));
    set_field(L, "stepcas_version", manifest.stepcas_version);

    lua_pushstring(L, "supported_targets");
    lua_newtable(L);
    for (size_t i = 0; i < manifest.supported_target_count; ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
        lua_newtable(L);
        set_field(L, "calculator_model", manifest.supported_targets[i].calculator_model);
        set_field(L, "os_version", manifest.supported_targets[i].os_version);
        set_field(L, "ndl_version", manifest.supported_targets[i].ndl_version);
        lua_settable(L, -3);
    }
    lua_settable(L, -3);

    const bool integrity_failed = module_integrity_status != IntegrityStatus::Verified;
    lua_pushstring(L, "symbolic_backend");
    lua_newtable(L);
    set_field(L, "name", manifest.symbolic_backend.name);
    set_field(L, "version", manifest.symbolic_backend.version);
    // No availability key: SymbolicBackendCapability has no such field and integrity_status already
    // answers that question, so an invented one would have read false in every state.
    if (integrity_failed) {
        set_field(L, "interface_id", "unavailable");
        set_field(L, "deployment", "integrity-rejected");
    } else {
        set_field(L, "interface_id", manifest.symbolic_backend.interface_id);
        set_field(L, "deployment", manifest.symbolic_backend.deployment);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "installed_modules");
    lua_newtable(L);
    const size_t installed_module_count = integrity_failed ? 0 : manifest.installed_module_count;
    for (size_t i = 0; i < installed_module_count; ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
        lua_newtable(L);
        set_field(L, "kind", manifest.installed_modules[i].kind);
        set_field(L, "id", manifest.installed_modules[i].id);
        lua_settable(L, -3);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "schema_versions");
    lua_newtable(L);
    for (size_t i = 0; i < manifest.schema_version_count; ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
        lua_newtable(L);
        set_field(L, "id", manifest.schema_versions[i].id);
        set_field(L, "version", static_cast<int>(manifest.schema_versions[i].version));
        lua_settable(L, -3);
    }
    lua_settable(L, -3);

    lua_pushstring(L, "integrity_identifiers");
    lua_newtable(L);
    for (size_t i = 0; i < manifest.integrity_identifier_count; ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
        lua_newtable(L);
        set_field(L, "component", manifest.integrity_identifiers[i].component);
        set_field(L, "scheme", manifest.integrity_identifiers[i].scheme);
        set_field(L, "value", manifest.integrity_identifiers[i].value);
        lua_settable(L, -3);
    }
    lua_settable(L, -3);
    return 1;
}
#endif

int l_integrity_status(lua_State *L) {
    lua_pushstring(L, integrity_status_name(module_integrity_status));
    return 1;
}

// Operation-local arena occupancy at return is its peak because the arena only grows.
void set_cost(lua_State *L, const Arena &arena, const Derivation &d, const Cost &cost,
              size_t giac_calls) {
    set_field(L, "nodes", static_cast<int>(arena.node_count()));
    set_field(L, "child_slots", static_cast<int>(arena.child_slot_count()));
    set_field(L, "step_count", static_cast<int>(d.size()));
    set_field(L, "rewrites", static_cast<int>(cost.rewrites));
    set_field(L, "giac_calls", static_cast<int>(giac_calls));
}

std::string joined(const std::vector<std::string> &parts) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out += "; ";
        out += parts[i];
    }
    return out;
}

// Name a record by its rule, or a plan by its strategy, so no row renders blank.
std::string step_label(const Derivation &d, StepId id) {
    const Step &s = d.at(id);
    if (!s.rule_name.empty())
        return s.rule_name;
    const PlanPayload *p = d.plan(id);
    if (p && !p->selected_strategy.empty())
        return p->selected_strategy;
    return step_kind_name(s.kind);
}

// The record is a tree and the UI is a list, so flatten it here and carry the depth along.
//
// A transformation carries the expression it started from and the one it produced, which is the
// thing a reader actually follows down the page. Without them a step names a rule and states a goal
// and never shows the algebra, so the record explains a derivation the screen never displays.
//
// The stack is checked before every level. Lua guarantees a C function only LUA_MINSTACK free slots,
// this holds two per level of nesting for the whole recursion, and overflowing without asking is not
// an error the API reports: it writes past the stack. A deep derivation would corrupt whatever is
// above it, which is not a fault that shows up where it was caused.
bool push_step(lua_State *L, const Arena &arena, const Derivation &d, StepId id, int depth,
               int *row) {
    const Step &s = d.at(id);

    // Two for the row and its table, four for a set_field pair with room to spare.
    if (!lua_checkstack(L, 6))
        return false;

    lua_pushinteger(L, ++(*row));
    lua_newtable(L);
#if NPS_DIAG
    {
        // Diagnostic: Lua faults reading the fourth entry of this array while the first three are
        // sound, so this reports what each row is actually given. A length that is absurd names a
        // string built from a pointer that no longer points where it did.
        const std::string label = step_label(d, id);
        char line[160];
        snprintf(line, sizeof(line), "push_step row %d depth %d kind %s label %u goal %u",
                 *row, depth, step_kind_name(s.kind), (unsigned)label.size(),
                 (unsigned)s.goal.size());
        trace(line);
    }
#endif
    set_field(L, "kind", step_kind_name(s.kind));
    set_field(L, "phase", s.phase);
    set_field(L, "name", step_label(d, id));
    set_field(L, "goal", s.goal);
    set_field(L, "short", s.explanation_short);
    set_field(L, "claim", claim_type_name(s.claim));
    set_field(L, "verified", s.verified());
    set_field(L, "failed", s.has_failed_verification());
    set_field(L, "depth", depth);
    // The fuller explanation, the rule's restrictions and what checked it, for a step view that
    // shows one step at a time and has room to say more than the list does.
    if (!s.explanation_detailed.empty())
        set_field(L, "detail", s.explanation_detailed);
    if (!s.rule_id.empty())
        set_field(L, "rule", s.rule_id);
    if (!s.domain_restrictions.empty())
        set_field(L, "domain", joined(s.domain_restrictions));
    if (!s.assumptions_before.empty())
        set_field(L, "assumes", joined(s.assumptions_before));
    if (!s.verifications.empty()) {
        std::string checks;
        for (size_t i = 0; i < s.verifications.size(); ++i) {
            const VerificationRecord &v = s.verifications[i];
            if (i)
                checks += "; ";
            checks += v.method;
            checks += ": ";
            checks += verification_outcome_name(v.outcome);
            if (!v.detail.empty()) {
                checks += ", ";
                checks += v.detail;
            }
        }
        set_field(L, "checks", checks);
    }

    const TransformationPayload *t = d.transformation(id);
    if (t) {
#if NPS_DIAG
        // Diagnostic: these two NodeIds are the only values here that index the arena, and the arena
        // grew during the cross-check when Giac's reply was parsed into it. An id past the end would
        // read whatever is there.
        char line[160];
        snprintf(line, sizeof(line), "  row %d before %u after %u of %u nodes", *row,
                 (unsigned)t->before, (unsigned)t->after, (unsigned)arena.node_count());
        trace(line);
#endif
        if (t->before != kNoNode)
            set_field(L, "before", print(arena, t->before));
        if (t->after != kNoNode)
            set_field(L, "after", print(arena, t->after));
        if (!t->concrete_action.empty())
            set_field(L, "action", t->concrete_action);
    }
    // A check has no before and after, so what it did and what it saw take their place.
    const CheckPayload *c = d.check(id);
    if (c) {
        if (!c->expected_relation.empty())
            set_field(L, "before", c->expected_relation);
        if (!c->observed_result.empty())
            set_field(L, "after", c->observed_result);
        if (!c->check_method.empty())
            set_field(L, "action", c->check_method);
    }
    // A case has no before and after either. Three of the eight fields go over: the condition, how
    // the case ended and what settled it. The other five are the auditor's half of STEP-024, which
    // the invariant pass already gates and a beginner has no question for.
    const BranchPayload *b = d.branch(id);
    if (b) {
        if (b->condition != kNoNode)
            set_field(L, "case", print(arena, b->condition));
        set_field(L, "resolution", branch_resolution_name(b->resolution));
        if (!b->resolution_evidence.empty())
            set_field(L, "settled_by", b->resolution_evidence);
    }
    lua_settable(L, -3);

    for (size_t i = 0; i < s.children.size(); ++i) {
        if (!push_step(L, arena, d, s.children[i], depth + 1, row))
            return false;
    }
    return true;
}

// Every returning path gives steps a table, so an empty walkthrough never reads as a missing field.
void push_empty_steps(lua_State *L) {
    lua_pushstring(L, "steps");
    lua_newtable(L);
    lua_settable(L, -3);
}

// A record too deep to push comes back as a shorter list with steps_truncated set, rather than as a
// half written table or a corrupted stack. The UI can say so; it cannot recover from the other two.
void push_steps(lua_State *L, const Arena &arena, const Derivation &d) {
    if (!lua_checkstack(L, 4)) {
        push_empty_steps(L);
        set_field(L, "steps_truncated", true);
        return;
    }
    bool complete = true;
    lua_pushstring(L, "steps");
    lua_newtable(L);
    int row = 0;
    const std::vector<StepId> &roots = d.roots();
    for (size_t i = 0; complete && i < roots.size(); ++i)
        complete = push_step(L, arena, d, roots[i], 0, &row);

#if NPS_DIAG
    {
        // Diagnostic: C++ writes four sound rows and Lua then faults reading the fourth. Reading it
        // back here, while the table is still on our stack, says whether it was already wrong when
        // built or whether something damaged it afterwards. Those are different bugs.
        char line[160];
        int n = snprintf(line, sizeof(line), "readback %d rows:", row);
        for (int i = 1; i <= row && n > 0 && n < (int)sizeof(line) - 24; ++i) {
            lua_rawgeti(L, -1, i);
            const int t = lua_type(L, -1);
            size_t fields = 0;
            if (t == LUA_TTABLE) {
                lua_pushnil(L);
                while (lua_next(L, -2)) {
                    ++fields;
                    lua_pop(L, 1);
                }
            }
            lua_pop(L, 1);
            n += snprintf(line + n, sizeof(line) - n, " %d=%s/%u", i, lua_typename(L, t),
                          (unsigned)fields);
        }
        trace(line);
    }
#endif

    lua_settable(L, -3);
    set_field(L, "steps_truncated", !complete);
}

void push_no_steps(lua_State *L) {
    push_empty_steps(L);
    set_field(L, "steps_truncated", false);
}

// Nil plus a message, with the position, because a parse error without one is no help while typing.
int parse_failed(lua_State *L, const ParseResult &r) {
    lua_pushnil(L);
    std::string msg(status_name(r.status));
    msg += " at character ";
    char digits[16];
    snprintf(digits, sizeof(digits), "%u", (unsigned)r.offset + 1);
    msg += digits;
    lua_pushlstring(L, msg.data(), msg.size());
    return 2;
}

// canonicalize answers kNoNode for a form it does not handle as well as for a limit it hit, and the
// arena is the only thing that knows which. Both the reading and its wording live in core, where a
// starved arena can be built on purpose and no caller here can reach either arm.
int canonical_refused(lua_State *L, const Arena &arena) {
    lua_pushnil(L);
    const std::string msg = canonical_refusal_message(arena);
    lua_pushlstring(L, msg.data(), msg.size());
    return 2;
}

int expression_resource_failure(lua_State *L, const std::string &detail) {
    return typed_failure(L, "resource exceeded", "resource limit reached", detail);
}

bool prepare_normalized_expression(const Arena &arena, const SolutionContext &context,
                                   std::string *normalized, std::string *detail) {
    *normalized = print(arena, context.normalized_problem_model);
    if (normalized->size() > arena.limits().max_input_bytes) {
        normalized->clear();
        *detail = "the normalized expression is longer than the accepted limit";
        return false;
    }
    return true;
}

void set_expression_context(lua_State *L, const SolutionContext &context) {
    set_field(L, "original_expression", context.original_expression);
    set_field(L, "normalized_expression", context.normalized_expression);
}

#if NPS_DIAG
// A reset leaves nothing on screen and nothing in a Lua variable, so the only way to find out how
// far a run got is to write it down as it goes. Opened, written and closed on every call so the
// bytes reach flash before the fault rather than sitting in a buffer that dies with the process.
//
// Diagnostic. It comes out once the reset is understood.
int l_trace(lua_State *L) {
    const char *text = luaL_checkstring(L, 1);
    FILE *f = fopen("/documents/ndl/nps_trace.txt.tns", "a");
    if (!f)
        return 0;
    fputs(text, f);
    fputc('\n', f);
    fclose(f);
    return 0;
}

// The largest block the allocator will still hand out. Giac is built with NO_STDEXCEPT and
// -fno-exceptions, so a failed allocation there cannot throw and comes back as a null that nothing
// checks. That makes the size of this number, and how it moves across calls, the thing worth
// watching rather than Lua's own heap count, which measures a different heap entirely.
int l_largest_block(lua_State *L) {
    lua_pushinteger(L, static_cast<lua_Integer>(allocator_headroom_kb()));
    return 1;
}

// Diagnostic. The same three writes as ndl/ndl/src/resources/utils.c:250-255.
//
// ploaderhook.c:342 masks interrupts for the whole of a module load and ploaderhook.c:382 disables
// the watchdog inside that window, with the comment that the OS seems to re-enable it. luagiac then
// masks interrupts again for the whole of every caseval (luabridge.cc:80-83). A watchdog that is
// live and unserviced across that window resets the calculator with no dialog, which is what we see,
// and it would explain why unrelated work beforehand changes whether a call survives.
int l_disable_watchdog(lua_State *L) {
    *(volatile unsigned *)0x90060C00 = 0x1ACCE551;
    *(volatile unsigned *)0x90060008 = 0;
    *(volatile unsigned *)0x90060C00 = 0;
    lua_pushboolean(L, 1);
    return 1;
}

// Diagnostic. Reads one word of physical memory and hands it back.
//
// luabridge.cc:39-40 reads 0x90110b04 and 0x90110b0c to detect exam mode, and does it as the very
// first thing luagiac_init does, with no is_cx2 branch. khicas branches on is_cx2 for other
// peripherals (k_csdk.c:1335 picks the contrast register that way) and Ndl reads the CX II keypad
// at 0x90140810 (on_key_pressed.c:27), so those two addresses are worth reading on their own before
// concluding anything about them.
int l_peek(lua_State *L) {
    const unsigned addr = static_cast<unsigned>(luaL_checkinteger(L, 1));
    lua_pushinteger(L, static_cast<lua_Integer>(*(volatile unsigned *)addr));
    return 1;
}

// What the watchdog registers read, so a run can say whether it was live rather than assume it.
int l_watchdog_state(lua_State *L) {
    lua_pushinteger(L, static_cast<lua_Integer>(*(volatile unsigned *)0x90060008));
    lua_pushinteger(L, static_cast<lua_Integer>(*(volatile unsigned *)0x90060004));
    return 2;
}

int l_trace_clear(lua_State *L) {
    (void)L;
    FILE *f = fopen("/documents/ndl/nps_trace.txt.tns", "w");
    if (f)
        fclose(f);
    return 0;
}
#endif

int l_allocator_headroom_kb(lua_State *L) {
    lua_pushinteger(L, static_cast<lua_Integer>(allocator_headroom_kb()));
    return 1;
}

// PERF-011 wants free memory with everything resident, and an outside probe cannot see that state:
// the handheld has no exec, and the document loader tears the Lua state down before a program runs.
// So the number has to come from inside the release module. Both figures are reported because a solve
// fragments rather than consumes, and the contiguous one runs out first.
int l_heap_free(lua_State *L) {
    const AllocatorTotalFree total = measure_total_free();
    if (!lua_checkstack(L, 4))
        return luaL_error(L, "not enough Lua stack for the heap reading");
    lua_newtable(L);
    set_field(L, "total_kb", static_cast<int>(total.total_bytes >> 10));
    set_field(L, "largest_kb", static_cast<int>(total.largest_block_bytes >> 10));
    set_field(L, "blocks", static_cast<int>(total.block_count));
    set_field(L, "truncated", total.hold_limit_reached);
    return 1;
}

// The underlying call takes a button count and varargs, so the count has to be fixed at each call
// site. Zero, two and three are what libndls exposes, and one custom label cannot be expressed.
int l_os_msgbox(lua_State *L) {
    const char *title = luaL_checkstring(L, 1);
    const char *message = luaL_checkstring(L, 2);
    const char *first = luaL_optstring(L, 3, nullptr);
    const char *second = luaL_optstring(L, 4, nullptr);
    const char *third = luaL_optstring(L, 5, nullptr);
    unsigned pressed;

    if (first && !second)
        return luaL_error(L, "os_msgbox takes no button labels, or two, or three");
    if (third)
        pressed = show_msgbox_3b(title, message, first, second, third);
    else if (second)
        pressed = show_msgbox_2b(title, message, first, second);
    else
        pressed = show_msgbox(title, message);
    lua_pushinteger(L, static_cast<lua_Integer>(pressed));
    return 1;
}

#if NPS_GIAC
int l_os_menu(lua_State *L) {
    const char *title = luaL_checkstring(L, 1);
    if (lua_type(L, 2) != LUA_TTABLE)
        return luaL_error(L, "os_menu takes a title and a table of item strings");

    // Counted and checked before the vector exists, so neither refusal below longjmps past it.
    int count = 0;
    for (;; ++count) {
        lua_rawgeti(L, 2, count + 1);
        const int type = lua_type(L, -1);
        lua_pop(L, 1);
        if (type == LUA_TNIL)
            break;
        if (type != LUA_TSTRING)
            return luaL_error(L, "os_menu item %d is not a string", count + 1);
    }
    if (count == 0)
        return luaL_error(L, "os_menu needs at least one item");

    std::vector<std::string> items;
    items.reserve(static_cast<size_t>(count));
    for (int index = 1; index <= count; ++index) {
        lua_rawgeti(L, 2, index);
        size_t size = 0;
        const char *text = lua_tolstring(L, -1, &size);
        items.push_back(std::string(text, size));
        lua_pop(L, 1);
    }

    std::vector<const char *> pointers;
    pointers.reserve(items.size());
    for (const std::string &item : items)
        pointers.push_back(item.c_str());

    const int chosen = show_native_menu(title, pointers.data(), pointers.size());
    if (chosen <= 0) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, chosen);
    return 1;
}
#endif

int l_os_number_input(lua_State *L) {
    const char *title = luaL_checkstring(L, 1);
    const char *subtitle = luaL_checkstring(L, 2);
    const char *message = luaL_checkstring(L, 3);
    int value = static_cast<int>(luaL_optinteger(L, 4, 0));
    const int low = static_cast<int>(luaL_optinteger(L, 5, 0));
    const int high = static_cast<int>(luaL_optinteger(L, 6, 9999));
    if (show_1numeric_input(title, subtitle, message, &value, low, high) != 1) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushinteger(L, value);
    return 1;
}

int l_device_identity(lua_State *L) {
    const DeviceIdentity identity = interpret_device_identity(
        nl_hwtype(), nl_hwsubtype(), nl_osid(), nl_ndl_rev(),
        nl_loaded_by_3rd_party_loader() != FALSE);
    if (!lua_checkstack(L, 4))
        return luaL_error(L, "not enough Lua stack for the device reading");
    lua_newtable(L);
    set_field(L, "model", calculator_model_name(identity.model));
    set_field(L, "cas", cas_build_name(identity.cas));
    set_field(L, "os", identity.os_name);
    set_field(L, "os_index", static_cast<int>(identity.os_version_index));
    set_field(L, "hardware_type", static_cast<int>(identity.hardware_type));
    set_field(L, "hardware_subtype", static_cast<int>(identity.hardware_subtype));
    set_field(L, "ndl_revision", static_cast<int>(identity.ndl_revision));
    set_field(L, "third_party_loader", identity.loaded_by_third_party_loader);
    set_field(L, "model_agrees_with_os", identity.model_agrees_with_os);
    return 1;
}

#if NPS_RESOURCE_PROFILE
int profile_argument_error(lua_State *L, const char *message, const char *field = "") {
    ResourceProfileMetrics metrics;
    metrics.request_failed = true;
    finish_resource_profile(metrics);
    return luaL_error(L, message, field);
}

bool profile_boolean_field(lua_State *L, int table_index, const char *key) {
    const int table = absolute_index(L, table_index);
    lua_pushstring(L, key);
    lua_rawget(L, table);
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return false;
    }
    if (lua_type(L, -1) != LUA_TBOOLEAN) {
        lua_pop(L, 1);
        profile_argument_error(L, "%s must be a boolean", key);
        return false;
    }
    const bool value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return value;
}

uint64_t profile_metric_field(lua_State *L, int table_index, const char *key) {
    const int table = absolute_index(L, table_index);
    lua_pushstring(L, key);
    lua_rawget(L, table);
    if (lua_type(L, -1) != LUA_TNUMBER) {
        lua_pop(L, 1);
        profile_argument_error(L, "%s must be a non-negative integer", key);
        return 0;
    }
    const lua_Number number = lua_tonumber(L, -1);
    lua_pop(L, 1);
    if (number != number || number < 0 ||
        number > static_cast<lua_Number>(std::numeric_limits<uint32_t>::max())) {
        profile_argument_error(L, "%s is outside the target measurement range", key);
        return 0;
    }
    const uint64_t value = static_cast<uint64_t>(number);
    if (static_cast<lua_Number>(value) != number) {
        profile_argument_error(L, "%s must be a non-negative integer", key);
        return 0;
    }
    return value;
}

const char *profile_operation(lua_State *L, int table_index) {
    const int table = absolute_index(L, table_index);
    lua_pushstring(L, "operation");
    lua_rawget(L, table);
    size_t length = 0;
    const char *operation = lua_type(L, -1) == LUA_TSTRING
                                ? lua_tolstring(L, -1, &length)
                                : nullptr;
    if (!operation || length == 0 || std::strlen(operation) != length) {
        lua_pop(L, 1);
        profile_argument_error(L, "operation must be a non-empty string without embedded nulls");
        return nullptr;
    }
    return operation;
}

void set_profile_size_field(lua_State *L, const char *key, size_t value) {
    lua_pushstring(L, key);
    lua_pushnumber(L, static_cast<lua_Number>(value));
    lua_settable(L, -3);
}

void set_profile_headroom(lua_State *L, const char *key, const AllocatorHeadroom &headroom) {
    lua_pushstring(L, key);
    lua_newtable(L);
    set_field(L, "scope", "contiguous allocation lower bound, not free memory");
    set_profile_size_field(L, "contiguous_lower_bound_bytes",
                           headroom.contiguous_lower_bound_bytes);
    set_profile_size_field(L, "resolution_bytes", headroom.resolution_bytes);
    set_profile_size_field(L, "search_ceiling_bytes", headroom.search_ceiling_bytes);
    set_field(L, "ceiling_reached", headroom.ceiling_reached);
    lua_settable(L, -3);
}

void set_profile_allocation(lua_State *L, const AllocationSnapshot &allocation) {
    lua_pushstring(L, "allocation");
    lua_newtable(L);
    set_field(L, "scope", "module requests observed through wrapped allocation APIs");
    set_profile_size_field(L, "baseline_requested_bytes",
                           allocation.baseline_requested_bytes);
    set_profile_size_field(L, "current_requested_bytes", allocation.current_requested_bytes);
    set_profile_size_field(L, "interval_peak_requested_bytes",
                           allocation.peak_requested_bytes);
    set_profile_size_field(L, "allocation_count", allocation.allocation_count);
    set_profile_size_field(L, "free_count", allocation.free_count);
    set_profile_size_field(L, "reallocation_count", allocation.reallocation_count);
    set_profile_size_field(L, "failure_count", allocation.failure_count);
    set_profile_size_field(L, "unknown_free_count", allocation.unknown_free_count);
    set_profile_size_field(L, "unknown_reallocation_count",
                           allocation.unknown_reallocation_count);
    set_field(L, "valid", allocation.valid);
    set_field(L, "overflowed", allocation.overflowed);
    lua_settable(L, -3);
}

int l_resource_profile_begin(lua_State *L) {
    if (lua_type(L, 1) != LUA_TSTRING)
        return luaL_error(L, "operation must be a string");
    size_t length = 0;
    const char *operation = lua_tolstring(L, 1, &length);
    if (!operation || length == 0 || std::strlen(operation) != length)
        return luaL_error(L, "operation must be non-empty and contain no embedded nulls");
    lua_pushboolean(L, begin_resource_profile(operation));
    return 1;
}

int l_resource_profile_finish(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE)
        return profile_argument_error(L, "resource profile metrics must be a table");

    ResourceProfileMetrics metrics;
    metrics.operation = profile_operation(L, 1);
    metrics.render_ready_ms = profile_metric_field(L, 1, "render_ready_ms");
    metrics.lua_live_bytes = static_cast<size_t>(profile_metric_field(L, 1, "lua_live_bytes"));
    metrics.request_failed = profile_boolean_field(L, 1, "request_failed");
    metrics.solver_metrics_available = profile_boolean_field(L, 1, "solver_metrics_available");
    if (metrics.solver_metrics_available) {
        metrics.arena_nodes = static_cast<size_t>(profile_metric_field(L, 1, "arena_nodes"));
        metrics.arena_child_slots =
            static_cast<size_t>(profile_metric_field(L, 1, "arena_child_slots"));
        metrics.derivation_steps =
            static_cast<size_t>(profile_metric_field(L, 1, "derivation_steps"));
        metrics.rewrites = static_cast<size_t>(profile_metric_field(L, 1, "rewrites"));
        metrics.backend_calls =
            static_cast<size_t>(profile_metric_field(L, 1, "backend_calls"));
    }

    const ResourceProfileResult profile = finish_resource_profile(metrics);
    lua_newtable(L);
    const char *status = profile.interval_active
                             ? (profile.report_written ? "written" : "report write failed")
                             : "no active interval";
    set_field(L, "status", status);
    set_field(L, "interval_active", profile.interval_active);
    set_field(L, "report_written", profile.report_written);
    set_field(L, "report_path", kResourceProfileReportPath);
    set_profile_size_field(L, "tracker_capacity", profile.tracker_capacity);
    set_profile_allocation(L, profile.allocation);
    set_profile_headroom(L, "headroom_before", profile.headroom_before);
    set_profile_headroom(L, "headroom_render_ready", profile.headroom_render_ready);
    return 1;
}
#endif

const char *scalar_string_argument(lua_State *L, int index, const char *fallback = nullptr) {
    size_t size = 0;
    const char *text = fallback ? luaL_optlstring(L, index, fallback, &size)
                                : luaL_checklstring(L, index, &size);
    if (std::memchr(text, '\0', size) != nullptr)
        luaL_argerror(L, index, "embedded NUL is not allowed");
    return text;
}

// The variable is typed text, so a refusal is nil and a reason as for a parse failure, not a raise.
bool variable_argument(lua_State *L, int index, std::string *variable) {
    const char *name = scalar_string_argument(L, index, "x");
    if (!is_identifier(name)) {
        lua_pushnil(L);
        lua_pushstring(L, "variable must be a single identifier without whitespace");
        return false;
    }
    *variable = normalize_identifier(name);
    return true;
}

int l_canonical(lua_State *L) {
    const char *text = scalar_string_argument(L, 1);

    Arena arena;
    ParseResult parsed = parse(arena, text);
    if (!parsed.ok())
        return parse_failed(L, parsed);

    NodeId c = canonicalize(arena, parsed.root);
    if (c == kNoNode)
        return canonical_refused(L, arena);

    std::string out = print(arena, c);
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

int l_math_display(lua_State *L) {
    const char *text = scalar_string_argument(L, 1);
    if (lua_objlen(L, 1) > Limits{}.max_input_bytes) {
        lua_pushnil(L);
        lua_pushliteral(L, "math display exceeds the input limit");
        return 2;
    }
    Arena arena;
    const ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) return parse_failed(L, parsed);
    const std::string display = print_math(arena, parsed.root);
    lua_pushlstring(L, display.data(), display.size());
    return 1;
}

int l_giac(lua_State *L) {
    const char *text = scalar_string_argument(L, 1);

    Arena arena;
    ParseResult parsed = parse(arena, text);
    if (!parsed.ok())
        return parse_failed(L, parsed);

    std::string out = print_giac(arena, parsed.root);
    // An empty rendering of a parsed expression means Giac has no faithful syntax for it, which is
    // true of assignment, approximation and identity. Refused by name rather than handed back as an
    // empty string, because an empty command reaching Giac is a different question than the one
    // asked and its answer would be attributed to this one.
    if (out.empty()) {
        lua_pushnil(L);
        lua_pushstring(L, "this relation has no faithful Giac syntax, so it is not sent");
        return 2;
    }
    lua_pushlstring(L, out.data(), out.size());
    return 1;
}

// Whether to cross-check is the function that was called, not an argument. A boolean third argument
// was tried and measured on the handheld: with it present, lua_toboolean reported false for a Lua
// true, and the cross-check was silently skipped for both entry points. The cause was Ndl's
// syscall table naming the wrong OS function as lua_toboolean on every CX II OS, fixed 2026-09-03
// in the MakeSyscalls idc files. The separate entry points stay, because what a flag turns off is
// the check section 17 says must be visible.
// The mode arrives per call rather than being held here, so this side keeps no state a replay could
// not see and the document is free to change the setting between two solves.
NumericMode mode_argument(lua_State *L, int index) {
    const char *named = scalar_string_argument(L, index, "exact");
    if (std::string(named) == "exact")
        return NumericMode::Exact;
    if (std::string(named) == "decimal")
        return NumericMode::Decimal;
    luaL_error(L, "numeric mode has to be 'exact' or 'decimal', not '%s'", named);
    return NumericMode::Exact;
}

int solve_into(lua_State *L, bool cross) {
    // Argument checks come first: a raise is a longjmp, which skips the GcPause destructor.
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    const NumericMode mode = mode_argument(L, 3);
    std::string variable;
    if (!variable_argument(L, 2, &variable))
        return 2;
    const std::string text(text_data, text_size);
    GcPause paused(L);

    Arena arena;
    ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        if (resource_status(parsed.status))
            return expression_resource_failure(L, parsed.message);
        return parse_failed(L, parsed);
    }

    Derivation d;
    d.request.original_expression = text;
    d.request.numeric_mode = mode;
    const NodeId unknown = arena.symbol(variable);

    // Degree two with no term of degree one belongs to another family, and this is where a typed
    // equation is sent to it. Asked before the linear rule rather than after it because every
    // rejection it makes is decided before its first recorded step, while the linear rule can keep a
    // checked prefix and then report the same equation as not linear, which would leave a dead
    // prefix under the split that answered it.
    Attempt a;
    QuadraticResult q =
        solve_by_square_root(arena, d, parsed.root, unknown, interactive_budget());
    if (quadratic_family(q.outcome)) {
        a = quadratic_attempt(arena, q);
    } else {
        SolveResult r = solve_linear(arena, d, parsed.root, unknown, interactive_budget());
        a = linear_attempt(arena, r);
        // The probe's own spend, so the reported cost is what the solve took rather than what the
        // rule that answered it took.
        add_cost(&a.cost, q.cost);
    }

    std::string normalized_expression;
    std::string normalization_detail;
    if (!prepare_normalized_expression(arena, d.context, &normalized_expression,
                                       &normalization_detail))
        return expression_resource_failure(L, normalization_detail);
    d.context.normalized_expression = normalized_expression;

    // Giac first, while nothing of ours is on the Lua stack. The solution is what it is asked to
    // agree with, so solve gets the same comparison differentiation gets rather than a bare tag.
    CrossCheck c;
    if (cross && a.ask_backend)
        c = ask_giac(L, arena, a.status, Op::Solve, parsed.root, unknown, a.comparable, false,
                     a.finite_solutions ? &a.solutions : nullptr);
    const bool answer_only = answer_only_allowed(a.status) && c.has_answer;
    const DerivationStatus status =
        cross && (a.solved || a.finite_solutions) ? cross_checked_status(a.status, c) : a.status;
    d.context.derivation_status = status;

    lua_newtable(L);
    set_field(L, "outcome", a.outcome);
    set_field(L, "detail", a.detail);
    set_field(L, "solved", a.solved);
    set_field(L, "has_result", a.has_result || answer_only);
    set_field(L, "answer_only", answer_only);
    set_field(L, "status", derivation_status_name(status));
    set_field(L, "result_form",
              result_form_name(primary_result_form(a.has_result, answer_only, status, c.form)));
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    set_expression_context(L, d.context);
    if (!a.answer.empty())
        set_field(L, "result", a.answer);
    else if (answer_only)
        set_field(L, "result", c.value);
    if (cross)
        set_cross_check(L, c, a.has_result || answer_only);
    set_cost(L, arena, d, a.cost, c.calls);
    // Whether there is a walkthrough, not where the answer came from. A backend answer is segregated
    // from the walkthrough rather than a substitute for it, so a refusal that got partway shows the
    // prefix STEP-025 kept and the answer beside it. Only a refusal that recorded nothing has none.
    if (d.size() == 0)
        push_no_steps(L);
    else
        push_steps(L, arena, d);
    return 1;
}

int l_solve(lua_State *L) { return solve_into(L, true); }

// The same solve with no call into Giac. Separate rather than a flag, so nothing can turn the
// cross-check off by accident.
int l_solve_local(lua_State *L) { return solve_into(L, false); }

// PERF-002 asks for a solve that is computed incrementally within a documented memory budget, so
// the task outlives the call that starts it and the Lua owner advances it a bounded number of
// cooperative checkpoints per paint. One resident task, because the viewer shows one solve.
constexpr size_t kSolveTaskFrameBytes = 65536;

std::optional<SolveTask> resident_solve;

const char *task_state_name(TaskState state) {
    switch (state) {
        case TaskState::Pending: return "pending";
        case TaskState::Complete: return "complete";
        case TaskState::Cancelled: return "cancelled";
        case TaskState::AllocationFailed: return "allocation_failed";
        case TaskState::Invalid: break;
    }
    return "invalid";
}

// The published prefix goes over whatever the state is, because a cancelled or exhausted solve
// keeps the moves it verified and the viewer is entitled to show them.
void push_solve_progress(lua_State *L, SolveTask &task) {
    const Derivation &d = task.published();
    const SolveResources resources = task.resources();
    lua_newtable(L);
    set_field(L, "state", task_state_name(task.state()));
    set_field(L, "pending", task.state() == TaskState::Pending);
    set_field(L, "status", derivation_status_name(d.context.derivation_status));
    set_expression_context(L, d.context);
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    set_field(L, "frame_capacity", static_cast<int>(resources.frame_capacity));
    set_field(L, "frame_live_bytes", static_cast<int>(resources.frame_live_bytes));
    set_field(L, "frame_peak_bytes", static_cast<int>(resources.frame_peak_bytes));
    set_field(L, "live_frames", static_cast<int>(resources.live_frames));
    set_cost(L, task.arena(), d, resources.cost, 0);
    const SolveTaskResult *result = task.result();
    if (result) {
        if (const SolveResult *linear = std::get_if<SolveResult>(result)) {
            const Attempt a = linear_attempt(task.arena(), *linear);
            set_field(L, "outcome", a.outcome);
            set_field(L, "detail", a.detail);
            set_field(L, "solved", a.solved);
            set_field(L, "has_result", a.has_result);
            if (!a.answer.empty())
                set_field(L, "result", a.answer);
        } else {
            const RearrangeResult &r = std::get<RearrangeResult>(*result);
            set_field(L, "outcome", rearrange_outcome_name(r.outcome));
            set_field(L, "detail", r.detail);
            set_field(L, "solved", r.outcome == RearrangeOutcome::Isolated);
            set_field(L, "has_result", r.formula != kNoNode);
            if (r.formula != kNoNode)
                set_field(L, "result", print(task.arena(), r.formula));
        }
    }
    if (d.size() == 0)
        push_empty_steps(L);
    else
        push_steps(L, task.arena(), d);
}

// Starting a solve replaces the resident one. An abandoned viewer must not keep its frames charged
// against the next solve's budget, and close is what releases them.
int l_solve_begin(lua_State *L) {
    // Argument checks come first: a raise is a longjmp, which skips the GcPause destructor.
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    const char *operation = scalar_string_argument(L, 3, "linear");
    const NumericMode mode = mode_argument(L, 4);
    const std::string named(operation);
    if (named != "linear" && named != "rearrange")
        luaL_error(L, "solve operation has to be 'linear' or 'rearrange', not '%s'", operation);
    std::string variable;
    if (!variable_argument(L, 2, &variable))
        return 2;
    const std::string text(text_data, text_size);
    GcPause paused(L);
    resident_solve.reset();

    SolveRequest request;
    request.original_expression = text;
    request.numeric_mode = mode;
    resident_solve.emplace(named == "linear" ? SolveOperation::Linear : SolveOperation::Rearrange,
                           std::move(request), variable, kSolveTaskFrameBytes);
    push_solve_progress(L, *resident_solve);
    return 1;
}

// Units are cooperative checkpoints rather than milliseconds, so the same call does the same work
// on the host and on the handheld and a test can pin the prefix a given budget reaches.
int l_solve_advance(lua_State *L) {
    const lua_Integer units = luaL_optinteger(L, 1, 1);
    if (units < 0)
        luaL_argerror(L, 1, "units cannot be negative");
    if (!resident_solve) {
        lua_pushnil(L);
        lua_pushliteral(L, "no solve is in progress");
        return 2;
    }
    GcPause paused(L);
    resident_solve->advance(static_cast<size_t>(units));
    push_solve_progress(L, *resident_solve);
    return 1;
}

int l_solve_cancel(lua_State *L) {
    if (!resident_solve) {
        lua_pushnil(L);
        lua_pushliteral(L, "no solve is in progress");
        return 2;
    }
    GcPause paused(L);
    resident_solve->cancel();
    push_solve_progress(L, *resident_solve);
    return 1;
}

// Releasing the frames is separate from cancelling, because the viewer reads the published prefix
// of a cancelled solve and only closes it when it moves on.
int l_solve_close(lua_State *L) {
    const bool held = resident_solve.has_value();
    resident_solve.reset();
    lua_pushboolean(L, held);
    return 1;
}

int differentiate_into(lua_State *L, bool cross) {
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    const NumericMode mode = mode_argument(L, 3);
    std::string variable;
    if (!variable_argument(L, 2, &variable))
        return 2;
    const std::string text(text_data, text_size);
    GcPause paused(L);
    trace_mem(L, "differentiate_into: entered");

    Arena arena;
    trace("differentiate_into: arena built");
    ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        if (resource_status(parsed.status))
            return expression_resource_failure(L, parsed.message);
        return parse_failed(L, parsed);
    }
    trace("differentiate_into: parsed");

    Derivation d;
    d.request.original_expression = text;
    d.request.numeric_mode = mode;
    DiffResult r =
        differentiate(arena, d, parsed.root, arena.symbol(variable), interactive_budget());
    std::string normalized_expression;
    std::string normalization_detail;
    if (!prepare_normalized_expression(arena, d.context, &normalized_expression,
                                       &normalization_detail))
        return expression_resource_failure(L, normalization_detail);
    d.context.normalized_expression = normalized_expression;
    trace("differentiate_into: differentiated");

    // Everything that needs Giac happens here, before the table exists, so no half-built Lua value
    // of ours is on the stack while the interpreter is re-entered.
    CrossCheck c;
    if (cross && cross_check_allowed(r.outcome))
        c = ask_giac(L, arena, r.status, Op::Differentiate, parsed.root, arena.symbol(variable),
                    r.derivative);
    const bool answer_only = answer_only_allowed(r.status) && c.has_answer;
    const DerivationStatus status = cross && r.outcome == DiffOutcome::Differentiated
                                        ? cross_checked_status(r.status, c)
                                        : r.status;
    d.context.derivation_status = status;
    trace_mem(L, "differentiate_into: giac done");

    // Printing also happens before the table, for the same reason: it is arena work, not Lua work.
    const std::string printed = r.derivative == kNoNode ? std::string() : print(arena, r.derivative);
    const NodeId canon = r.derivative == kNoNode ? kNoNode : canonicalize(arena, r.derivative);
    const std::string canon_text = canon == kNoNode ? std::string() : print(arena, canon);
    const bool local_result = r.derivative != kNoNode;
    const bool has_result = local_result || answer_only;
    trace("differentiate_into: printed");

    lua_newtable(L);
    set_field(L, "outcome", diff_outcome_name(r.outcome));
    set_field(L, "detail", r.detail);
    set_field(L, "solved", r.outcome == DiffOutcome::Differentiated);
    set_field(L, "has_result", has_result);
    set_field(L, "answer_only", answer_only);
    set_field(L, "status", derivation_status_name(status));
    set_field(L, "result_form",
              result_form_name(primary_result_form(local_result, answer_only, status, c.form)));
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    set_expression_context(L, d.context);
    if (!printed.empty())
        set_field(L, "result", printed);
    else if (answer_only)
        set_field(L, "result", c.value);
    if (!canon_text.empty())
        set_field(L, "canonical", canon_text);
    if (cross)
        set_cross_check(L, c, has_result);
    set_cost(L, arena, d, r.cost, c.calls);
    // Whether there is a walkthrough, not where the answer came from. A backend answer is segregated
    // from the walkthrough rather than a substitute for it, so a refusal that got partway shows the
    // prefix STEP-025 kept and the answer beside it. Only a refusal that recorded nothing has none.
    if (d.size() == 0)
        push_no_steps(L);
    else
        push_steps(L, arena, d);
    trace_mem(L, "differentiate_into: steps pushed");
    return 1;
}

int l_differentiate(lua_State *L) { return differentiate_into(L, true); }

int l_differentiate_local(lua_State *L) { return differentiate_into(L, false); }

// The cross-check here is the independent half of VER-005: Giac differentiates our answer and says
// whether that is the integrand. Comparing antiderivatives instead would report a disagreement for
// every answer that differs by a constant or by ln(x) against ln(abs(x)), which is not a wrong
// answer, and it would leave the derivative check with no second opinion.
int integrate_into(lua_State *L, bool cross) {
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    const NumericMode mode = mode_argument(L, 3);
    std::string variable;
    if (!variable_argument(L, 2, &variable))
        return 2;
    const std::string text(text_data, text_size);
    GcPause paused(L);

    Arena arena;
    ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        if (resource_status(parsed.status))
            return expression_resource_failure(L, parsed.message);
        return parse_failed(L, parsed);
    }

    Derivation d;
    d.request.original_expression = text;
    d.request.numeric_mode = mode;
    GiacBackend backend(L);
    const bool backed = cross && GiacBackend::available(L);
    IntegrateResult r = integrate(arena, d, parsed.root, arena.symbol(variable), interactive_budget(),
                                  backed ? &backend : nullptr);
    std::string normalized_expression;
    std::string normalization_detail;
    if (!prepare_normalized_expression(arena, d.context, &normalized_expression,
                                       &normalization_detail))
        return expression_resource_failure(L, normalization_detail);
    d.context.normalized_expression = normalized_expression;

    CrossCheck c;
    switch (integrate_cross_check_route(cross, r.outcome, r.particular, r.status)) {
        case IntegrateCrossCheckRoute::Differentiate:
            c = ask_giac(L, arena, r.status, Op::Differentiate, r.particular,
                         arena.symbol(variable), parsed.root, true, nullptr,
                         backed ? &backend : nullptr);
            break;
        case IntegrateCrossCheckRoute::Integrate:
            c = ask_giac(L, arena, r.status, Op::Integrate, parsed.root, arena.symbol(variable),
                         kNoNode, true, nullptr, backed ? &backend : nullptr);
            break;
        case IntegrateCrossCheckRoute::None:
            break;
    }
    const bool answer_only = answer_only_allowed(r.status) && c.has_answer;
    const DerivationStatus status = cross && r.outcome == IntegrateOutcome::Integrated
                                        ? cross_checked_status(r.status, c)
                                        : r.status;
    d.context.derivation_status = status;

    const std::string printed =
        r.antiderivative == kNoNode ? std::string() : print(arena, r.antiderivative);
    const NodeId canon =
        r.antiderivative == kNoNode ? kNoNode : canonicalize(arena, r.antiderivative);
    const std::string canon_text = canon == kNoNode ? std::string() : print(arena, canon);
    const std::string assumptions = joined(d.context.active_assumptions);
    const bool local_result = r.antiderivative != kNoNode;
    const bool has_result = local_result || answer_only;

    lua_newtable(L);
    set_field(L, "outcome", integrate_outcome_name(r.outcome));
    set_field(L, "detail", r.detail);
    set_field(L, "solved", r.outcome == IntegrateOutcome::Integrated);
    set_field(L, "has_result", has_result);
    set_field(L, "answer_only", answer_only);
    set_field(L, "status", derivation_status_name(status));
    set_field(L, "result_form",
              result_form_name(primary_result_form(local_result, answer_only, status, c.form)));
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    set_expression_context(L, d.context);
    if (!printed.empty())
        set_field(L, "result", printed);
    else if (answer_only)
        set_field(L, "result", c.value);
    if (!canon_text.empty())
        set_field(L, "canonical", canon_text);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    if (cross) {
        set_cross_check(L, c, r.particular != kNoNode || answer_only);
        if (!answer_only)
            set_field(L, "giac_method", "differentiated the answer");
    }
    set_cost(L, arena, d, r.cost, r.cost.backend_calls + c.calls);
    // Whether there is a walkthrough, not where the answer came from. A backend answer is segregated
    // from the walkthrough rather than a substitute for it, so a refusal that got partway shows the
    // prefix STEP-025 kept and the answer beside it. Only a refusal that recorded nothing has none.
    if (d.size() == 0)
        push_no_steps(L);
    else
        push_steps(L, arena, d);
    return 1;
}

int rewrite_into(lua_State *L, CommandKind kind) {
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    mode_argument(L, 3);
    std::string variable;
    if (!variable_argument(L, 2, &variable))
        return 2;
    const std::string text(text_data, text_size);
    GcPause paused(L);
    Arena arena;
    const ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        if (resource_status(parsed.status))
            return expression_resource_failure(L, parsed.message);
        return typed_failure(L, "invalid input", "invalid input", parsed.message);
    }
    Derivation d;
    d.request.original_expression = text;
    GiacBackend giac(L);
    Backend *backend = GiacBackend::available(L) ? &giac : nullptr;
    const char *outcome = "refused";
    std::string detail;
    NodeId answer = kNoNode;
    DerivationStatus status = DerivationStatus::NotRecorded;
    Cost cost;
    bool solved = false;
    if (kind == CommandKind::Rearrange) {
        const RearrangeResult r = rearrange(arena, d, parsed.root, arena.symbol(variable),
                                            interactive_budget(), backend);
        outcome = rearrange_outcome_name(r.outcome);
        detail = r.detail;
        answer = r.formula;
        status = r.status;
        cost = r.cost;
        solved = r.outcome == RearrangeOutcome::Isolated;
    } else {
        const RewriteGoal goal = kind == CommandKind::Factor ? RewriteGoal::Factor
                               : kind == CommandKind::Expand ? RewriteGoal::Expand : RewriteGoal::Simplify;
        const RewriteResult r = rewrite(arena, d, parsed.root, goal, interactive_budget(), backend);
        outcome = rewrite_outcome_name(r.outcome);
        detail = r.detail;
        answer = r.expression;
        status = r.status;
        cost = r.cost;
        solved = r.outcome == RewriteOutcome::Rewritten || r.outcome == RewriteOutcome::AlreadyInForm;
    }
    std::string normalized;
    std::string normalization_detail;
    if (!prepare_normalized_expression(arena, d.context, &normalized, &normalization_detail))
        return expression_resource_failure(L, normalization_detail);
    d.context.normalized_expression = normalized;
    const std::string printed = answer == kNoNode ? std::string() : print(arena, answer);
    lua_newtable(L);
    set_field(L, "outcome", outcome);
    set_field(L, "detail", detail);
    set_field(L, "solved", solved);
    set_field(L, "has_result", solved && answer != kNoNode);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(status));
    set_field(L, "result_form",
              result_form_name(primary_result_form(solved && answer != kNoNode, false, status,
                                                   ResultForm::NoResult)));
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    set_expression_context(L, d.context);
    if (!printed.empty())
        set_field(L, "result", printed);
    set_cost(L, arena, d, cost, cost.backend_calls);
    if (d.size() == 0)
        push_no_steps(L);
    else
        push_steps(L, arena, d);
    return 1;
}

int integer_into(lua_State *L) {
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    mode_argument(L, 3);
    const std::string text(text_data, text_size);
    GcPause paused(L);
    Arena arena;
    const ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        if (resource_status(parsed.status))
            return expression_resource_failure(L, parsed.message);
        return typed_failure(L, "invalid input", "invalid input", parsed.message);
    }
    Derivation d;
    d.request.original_expression = text;
    const IntegerResult result = integer_method(arena, d, parsed.root, interactive_budget());
    std::string normalized;
    std::string normalization_detail;
    if (!prepare_normalized_expression(arena, d.context, &normalized, &normalization_detail))
        return expression_resource_failure(L, normalization_detail);
    d.context.normalized_expression = normalized;
    const bool solved = result.outcome == IntegerOutcome::Evaluated;
    const bool has_result = solved && result.expression != kNoNode;
    const std::string printed = has_result ? print(arena, result.expression) : std::string();
    lua_newtable(L);
    set_field(L, "outcome", integer_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", solved);
    set_field(L, "has_result", has_result);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    set_field(L, "result_form",
              result_form_name(primary_result_form(has_result, false, result.status,
                                                   ResultForm::NoResult)));
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    set_expression_context(L, d.context);
    if (!printed.empty())
        set_field(L, "result", printed);
    set_cost(L, arena, d, result.cost, result.cost.backend_calls);
    if (d.size() == 0)
        push_no_steps(L);
    else
        push_steps(L, arena, d);
    return 1;
}

int matrix_into(lua_State *L, CommandKind kind) {
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    mode_argument(L, 3);
    const std::string text(text_data, text_size);
    GcPause paused(L);
    Arena arena;
    const ParseResult parsed = parse(arena, text);
    if (!parsed.ok()) {
        if (resource_status(parsed.status))
            return expression_resource_failure(L, parsed.message);
        return typed_failure(L, "invalid input", "invalid input", parsed.message);
    }
    Derivation d;
    d.request.original_expression = text;
    GiacBackend backend(L);
    Adapter adapter(arena, backend);
    const MatrixResult result = kind == CommandKind::Determinant
                                    ? matrix_determinant(arena, adapter, d, parsed.root, interactive_budget())
                                    : matrix_method(arena, adapter, d, parsed.root,
                                                    kind == CommandKind::Ref ? MatrixForm::Echelon
                                                                             : MatrixForm::ReducedEchelon,
                                                    interactive_budget());
    std::string normalized;
    std::string normalization_detail;
    if (!prepare_normalized_expression(arena, d.context, &normalized, &normalization_detail))
        return expression_resource_failure(L, normalization_detail);
    d.context.normalized_expression = normalized;
    const bool solved = result.outcome == MatrixOutcome::Reduced || result.outcome == MatrixOutcome::Determined;
    const bool has_result = solved && result.expression != kNoNode;
    const std::string printed = has_result ? print(arena, result.expression) : std::string();
    lua_newtable(L);
    set_field(L, "outcome", matrix_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", solved);
    set_field(L, "has_result", has_result);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    set_field(L, "result_form",
              result_form_name(primary_result_form(has_result, false, result.status,
                                                   ResultForm::NoResult)));
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    set_expression_context(L, d.context);
    if (!printed.empty())
        set_field(L, "result", printed);
    // The adapter's count, not the record's, which charges the budget before the call is made.
    set_cost(L, arena, d, result.cost, adapter.call_count());
    if (d.size() == 0)
        push_no_steps(L);
    else
        push_steps(L, arena, d);
    return 1;
}

int bounded_integer_argument(lua_State *L, int index, int minimum, int maximum) {
    if (lua_type(L, index) != LUA_TNUMBER) luaL_argerror(L, index, "an integer is required");
    const lua_Number value = lua_tonumber(L, index);
    if (!(value >= minimum && value <= maximum)) luaL_argerror(L, index, "integer is outside the supported range");
    const int integer = static_cast<int>(value);
    if (value != integer) luaL_argerror(L, index, "an integer is required");
    return integer;
}

int paint_canvas(lua_State *L) {
    const auto *canvas = *static_cast<const ui::Canvas **>(lua_touserdata(L, 2));
    for (const ui::Fill &fill : canvas->fills()) {
        lua_getfield(L, 1, "setColorRGB");
        lua_pushvalue(L, 1);
        lua_pushinteger(L, fill.color.red);
        lua_pushinteger(L, fill.color.green);
        lua_pushinteger(L, fill.color.blue);
        lua_call(L, 4, 0);
        lua_getfield(L, 1, "fillRect");
        lua_pushvalue(L, 1);
        lua_pushinteger(L, fill.bounds.x);
        lua_pushinteger(L, fill.bounds.y);
        lua_pushinteger(L, fill.bounds.width);
        lua_pushinteger(L, fill.bounds.height);
        lua_call(L, 5, 0);
    }
    return 0;
}

const ui::Canvas **prepare_canvas(lua_State *L) {
    auto **reference = static_cast<const ui::Canvas **>(lua_newuserdata(L, sizeof(const ui::Canvas *)));
    *reference = nullptr;
    const int token = lua_gettop(L);
    lua_pushcfunction(L, paint_canvas);
    lua_pushvalue(L, 1);
    lua_pushvalue(L, token);
    return reference;
}

bool paint_canvas_protected(lua_State *L, const ui::Canvas &canvas) {
    if (!canvas.good()) { lua_pop(L, 3); return false; }
    if (lua_pcall(L, 2, 0, 0) == 0) return true;
    lua_pop(L, 1);
    return false;
}

int l_ui_panel(lua_State *L) {
    const int width = bounded_integer_argument(L, 2, 1, 4096);
    const int height = bounded_integer_argument(L, 3, 1, 4096);
    const int header = bounded_integer_argument(L, 4, 0, height);
    const int footer = bounded_integer_argument(L, 5, 0, height);
    const ui::Canvas **reference = prepare_canvas(L);
    ui::Canvas canvas(width, height);
    *reference = &canvas;
    canvas.panel(header, footer);
    const bool painted = paint_canvas_protected(L, canvas);
    *reference = nullptr;
    lua_pushboolean(L, painted);
    return 1;
}

int l_ui_icon(lua_State *L) {
    const int icon = bounded_integer_argument(L, 2, 0, 4);
    const int x = bounded_integer_argument(L, 3, -4096, 4096);
    const int y = bounded_integer_argument(L, 4, -4096, 4096);
    const int width = bounded_integer_argument(L, 5, 1, 4096);
    const int height = bounded_integer_argument(L, 6, 1, 4096);
    const ui::Canvas **reference = prepare_canvas(L);
    ui::Canvas canvas(width, height);
    *reference = &canvas;
    canvas.icon(static_cast<ui::Icon>(icon), x, y, {37, 57, 87}, {255, 255, 255});
    const bool painted = paint_canvas_protected(L, canvas);
    *reference = nullptr;
    lua_pushboolean(L, painted);
    return 1;
}

int l_ui_icon_image(lua_State *L) {
    const int icon = bounded_integer_argument(L, 1, 0, 4);
    std::array<char, 532> encoded{};
    if (!ui::icon_ti_image(static_cast<ui::Icon>(icon), encoded)) {
        lua_pushnil(L);
        return 1;
    }
    lua_pushlstring(L, encoded.data(), encoded.size());
    return 1;
}

#if NPS_RETAINED_UI
alignas(ui::RetainedMenu::pixel_alignment) std::array<uint16_t, 320 * 240> menu_pixels;
std::array<char, 20 + 320 * 240 * 2> menu_encoded;
std::optional<ui::RetainedMenu> retained_menu;
int menu_width = 0, menu_height = 0;

int l_ui_menu_close(lua_State *) {
    retained_menu.reset();
    return 0;
}

int l_ui_menu_open(lua_State *L) {
    const int width = bounded_integer_argument(L, 1, 96, 320);
    const int height = bounded_integer_argument(L, 2, 96, 240);
    luaL_checktype(L, 3, LUA_TSTRING);
    size_t title_length = 0;
    const char *title = lua_tolstring(L, 3, &title_length);
    luaL_checktype(L, 4, LUA_TTABLE);
    const size_t count = lua_objlen(L, 4);
    if (!count || count > ui::RetainedMenu::max_entries) return luaL_error(L, "Unsupported menu length");
    const bool described = !lua_isnoneornil(L, 5);
    if (described) {
        luaL_checktype(L, 5, LUA_TTABLE);
        if (lua_objlen(L, 5) != count) return luaL_error(L, "Menu descriptions must match the entries");
    }
    std::array<std::string_view, ui::RetainedMenu::max_entries> labels{};
    std::array<std::string_view, ui::RetainedMenu::max_entries> descriptions{};
    for (size_t i = 0; i < count; ++i) {
        lua_rawgeti(L, 4, static_cast<int>(i + 1));
        luaL_checktype(L, -1, LUA_TSTRING);
        size_t length = 0;
        const char *label = lua_tolstring(L, -1, &length);
        labels[i] = {label, length};
        lua_pop(L, 1);
        if (described) {
            lua_rawgeti(L, 5, static_cast<int>(i + 1));
            luaL_checktype(L, -1, LUA_TSTRING);
            const char *description = lua_tolstring(L, -1, &length);
            descriptions[i] = {description, length};
            lua_pop(L, 1);
        }
    }
    retained_menu.reset();
    retained_menu.emplace(width, height, menu_pixels, std::string_view(title, title_length),
                          std::span<const std::string_view>(labels.data(), count),
                          std::span<const std::string_view>(descriptions.data(), described ? count : 0));
    menu_width = width;
    menu_height = height;
    lua_pushboolean(L, retained_menu->status() == NPS_RETAINED_READY);
    return 1;
}

int l_ui_menu_frame(lua_State *L) {
    if (!retained_menu || retained_menu->status() != NPS_RETAINED_READY) {
        lua_pushnil(L);
        lua_pushboolean(L, false);
        return 2;
    }
    const bool changed = retained_menu->render();
    if (retained_menu->status() != NPS_RETAINED_READY) {
        lua_pushnil(L);
        lua_pushboolean(L, false);
        return 2;
    }
    if (changed) {
        const size_t bytes = ui::ti_image_size(menu_width, menu_height);
        if (!ui::encode_ti_image(menu_width, menu_height, retained_menu->pixels(), {menu_encoded.data(), bytes})) {
            lua_pushnil(L);
            lua_pushboolean(L, false);
            return 2;
        }
        lua_pushlstring(L, menu_encoded.data(), bytes);
    } else lua_pushnil(L);
    lua_pushboolean(L, true);
    return 2;
}

int l_ui_menu_select(lua_State *L) {
    const int index = bounded_integer_argument(L, 1, 1, static_cast<int>(ui::RetainedMenu::max_entries));
    lua_pushboolean(L, retained_menu && retained_menu->select(static_cast<size_t>(index - 1)));
    return 1;
}

int l_ui_menu_scroll(lua_State *L) {
    const int pixels = bounded_integer_argument(L, 1, -240, 240);
    const bool accepted = retained_menu && retained_menu->scroll(pixels);
    lua_pushboolean(L, accepted);
    if (accepted) lua_pushinteger(L, static_cast<lua_Integer>(retained_menu->selected() + 1));
    else lua_pushnil(L);
    return 2;
}
#if NPS_RETAINED_FAULT_INJECTION
int l_ui_menu_test_failure(lua_State *L) {
    luaL_checktype(L, 1, LUA_TSTRING);
    size_t length = 0;
    const char *text = lua_tolstring(L, 1, &length);
    const std::string_view mode(text, length);
    if (mode == "resource") nps_retained_test_fail_after(0);
    else if (mode == "assert") nps_retained_test_assert_next();
    else return luaL_error(L, "Expected resource or assert failure");
    return 0;
}

int l_ui_menu_test_status(lua_State *L) {
    lua_pushinteger(L, nps_retained_failure());
    lua_pushinteger(L, static_cast<lua_Integer>(nps_retained_test_attempts()));
    return 2;
}
#endif
#endif

int calculus_into(lua_State *L) {
    size_t size = 0;
    const char *text = luaL_checklstring(L, 1, &size);
    const NumericMode mode = mode_argument(L, 3);
    std::string variable;
    if (!variable_argument(L, 2, &variable)) return 2;
    GcPause paused(L);
    Arena arena;
    Derivation derivation;
    derivation.request.original_expression.assign(text, size);
    derivation.request.numeric_mode = mode;
    const Command command = parse_command(arena, derivation.request.original_expression, variable);
    CalculusResult result;
    if (GiacBackend::available(L)) {
        GiacBackend backend(L);
        result = calculus_walkthrough(arena, derivation, command, interactive_budget(), &backend);
    } else {
        result = calculus_walkthrough(arena, derivation, command, interactive_budget());
    }
    std::string normalization;
    std::string why;
    if (!prepare_normalized_expression(arena, derivation.context, &normalization, &why))
        return expression_resource_failure(L, why);
    derivation.context.normalized_expression = normalization;
    const std::string answer = result.value != kNoNode ? print(arena, result.value)
        : result.infinity > 0 ? "+infinity" : result.infinity < 0 ? "-infinity"
        : result.does_not_exist ? "does not exist" : "";
    // The engine answered, which is what every sibling producer's solved means. Whether the answer
    // was then checked is the status beside it.
    const bool solved = result.outcome == CalculusOutcome::Evaluated ||
                        result.outcome == CalculusOutcome::InfiniteLimit ||
                        result.outcome == CalculusOutcome::DoesNotExist;
    lua_newtable(L);
    set_field(L, "mode", command_kind_name(command.kind));
    set_field(L, "outcome", calculus_outcome_name(result.outcome));
    set_field(L, "status", derivation_status_name(result.status));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", solved);
    set_field(L, "has_result", !answer.empty());
    set_field(L, "answer_only", result.answer_only);
    set_field(L, "numeric_mode", numeric_mode_name(mode));
    set_field(L, "request_expression", derivation.request.original_expression);
    set_expression_context(L, derivation.context);
    if (!answer.empty()) set_field(L, "result", answer);
    if (command.kind == CommandKind::Limit && !answer.empty())
        set_field(L, "limit_exists", !result.does_not_exist);
    // The tangent family reports what the line was built from and whether the relation it states is
    // an equality. A linearization is an approximation away from the point and says so here.
    if (result.slope != kNoNode) set_field(L, "tangent_slope", print(arena, result.slope));
    if (result.point_value != kNoNode)
        set_field(L, "tangent_point_value", print(arena, result.point_value));
    if (command.kind == CommandKind::Tangent || command.kind == CommandKind::Linearize) {
        set_field(L, "approximation", result.approximate);
        set_field(L, "relation", result.approximate ? "approximately equal" : "equal");
    }
    if (result.infinity != 0) set_field(L, "infinite_limit", true);
    const ResultForm backend_form =
        result.backend_attempted ? result_form(result.backend_result) : ResultForm::NoResult;
    set_field(L, "result_form",
              result_form_name(primary_result_form(!answer.empty(), result.answer_only,
                                                   result.status, backend_form)));
    set_field(L, "giac_tag", result.backend_attempted ? tag_name(result.backend_result.tag) : "unavailable");
    set_field(L, "giac_form", result_form_name(backend_form));
    if (result.backend_compared) set_field(L, "agrees", result.agrees);
    if (!result.backend_result.detail.empty()) set_field(L, "giac_detail", result.backend_result.detail);
    // giac_tag describes the first reply and stays truthful, so the comparison needs its own fields
    // or stepVerdict at nps_v4.lua:2695 reports an exact answer for a comparison that never finished.
    if (result.comparison_attempted) {
        set_field(L, "giac_compare_tag", tag_name(result.comparison_result.tag));
        set_field(L, "giac_compare_form", result_form_name(result_form(result.comparison_result)));
        set_field(L, "giac_compare_raw", result.comparison_result.raw);
        if (!result.comparison_result.detail.empty())
            set_field(L, "giac_compare_detail", result.comparison_result.detail);
    }
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    if (derivation.size() == 0) push_no_steps(L);
    else push_steps(L, arena, derivation);
    return 1;
}

// A form refused or left unconfirmed returns nil so the shell falls back to Giac as before.
int separable_into(lua_State *L) {
    size_t size = 0;
    const char *text = luaL_checklstring(L, 1, &size);
    const NumericMode mode = mode_argument(L, 3);
    std::string variable;
    if (!variable_argument(L, 2, &variable)) return 2;
    GcPause paused(L);
    Arena arena;
    Derivation derivation;
    derivation.request.original_expression.assign(text, size);
    derivation.request.numeric_mode = mode;
    const Command command = parse_command(arena, derivation.request.original_expression, variable);
    const SeparableResult result =
        solve_separable(arena, derivation, command, interactive_budget());
    if (result.outcome == SeparableOutcome::UnsupportedForm ||
        result.outcome == SeparableOutcome::Refused) {
        lua_pushnil(L);
        return 1;
    }
    std::string normalization;
    std::string why;
    if (!prepare_normalized_expression(arena, derivation.context, &normalization, &why))
        return expression_resource_failure(L, why);
    derivation.context.normalized_expression = normalization;
    const std::string answer = result.solution != kNoNode ? print(arena, result.solution) : "";
    lua_newtable(L);
    set_field(L, "mode", command_kind_name(command.kind));
    set_field(L, "outcome", separable_outcome_name(result.outcome));
    set_field(L, "status", derivation_status_name(result.status));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", result.outcome == SeparableOutcome::Solved);
    set_field(L, "has_result", !answer.empty());
    set_field(L, "answer_only", false);
    set_field(L, "numeric_mode", numeric_mode_name(mode));
    set_field(L, "request_expression", derivation.request.original_expression);
    set_expression_context(L, derivation.context);
    if (!answer.empty()) {
        set_field(L, "result", answer);
        set_field(L, "explicit_solution", result.explicit_solution);
    }
    if (result.constant != kNoNode) set_field(L, "constant", print(arena, result.constant));
    set_field(L, "result_form",
              result_form_name(primary_result_form(!answer.empty(), false, result.status,
                                                   ResultForm::NoResult)));
    set_field(L, "giac_tag", "unavailable");
    set_field(L, "giac_form", result_form_name(ResultForm::NoResult));
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    if (derivation.size() == 0) push_no_steps(L);
    else push_steps(L, arena, derivation);
    return 1;
}

int l_walkthrough(lua_State *L) {
    size_t text_size = 0;
    const char *text_data = luaL_checklstring(L, 1, &text_size);
    const char *default_variable = scalar_string_argument(L, 2, "x");
    const NumericMode numeric_mode = mode_argument(L, 3);
    CommandKind kind;
    {
        Arena arena;
        const Command command = parse_command(arena, std::string(text_data, text_size), default_variable);
        kind = command.kind;
        if (command.status == CommandStatus::Unhandled) {
            lua_pushnil(L);
            return 1;
        }
        const bool unsupported_mode = command.status == CommandStatus::Ready &&
            numeric_mode == NumericMode::Decimal && kind != CommandKind::Solve &&
            kind != CommandKind::Differentiate && kind != CommandKind::Integrate;
        if (command.status != CommandStatus::Ready || unsupported_mode) {
            const bool resource = command.status == CommandStatus::ResourceExceeded;
            const bool unsupported = command.status == CommandStatus::Unsupported || unsupported_mode;
            typed_failure(L, resource ? "resource exceeded" : unsupported ? "unsupported form" : "invalid input",
                          resource ? "resource limit reached" : unsupported ? "unsupported" : "invalid input",
                          unsupported_mode ? "this walkthrough supports exact numeric mode only" : command.detail);
            set_field(L, "mode", command_kind_name(kind));
            set_field(L, "numeric_mode", numeric_mode_name(numeric_mode));
            lua_pushvalue(L, 1);
            lua_setfield(L, -2, "request_expression");
            return 1;
        }
        if (kind != CommandKind::Limit && kind != CommandKind::DefiniteIntegral &&
            kind != CommandKind::Tangent && kind != CommandKind::Linearize &&
            kind != CommandKind::Desolve) {
        lua_settop(L, 3);
        lua_pushvalue(L, 1);
        lua_pushlstring(L, command.operand_text.data(), command.operand_text.size());
        lua_replace(L, 1);
        lua_pushlstring(L, command.variable_name.data(), command.variable_name.size());
        lua_replace(L, 2);
        }
    }
    if (kind == CommandKind::Limit || kind == CommandKind::DefiniteIntegral ||
        kind == CommandKind::Tangent || kind == CommandKind::Linearize)
        return calculus_into(L);
    if (kind == CommandKind::Desolve)
        return separable_into(L);
    int count;
    if (kind == CommandKind::Solve)
        count = solve_into(L, true);
    else if (kind == CommandKind::Differentiate)
        count = differentiate_into(L, true);
    else if (kind == CommandKind::Integrate)
        count = integrate_into(L, true);
    else if (kind == CommandKind::Integer)
        count = integer_into(L);
    else if (kind == CommandKind::Ref || kind == CommandKind::Rref || kind == CommandKind::Determinant)
        count = matrix_into(L, kind);
    else
        count = rewrite_into(L, kind);
    if (count != 1 || !lua_istable(L, -1)) {
        const char *detail = lua_tostring(L, -1);
        typed_failure(L, "invalid input", "invalid input", detail ? detail : "the command could not be read");
    }
    set_field(L, "mode", command_kind_name(kind));
    lua_pushvalue(L, 4);
    lua_setfield(L, -2, "request_expression");
    return 1;
}

// Kinematics leans on Giac twice: inside the solve it rearranges the symbolic equation (and checks
// its own form against the linear solver's value), and afterwards it solves the substituted
// equation independently as the cross-check. Both are the same backend on this frame, because the
// second must know whether the first gave up, and both calls finish before the table exists.
int kinematics_into(lua_State *L, bool cross) {
    const char *text = scalar_string_argument(L, 1);
    GcPause paused(L);

    KinematicsProblem problem;
    std::string why;
    if (!parse_kinematics(text, &problem, &why))
        return typed_failure(L, "invalid input", "invalid input", why);

    Arena arena;
    Derivation d;
    KinematicsResult r;
    const Budget budget = interactive_budget();
    // Outside the branch now, because the cross-check below has to ask the same object the solve
    // asked rather than a second Giac that never heard the first one give up.
    GiacBackend backend(L);
    const bool backed = cross && GiacBackend::available(L);
    r = solve_kinematics(arena, d, problem, budget, backed ? &backend : nullptr);

    CrossCheck c;
    if (cross && r.value != kNoNode)
        c = ask_giac(L, arena, r.status, Op::Solve, r.substituted, r.unknown, r.value, true, nullptr,
                     backed ? &backend : nullptr);
    else if (cross && answer_only_allowed(r.status) && r.substituted != kNoNode &&
             r.unknown != kNoNode)
        c = ask_giac(L, arena, r.status, Op::Solve, r.substituted, r.unknown, kNoNode, true, nullptr,
                     backed ? &backend : nullptr);
    const bool answer_only = answer_only_allowed(r.status) && c.has_answer;
    const bool has_result = r.outcome == KinematicsOutcome::Solved ||
                            r.outcome == KinematicsOutcome::NoSolution || answer_only;
    const DerivationStatus status = cross && r.outcome == KinematicsOutcome::Solved
                                        ? cross_checked_status(r.status, c)
                                        : r.status;
    d.context.derivation_status = status;

    const std::string answer =
        r.outcome == KinematicsOutcome::Solved
            ? problem.unknown + " = " + r.value_text + " " + r.unit_text
            : std::string();
    const std::string equation = r.equation == kNoNode ? std::string() : print(arena, r.equation);
    const std::string isolated =
        r.isolated == kNoNode ? std::string()
                              : problem.unknown + " = " + print(arena, r.isolated);
    const std::string assumptions = joined(d.context.active_assumptions);
    const std::string detail = answer_only && !r.answer_candidate_detail.empty()
                                   ? r.detail + "; " + r.answer_candidate_detail
                                   : r.detail;

    lua_newtable(L);
    set_field(L, "outcome", kinematics_outcome_name(r.outcome));
    set_field(L, "detail", detail);
    set_field(L, "solved", r.outcome == KinematicsOutcome::Solved);
    set_field(L, "has_result", has_result);
    set_field(L, "answer_only", answer_only);
    set_field(L, "status", derivation_status_name(status));
    set_field(L, "numeric_mode", numeric_mode_name(d.context.numeric_mode));
    if (!answer.empty()) {
        set_field(L, "result", answer);
        set_field(L, "value", r.value_text);
        set_field(L, "unit", r.unit_text);
        set_precision(L, r.precision);
    } else if (answer_only) {
        set_field(L, "result", problem.unknown + " = " + c.value + " " + r.unit_text);
        set_field(L, "value", c.value);
        set_field(L, "unit", r.unit_text);
    }
    if (!equation.empty())
        set_field(L, "equation", equation);
    if (!isolated.empty())
        set_field(L, "rearranged", isolated);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    if (!r.answer_candidate_detail.empty())
        set_field(L, "answer_candidate_detail", r.answer_candidate_detail);
    if (cross) {
        set_cross_check(L, c, r.value != kNoNode || answer_only);
        if (!answer_only)
            set_field(L, "giac_method", "solved the substituted equation");
    }
    set_cost(L, arena, d, r.cost, c.calls + r.cost.backend_calls);
    // Whether there is a walkthrough, not where the answer came from. A backend answer is segregated
    // from the walkthrough rather than a substitute for it, so a refusal that got partway shows the
    // prefix STEP-025 kept and the answer beside it. Only a refusal that recorded nothing has none.
    if (d.size() == 0)
        push_no_steps(L);
    else
        push_steps(L, arena, d);
    return 1;
}

int l_kinematics(lua_State *L) { return kinematics_into(L, true); }

int l_kinematics_local(lua_State *L) { return kinematics_into(L, false); }

int l_catch_up(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        return typed_failure(L, "invalid problem", "invalid input",
                             "catch_up input must be a table");
    }

    CatchUpProblem problem;
    std::string why;
    if (!field_value(L, 1, "first", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!catch_up_body_table(L, -1, &problem.first, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input", "first: " + why);
    }
    lua_pop(L, 1);
    if (!field_value(L, 1, "second", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!catch_up_body_table(L, -1, &problem.second, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input", "second: " + why);
    }
    lua_pop(L, 1);

    GcPause paused(L);
    Arena arena;
    Derivation derivation;
    const CatchUpResult result =
        solve_catch_up(arena, derivation, problem, interactive_budget());
    const bool has_result = catch_up_has_result(result.outcome);

    lua_newtable(L);
    set_field(L, "outcome", catch_up_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", result.outcome == CatchUpOutcome::Solved);
    set_field(L, "has_result", has_result);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    if (has_result)
        set_field(L, "result", result.detail);
    if (result.outcome == CatchUpOutcome::Solved) {
        set_precision(L, result.event_time.precision);
        set_quantity_result(L, "event_time", result.event_time, result.event_time_text,
                            result.time_unit_text);
        set_quantity_result(L, "event_position", result.event_position,
                            result.event_position_text, result.position_unit_text);
    }
    if (!result.shared_active_start.unit.text.empty()) {
        set_quantity_result(L, "shared_active_start", result.shared_active_start,
                            rational_text(result.shared_active_start.value),
                            result.shared_active_start.unit.text);
    }
    if (result.equation != kNoNode)
        set_field(L, "equation", print(arena, result.equation));
    if (result.active_domain != kNoNode)
        set_field(L, "active_domain", print(arena, result.active_domain));
    if (result.substituted != kNoNode)
        set_field(L, "substituted", print(arena, result.substituted));
    const std::string assumptions = joined(derivation.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    push_steps(L, arena, derivation);
    return 1;
}

int l_unit_conversion(lua_State *L) {
    const char *source_text = scalar_string_argument(L, 1);
    const char *target_text = scalar_string_argument(L, 2);
    GcPause paused(L);

    UnitConversionProblem problem;
    const UnitConversionParseResult parsed =
        parse_unit_conversion_problem(source_text, target_text, &problem);
    if (!parsed.ok())
        return typed_failure(L, unit_conversion_parse_outcome_name(parsed.outcome), "invalid input",
                             parsed.detail);

    Arena arena;
    Derivation d;
    const UnitConversionResult r =
        solve_unit_conversion(arena, d, problem, interactive_budget());

    lua_newtable(L);
    set_field(L, "outcome", unit_conversion_outcome_name(r.outcome));
    set_field(L, "detail", r.detail);
    set_field(L, "solved", r.outcome == UnitConversionOutcome::Converted);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(r.status));
    if (r.has_value) {
        set_field(L, "result", r.value_text);
        set_field(L, "value", r.value_text);
        set_field(L, "exact_value", rational_text(r.value.value));
        set_field(L, "unit", r.value.unit.text.empty() ? si_unit_text(r.value.unit.dimension)
                                                       : r.value.unit.text);
        set_precision(L, r.value.precision);
        set_field(L, "source_to_si_factor", rational_text(r.source_to_si_factor));
        set_field(L, "si_to_target_factor", rational_text(r.si_to_target_factor));
        set_field(L, "combined_factor", rational_text(r.combined_factor));
    }
    const std::string assumptions = joined(d.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, d, r.cost, r.cost.backend_calls);
    push_steps(L, arena, d);
    return 1;
}

bool density_variable(std::string_view name, DensityVariable *variable) {
    if (name == "mass")
        *variable = DensityVariable::Mass;
    else if (name == "volume")
        *variable = DensityVariable::Volume;
    else if (name == "density")
        *variable = DensityVariable::Density;
    else
        return false;
    return true;
}

int l_density(lua_State *L) {
    const char *unknown_text = scalar_string_argument(L, 1);
    const char *first_variable_text = scalar_string_argument(L, 2);
    const char *first_quantity_text = scalar_string_argument(L, 3);
    const char *second_variable_text = scalar_string_argument(L, 4);
    const char *second_quantity_text = scalar_string_argument(L, 5);
    GcPause paused(L);

    DensityProblem problem;
    DensityKnown first;
    DensityKnown second;
    std::string why;
    bool parsed = density_variable(unknown_text, &problem.unknown);
    if (!parsed)
        why = "unknown density variable " + std::string(unknown_text);
    if (parsed && !density_variable(first_variable_text, &first.variable)) {
        parsed = false;
        why = "unknown density variable " + std::string(first_variable_text);
    }
    if (parsed && !parse_quantity(first_quantity_text, &first.quantity, &why))
        parsed = false;
    if (parsed && !density_variable(second_variable_text, &second.variable)) {
        parsed = false;
        why = "unknown density variable " + std::string(second_variable_text);
    }
    if (parsed && !parse_quantity(second_quantity_text, &second.quantity, &why))
        parsed = false;
    if (!parsed)
        return typed_failure(L, "invalid input", "invalid input", why);
    problem.knowns.push_back(std::move(first));
    problem.knowns.push_back(std::move(second));

    Arena arena;
    Derivation d;
    const DensityResult r = solve_density(arena, d, problem, interactive_budget());

    lua_newtable(L);
    set_field(L, "outcome", density_outcome_name(r.outcome));
    set_field(L, "detail", r.detail);
    set_field(L, "solved", r.outcome == DensityOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(r.status));
    set_field(L, "unknown", density_variable_name(problem.unknown));
    if (r.outcome == DensityOutcome::Solved) {
        set_field(L, "result", std::string(density_variable_name(problem.unknown)) + " = " +
                                   r.value_text + " " + r.unit_text);
        set_field(L, "value", r.value_text);
        set_field(L, "exact_value", rational_text(r.quantity.value));
        set_field(L, "unit", r.unit_text);
        set_precision(L, r.quantity.precision);
    }
    if (r.equation != kNoNode)
        set_field(L, "equation", print(arena, r.equation));
    if (r.substituted != kNoNode)
        set_field(L, "substituted", print(arena, r.substituted));
    const std::string assumptions = joined(d.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, d, r.cost, r.cost.backend_calls);
    push_steps(L, arena, d);
    return 1;
}

bool optics_relation(std::string_view name, OpticsRelation *relation) {
    if (name == "refraction")
        *relation = OpticsRelation::Refraction;
    else if (name == "thin lens")
        *relation = OpticsRelation::ThinLens;
    else if (name == "spherical mirror")
        *relation = OpticsRelation::SphericalMirror;
    else if (name == "two-slit interference")
        *relation = OpticsRelation::DoubleSlit;
    else if (name == "single-slit diffraction")
        *relation = OpticsRelation::SingleSlit;
    else
        return false;
    return true;
}

bool optics_variable(std::string_view name, OpticsVariable *variable) {
    if (name == "incident index")
        *variable = OpticsVariable::IndexIncident;
    else if (name == "incident sine")
        *variable = OpticsVariable::SineIncident;
    else if (name == "transmitted index")
        *variable = OpticsVariable::IndexTransmitted;
    else if (name == "transmitted sine")
        *variable = OpticsVariable::SineTransmitted;
    else if (name == "focal length")
        *variable = OpticsVariable::FocalLength;
    else if (name == "object distance")
        *variable = OpticsVariable::ObjectDistance;
    else if (name == "image distance")
        *variable = OpticsVariable::ImageDistance;
    else if (name == "slit spacing")
        *variable = OpticsVariable::SlitSpacing;
    else if (name == "fringe sine")
        *variable = OpticsVariable::SineFringe;
    else if (name == "fringe order")
        *variable = OpticsVariable::FringeOrder;
    else if (name == "wavelength")
        *variable = OpticsVariable::Wavelength;
    else
        return false;
    return true;
}

// The relation carries three or four variables, so the third known pair is optional rather than a
// separate entry point per relation.
int l_optics(lua_State *L) {
    const char *relation_text = scalar_string_argument(L, 1);
    const char *unknown_text = scalar_string_argument(L, 2);
    const char *names[3] = {scalar_string_argument(L, 3), scalar_string_argument(L, 5),
                            scalar_string_argument(L, 7, "")};
    const char *values[3] = {scalar_string_argument(L, 4), scalar_string_argument(L, 6),
                             scalar_string_argument(L, 8, "")};
    GcPause paused(L);

    OpticsProblem problem;
    std::string why;
    bool parsed = optics_relation(relation_text, &problem.relation);
    if (!parsed)
        why = "unknown optics relation " + std::string(relation_text);
    if (parsed && !optics_variable(unknown_text, &problem.unknown)) {
        parsed = false;
        why = "unknown optics variable " + std::string(unknown_text);
    }
    for (size_t i = 0; parsed && i < 3; ++i) {
        if (*names[i] == '\0' && *values[i] == '\0')
            continue;
        OpticsKnown known;
        if (!optics_variable(names[i], &known.variable)) {
            parsed = false;
            why = "unknown optics variable " + std::string(names[i]);
            break;
        }
        if (!parse_quantity(values[i], &known.quantity, &why)) {
            parsed = false;
            break;
        }
        problem.knowns.push_back(std::move(known));
    }
    if (!parsed)
        return typed_failure(L, "invalid input", "invalid input", why);

    Arena arena;
    Derivation d;
    const OpticsResult r = solve_optics(arena, d, problem, interactive_budget());

    lua_newtable(L);
    set_field(L, "outcome", optics_outcome_name(r.outcome));
    set_field(L, "detail", r.detail);
    set_field(L, "solved", r.outcome == OpticsOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(r.status));
    set_field(L, "relation", optics_relation_name(problem.relation));
    set_field(L, "unknown", optics_variable_name(problem.unknown));
    // The convention is what the sign of the answer is read under, so it travels with the answer
    // rather than staying inside the step it was declared in.
    set_field(L, "convention", r.convention);
    if (r.outcome == OpticsOutcome::Solved) {
        // si_unit_text spells a dimensionless SI unit as "1", which is correct engine
        // notation but reads as noise on a dimensionless answer like a sine.
        const std::string display_unit = r.unit_text == "1" ? std::string() : r.unit_text;
        std::string answer = std::string(optics_variable_name(problem.unknown)) + " = " +
                             r.value_text;
        if (!display_unit.empty())
            answer += " " + display_unit;
        set_field(L, "result", answer);
        set_field(L, "value", r.value_text);
        set_field(L, "exact_value", rational_text(r.quantity.value));
        set_field(L, "unit", display_unit);
        set_precision(L, r.quantity.precision);
    }
    if (r.has_magnification)
        set_field(L, "magnification", r.magnification_text);
    if (r.has_critical_sine)
        set_field(L, "critical_sine", r.critical_sine_text);
    if (r.equation != kNoNode)
        set_field(L, "equation", print(arena, r.equation));
    if (r.substituted != kNoNode)
        set_field(L, "substituted", print(arena, r.substituted));
    const std::string assumptions = joined(d.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, d, r.cost, r.cost.backend_calls);
    push_steps(L, arena, d);
    return 1;
}

int l_vector_addition(lua_State *L) {
    const char *first_text = scalar_string_argument(L, 1);
    const char *second_text = scalar_string_argument(L, 2);
    GcPause paused(L);

    VectorAdditionProblem problem;
    std::string why;
    if (!parse_vector(first_text, &problem.first, &why) ||
        !parse_vector(second_text, &problem.second, &why))
        return typed_failure(L, "invalid input", "invalid input", why);

    Arena arena;
    Derivation d;
    const VectorAdditionResult r =
        solve_vector_addition(arena, d, problem, interactive_budget());

    lua_newtable(L);
    set_field(L, "outcome", vector_addition_outcome_name(r.outcome));
    set_field(L, "detail", r.detail);
    set_field(L, "solved", r.outcome == VectorAdditionOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(r.status));
    if (r.has_value) {
        set_field(L, "result", r.value_text);
        set_field(L, "value", r.value_text);
        set_precision(L, r.value.precision);
    }
    const std::string assumptions = joined(d.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, d, r.cost, r.cost.backend_calls);
    push_steps(L, arena, d);
    return 1;
}

int l_vector_cross(lua_State *L) {
    const char *first_text = scalar_string_argument(L, 1);
    const char *second_text = scalar_string_argument(L, 2);
    GcPause paused(L);

    VectorCrossProblem problem;
    std::string why;
    if (!parse_vector(first_text, &problem.first, &why) ||
        !parse_vector(second_text, &problem.second, &why))
        return typed_failure(L, "invalid input", "invalid input", why);

    Arena arena;
    Derivation d;
    const VectorCrossResult r = solve_vector_cross(arena, d, problem, interactive_budget());

    lua_newtable(L);
    set_field(L, "outcome", vector_cross_outcome_name(r.outcome));
    set_field(L, "detail", r.detail);
    set_field(L, "solved", r.outcome == VectorCrossOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(r.status));
    if (r.has_value) {
        set_field(L, "result", r.value_text);
        set_field(L, "value", r.value_text);
        set_precision(L, r.value.precision);
    }
    const std::string assumptions = joined(d.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, d, r.cost, r.cost.backend_calls);
    push_steps(L, arena, d);
    return 1;
}

constexpr char kRelativeMotionGiacMethod[] = "Giac Simplify and local canonical comparison";

int relative_motion_into(lua_State *L, bool cross) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        return typed_failure(L, "invalid problem", "invalid input",
                             "relative motion input must be a table");
    }

    RelativeMotionProblem problem;
    std::string why;
    if (!string_field(L, 1, "subject_name", &problem.subject_name, &why) ||
        !string_field(L, 1, "reference_name", &problem.reference_name, &why)) {
        return typed_failure(L, "invalid problem", "invalid input", why);
    }
    if (!field_value(L, 1, "subject_velocity", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!vector_table(L, -1, &problem.subject_velocity, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input", "subject_velocity: " + why);
    }
    lua_pop(L, 1);
    if (!field_value(L, 1, "reference_velocity", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!vector_table(L, -1, &problem.reference_velocity, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input",
                             "reference_velocity: " + why);
    }
    lua_pop(L, 1);

    GcPause paused(L);
    Arena arena;
    Derivation derivation;
    RelativeMotionResult result;
    const Budget budget = interactive_budget();
    const bool backend_available = cross && GiacBackend::available(L);
    if (backend_available) {
        GiacBackend backend(L);
        result = solve_relative_motion(arena, derivation, problem, budget, &backend);
    } else {
        result = solve_relative_motion(arena, derivation, problem, budget, nullptr);
    }

    bool component_check_seen = false;
    bool component_check_failed = false;
    bool component_check_inconclusive = false;
    bool component_i_passed = false;
    bool component_j_passed = false;
    for (size_t index = 0; index < derivation.size(); ++index) {
        const Step &step = derivation.at(static_cast<StepId>(index));
        for (const VerificationRecord &verification : step.verifications) {
            if (verification.method != kRelativeMotionGiacMethod)
                continue;
            component_check_seen = true;
            if (verification.outcome == VerificationOutcome::Failed)
                component_check_failed = true;
            else if (verification.outcome == VerificationOutcome::Inconclusive)
                component_check_inconclusive = true;
            else if (verification.outcome == VerificationOutcome::Passed &&
                     step.rule_id == "physics.relative-motion.component-i")
                component_i_passed = true;
            else if (verification.outcome == VerificationOutcome::Passed &&
                     step.rule_id == "physics.relative-motion.component-j")
                component_j_passed = true;
        }
    }
    CrossCheck component_check;
    bool publish_component_check = cross && !backend_available;
    if (component_check_seen) {
        publish_component_check = true;
        component_check.tag = "incomplete";
        // A backend that could not answer leaves compared false, so agrees is absent and the status
        // helper above reads that as solved but unchecked. The tag still names what Giac did.
        if (component_check_inconclusive) {
            component_check.tag = "backend failure";
        } else if (component_check_failed &&
                   result.outcome == RelativeMotionOutcome::VerificationFailed) {
            component_check.tag = "exact";
            component_check.compared = true;
            component_check.agrees = false;
        } else if (component_i_passed && component_j_passed &&
                   result.outcome == RelativeMotionOutcome::Solved) {
            component_check.tag = "exact";
            component_check.compared = true;
            component_check.agrees = true;
        }
    }

    lua_newtable(L);
    set_field(L, "outcome", relative_motion_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", result.outcome == RelativeMotionOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    if (publish_component_check) {
        set_cross_check(L, component_check, false);
        if (component_check_seen)
            set_field(L, "giac_method", kRelativeMotionGiacMethod);
    }
    if (result.has_value) {
        set_field(L, "result", result.value_text);
        set_field(L, "value", result.value_text);
        set_field(L, "exact_x", rational_text(result.velocity.x));
        set_field(L, "exact_y", rational_text(result.velocity.y));
        set_field(L, "unit", result.velocity.unit.text);
        set_field(L, "frame", result.velocity.frame.name);
        set_field(L, "direction", relative_direction_name(result.direction));
        set_field(L, "interpretation", result.interpretation);
        set_precision(L, result.velocity.precision);
    }
    if (result.equation != kNoNode)
        set_field(L, "equation", print(arena, result.equation));
    if (result.substituted != kNoNode)
        set_field(L, "substituted", print(arena, result.substituted));
    const std::string assumptions = joined(derivation.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    push_steps(L, arena, derivation);
    return 1;
}

int l_relative_motion(lua_State *L) { return relative_motion_into(L, true); }

int l_relative_motion_local(lua_State *L) { return relative_motion_into(L, false); }

void set_vector_expression(lua_State *L, const char *key, const Arena &arena,
                           const VectorExpr &value) {
    lua_pushstring(L, key);
    lua_newtable(L);
    set_field(L, "x", print(arena, value.x));
    set_field(L, "y", print(arena, value.y));
    if (value.rank == 3)
        set_field(L, "z", print(arena, value.z));
    set_field(L, "rank", static_cast<int>(value.rank));
    set_field(L, "frame", value.frame.name);
    set_field(L, "unit", value.unit.text);
    set_precision(L, value.precision);
    lua_settable(L, -3);
}

void set_magnitude_angle_expression(lua_State *L, const char *key, const Arena &arena,
                                    const MagnitudeAngleExpr &value) {
    lua_pushstring(L, key);
    lua_newtable(L);
    set_field(L, "magnitude", print(arena, value.magnitude));
    set_field(L, "angle", print(arena, value.angle));
    if (value.polar_angle != kNoNode)
        set_field(L, "polar_angle", print(arena, value.polar_angle));
    set_field(L, "rank", static_cast<int>(value.rank));
    set_field(L, "frame", value.frame.name);
    set_field(L, "unit", value.unit.text);
    set_field(L, "angle_unit", angle_unit_name(value.angle_unit));
    set_precision(L, value.precision);
    lua_settable(L, -3);
}

int set_vector_components_result(lua_State *L, const Arena &arena, const Derivation &derivation,
                                 const VectorComponentsResult &result) {
    lua_newtable(L);
    set_field(L, "outcome", vector_components_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", result.outcome == VectorComponentsOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    set_field(L, "has_components", result.has_components);
    set_field(L, "has_polar", result.has_polar);
    if (result.has_components)
        set_vector_expression(L, "components", arena, result.components);
    if (result.has_polar)
        set_magnitude_angle_expression(L, "polar", arena, result.polar);
    if (result.has_components)
        set_precision(L, result.components.precision);
    else if (result.has_polar)
        set_precision(L, result.polar.precision);
    const std::string assumptions = joined(derivation.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    push_steps(L, arena, derivation);
    return 1;
}

int work_into(lua_State *L, bool cross) {
    if (lua_type(L, 1) != LUA_TTABLE)
        return typed_failure(L, "invalid problem", "invalid input", "work input must be a table");

    WorkProblem problem;
    std::string why;
    if (!field_value(L, 1, "force", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!vector_table(L, -1, &problem.force, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input", "force: " + why);
    }
    lua_pop(L, 1);
    if (!field_value(L, 1, "displacement", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!vector_table(L, -1, &problem.displacement, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input", "displacement: " + why);
    }
    lua_pop(L, 1);

    std::string profile;
    if (!string_field(L, 1, "force_profile", &profile, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (profile == "constant")
        problem.force_profile = WorkForceProfile::Constant;
    else if (profile == "variable")
        problem.force_profile = WorkForceProfile::Variable;
    else if (profile == "unspecified")
        problem.force_profile = WorkForceProfile::Unspecified;
    else
        return typed_failure(L, "invalid problem", "invalid input",
                             "force_profile must be constant, variable or unspecified");

    GcPause paused(L);
    Arena arena;
    Derivation derivation;
    WorkResult result;
    const Budget budget = interactive_budget();
    if (cross && GiacBackend::available(L)) {
        GiacBackend backend(L);
        result = solve_work(arena, derivation, problem, budget, &backend);
    } else {
        result = solve_work(arena, derivation, problem, budget, nullptr);
    }

    lua_newtable(L);
    set_field(L, "outcome", work_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", result.outcome == WorkOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    if (result.has_value) {
        set_field(L, "result", result.value_text + " " + result.unit_text);
        set_field(L, "value", result.value_text);
        set_field(L, "exact_value", rational_text(result.quantity.value));
        set_field(L, "unit", result.unit_text);
        set_field(L, "sign", work_sign_name(result.sign));
        set_field(L, "interpretation", result.interpretation);
        set_precision(L, result.quantity.precision);
    }
    if (result.equation != kNoNode)
        set_field(L, "equation", print(arena, result.equation));
    if (result.substituted != kNoNode)
        set_field(L, "substituted", print(arena, result.substituted));
    if (result.backend_value != kNoNode)
        set_field(L, "backend_value", print(arena, result.backend_value));
    const std::string assumptions = joined(derivation.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    push_steps(L, arena, derivation);
    return 1;
}

int l_work(lua_State *L) { return work_into(L, true); }

int l_work_local(lua_State *L) { return work_into(L, false); }

// PHYS-008. The force family reaches Lua as one table in and one record out, so #158 can draw a
// diagram from the same inventory the equations were summed from.
bool optional_quantity(lua_State *L, int table_index, const char *key, Quantity *value,
                       bool *present, std::string *why) {
    if (!field_value(L, table_index, key, why))
        return false;
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_pop(L, 1);
        *present = false;
        return true;
    }
    lua_pop(L, 1);
    if (!quantity_field(L, table_index, key, value, why))
        return false;
    *present = true;
    return true;
}

bool optional_rational(lua_State *L, int table_index, const char *key, Rational *value,
                       std::string *why) {
    if (!field_value(L, table_index, key, why))
        return false;
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    lua_pop(L, 1);
    Precision ignored;
    return rational_field(L, table_index, key, value, &ignored, why);
}

bool optional_name(lua_State *L, int table_index, const char *key, std::string *value,
                   std::string *why) {
    if (!field_value(L, table_index, key, why))
        return false;
    if (lua_type(L, -1) == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    lua_pop(L, 1);
    return string_field(L, table_index, key, value, why);
}

bool optional_boolean(lua_State *L, int table_index, const char *key, bool *value,
                      std::string *why) {
    if (!field_value(L, table_index, key, why))
        return false;
    const int kind = lua_type(L, -1);
    if (kind == LUA_TNIL) {
        lua_pop(L, 1);
        return true;
    }
    if (kind != LUA_TBOOLEAN) {
        lua_pop(L, 1);
        *why = std::string(key) + " must be a boolean";
        return false;
    }
    *value = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return true;
}

bool motion_stage_field(lua_State *L, int table_index, const char *key, MotionStage *value,
                        std::string *why) {
    std::string name = motion_stage_name(*value);
    if (!optional_name(L, table_index, key, &name, why))
        return false;
    if (name == "event")
        *value = MotionStage::Event;
    else if (name == "state")
        *value = MotionStage::State;
    else if (name == "interval")
        *value = MotionStage::Interval;
    else {
        *why = std::string(key) + " must be event, state or interval";
        return false;
    }
    return true;
}

void set_planar_vector(lua_State *L, const char *key, const Vector &vector,
                       const std::string &text, MotionStage stage) {
    lua_pushstring(L, key);
    lua_newtable(L);
    set_field(L, "result", text);
    set_field(L, "exact_x", rational_text(vector.x));
    set_field(L, "exact_y", rational_text(vector.y));
    set_field(L, "unit", vector.unit.text);
    set_field(L, "frame", vector.frame.name);
    set_field(L, "rank", static_cast<int>(vector.rank));
    set_field(L, "stage", motion_stage_name(stage));
    set_precision(L, vector.precision);
    lua_settable(L, -3);
}

int l_planar_kinematics(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE) {
        return typed_failure(L, "invalid problem", "invalid input",
                             "planar kinematics input must be a table");
    }

    PlanarKinematicsProblem problem;
    std::string why;
    if (!string_field(L, 1, "body_name", &problem.body_name, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!field_value(L, 1, "initial_velocity", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!vector_table(L, -1, &problem.initial_velocity, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input", "initial_velocity: " + why);
    }
    lua_pop(L, 1);
    if (!field_value(L, 1, "acceleration", &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!vector_table(L, -1, &problem.acceleration, &why)) {
        lua_pop(L, 1);
        return typed_failure(L, "invalid problem", "invalid input", "acceleration: " + why);
    }
    lua_pop(L, 1);
    if (!quantity_field(L, 1, "elapsed_time", &problem.elapsed_time, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    if (!motion_stage_field(L, 1, "initial_velocity_stage", &problem.initial_velocity_stage,
                            &why) ||
        !motion_stage_field(L, 1, "acceleration_stage", &problem.acceleration_stage, &why) ||
        !motion_stage_field(L, 1, "elapsed_time_stage", &problem.elapsed_time_stage, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    std::string axes = planar_axes_name(problem.axes);
    if (!optional_name(L, 1, "axes", &axes, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (axes == "right-up")
        problem.axes = PlanarAxes::RightUp;
    else
        return typed_failure(L, "invalid problem", "invalid input", "axes must be right-up");

    if (!optional_boolean(L, 1, "projectile", &problem.projectile, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    GcPause paused(L);
    Arena arena;
    Derivation derivation;
    const Budget budget = interactive_budget();
    PlanarKinematicsResult result;
    if (GiacBackend::available(L)) {
        GiacBackend backend(L);
        result = solve_planar_kinematics(arena, derivation, problem, budget, &backend);
    } else {
        result = solve_planar_kinematics(arena, derivation, problem, budget, nullptr);
    }

    lua_newtable(L);
    set_field(L, "outcome", planar_kinematics_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", result.outcome == PlanarKinematicsOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    if (result.has_value) {
        set_field(L, "result", result.displacement_text);
        set_field(L, "value", result.displacement_text);
        set_field(L, "interpretation", result.interpretation);
        set_planar_vector(L, "displacement", result.displacement, result.displacement_text,
                          result.displacement_stage);
        set_planar_vector(L, "final_velocity", result.final_velocity, result.final_velocity_text,
                          result.final_velocity_stage);
        set_precision(L, result.displacement.precision);
    }
    if (result.equation != kNoNode)
        set_field(L, "equation", print(arena, result.equation));
    if (result.substituted != kNoNode)
        set_field(L, "substituted", print(arena, result.substituted));
    const std::string assumptions = joined(derivation.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    push_steps(L, arena, derivation);
    return 1;
}

void set_force_entry(lua_State *L, int index, const ForceEntry &entry) {
    lua_pushinteger(L, static_cast<lua_Integer>(index));
    lua_newtable(L);
    set_field(L, "kind", force_kind_name(entry.kind));
    set_field(L, "label", entry.label);
    set_field(L, "agent", entry.agent);
    set_field(L, "magnitude", entry.magnitude_text);
    set_field(L, "along", entry.along_text);
    set_field(L, "across", entry.across_text);
    set_field(L, "known", entry.known);
    lua_settable(L, -3);
}

int l_forces(lua_State *L) {
    if (lua_type(L, 1) != LUA_TTABLE)
        return typed_failure(L, "invalid problem", "invalid input", "forces input must be a table");

    ForcesProblem problem;
    std::string why;
    if (!optional_name(L, 1, "body", &problem.body, &why) ||
        !optional_name(L, 1, "support", &problem.support, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (!quantity_field(L, 1, "mass", &problem.mass, &why) ||
        !quantity_field(L, 1, "gravity", &problem.gravity, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    std::string surface = "horizontal";
    if (!optional_name(L, 1, "surface", &surface, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (surface == "horizontal")
        problem.surface = SurfaceKind::Horizontal;
    else if (surface == "incline")
        problem.surface = SurfaceKind::Incline;
    else
        return typed_failure(L, "invalid problem", "invalid input",
                             "surface must be horizontal or incline");
    if (!optional_rational(L, 1, "incline_sin", &problem.incline_sin, &why) ||
        !optional_rational(L, 1, "incline_cos", &problem.incline_cos, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    if (!optional_quantity(L, 1, "applied", &problem.applied, &problem.has_applied, &why) ||
        !optional_quantity(L, 1, "tension", &problem.tension, &problem.has_tension, &why) ||
        !optional_quantity(L, 1, "acceleration", &problem.acceleration, &problem.has_acceleration,
                           &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    std::string friction = "frictionless";
    if (!optional_name(L, 1, "friction", &friction, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (friction == "frictionless")
        problem.friction = FrictionModel::None;
    else if (friction == "static")
        problem.friction = FrictionModel::Static;
    else if (friction == "kinetic")
        problem.friction = FrictionModel::Kinetic;
    else
        return typed_failure(L, "invalid problem", "invalid input",
                             "friction must be frictionless, static or kinetic");
    if (!optional_rational(L, 1, "friction_coefficient", &problem.friction_coefficient, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    std::string motion = "undeclared";
    if (!optional_name(L, 1, "motion", &motion, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (motion == "undeclared")
        problem.motion = MotionSense::Undeclared;
    else if (motion == "up the axis")
        problem.motion = MotionSense::UpTheAxis;
    else if (motion == "down the axis")
        problem.motion = MotionSense::DownTheAxis;
    else
        return typed_failure(L, "invalid problem", "invalid input",
                             "motion must be undeclared, up the axis or down the axis");
    if (!optional_boolean(L, 1, "equilibrium", &problem.assume_equilibrium, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);

    std::string unknown;
    if (!string_field(L, 1, "unknown", &unknown, &why))
        return typed_failure(L, "invalid problem", "invalid input", why);
    if (unknown == "acceleration")
        problem.unknown = ForcesUnknown::Acceleration;
    else if (unknown == "applied force")
        problem.unknown = ForcesUnknown::AppliedForce;
    else if (unknown == "normal force")
        problem.unknown = ForcesUnknown::NormalForce;
    else if (unknown == "friction force")
        problem.unknown = ForcesUnknown::FrictionForce;
    else
        return typed_failure(L, "invalid problem", "invalid input",
                             "unknown must be acceleration, applied force, normal force or "
                             "friction force");

    GcPause paused(L);
    Arena arena;
    Derivation derivation;
    const ForcesResult result = solve_forces(arena, derivation, problem, interactive_budget());

    lua_newtable(L);
    set_field(L, "outcome", forces_outcome_name(result.outcome));
    set_field(L, "detail", result.detail);
    set_field(L, "solved", result.outcome == ForcesOutcome::Solved);
    set_field(L, "answer_only", false);
    set_field(L, "status", derivation_status_name(result.status));
    set_field(L, "unknown", forces_unknown_name(problem.unknown));
    set_field(L, "surface", surface_kind_name(problem.surface));
    set_field(L, "friction_model", friction_model_name(problem.friction));
    if (result.has_value) {
        set_field(L, "result", std::string(forces_unknown_name(problem.unknown)) + " = " +
                                   result.value_text + " " + result.unit_text);
        set_field(L, "value", result.value_text);
        set_field(L, "exact_value", rational_text(result.value));
        set_field(L, "unit", result.unit_text);
    }
    if (!result.along_equation_text.empty())
        set_field(L, "along_equation", result.along_equation_text);
    if (!result.across_equation_text.empty())
        set_field(L, "across_equation", result.across_equation_text);
    if (!result.consistency.empty())
        set_field(L, "consistency", result.consistency);
    set_field(L, "static_checked", result.static_checked);
    if (result.static_checked) {
        set_field(L, "required_friction", rational_text(result.required_friction));
        set_field(L, "maximum_static_friction", rational_text(result.maximum_static_friction));
    }

    lua_pushstring(L, "inventory");
    lua_newtable(L);
    for (size_t i = 0; i < result.inventory.size(); ++i)
        set_force_entry(L, static_cast<int>(i + 1), result.inventory[i]);
    lua_settable(L, -3);

    lua_pushstring(L, "pairs");
    lua_newtable(L);
    for (size_t i = 0; i < result.pairs.size(); ++i) {
        const InteractionPair &pair = result.pairs[i];
        lua_pushinteger(L, static_cast<lua_Integer>(i + 1));
        lua_newtable(L);
        set_field(L, "kind", force_kind_name(pair.kind));
        set_field(L, "on_body", pair.on_body);
        set_field(L, "by_body", pair.by_body);
        set_field(L, "reaction_on", pair.reaction_on);
        set_field(L, "reaction_by", pair.reaction_by);
        set_field(L, "magnitude", pair.magnitude_text);
        lua_settable(L, -3);
    }
    lua_settable(L, -3);

    const std::string assumptions = joined(derivation.context.active_assumptions);
    if (!assumptions.empty())
        set_field(L, "assumptions", assumptions);
    set_cost(L, arena, derivation, result.cost, result.cost.backend_calls);
    push_steps(L, arena, derivation);
    return 1;
}

int l_magnitude_angle_to_components(lua_State *L) {
    MagnitudeAngleExpr input;
    std::string magnitude;
    std::string angle;
    std::string why;
    if (!magnitude_angle_text(L, 1, &input, &magnitude, &angle, &why))
        return typed_failure(L, "invalid input", "invalid input", why);

    GcPause paused(L);
    Arena arena;
    if (!magnitude_angle_parse(arena, magnitude, angle, &input, &why))
        return typed_failure(L, "invalid input", "invalid input", why);

    Derivation derivation;
    VectorComponentsResult result;
    const Budget budget = interactive_budget();
    if (GiacBackend::available(L)) {
        GiacBackend backend(L);
        result = magnitude_angle_to_components(arena, derivation, input, backend, budget);
    } else {
        UnavailableBackend backend;
        result = magnitude_angle_to_components(arena, derivation, input, backend, budget);
    }
    return set_vector_components_result(L, arena, derivation, result);
}

int l_components_to_magnitude_angle(lua_State *L) {
    VectorExpr input;
    Vector exact;
    bool has_exact = false;
    AngleUnit output_unit;
    std::string x;
    std::string y;
    std::string z;
    std::string why;
    if (!vector_expression_text(L, 1, &input, &x, &y, &z, &why) ||
        !angle_unit_field(L, 1, &output_unit, &why))
        return typed_failure(L, "invalid input", "invalid input", why);

    GcPause paused(L);
    Arena arena;
    if (!vector_expression_parse(arena, x, y, z, &input, &exact, &has_exact, &why))
        return typed_failure(L, "invalid input", "invalid input", why);

    Derivation derivation;
    VectorComponentsResult result;
    const Budget budget = interactive_budget();
    if (GiacBackend::available(L)) {
        GiacBackend backend(L);
        result = has_exact ? components_to_magnitude_angle(arena, derivation, exact, output_unit,
                                                           backend, budget)
                           : components_to_magnitude_angle(arena, derivation, input, output_unit,
                                                           backend, budget);
    } else {
        UnavailableBackend backend;
        result = has_exact ? components_to_magnitude_angle(arena, derivation, exact, output_unit,
                                                           backend, budget)
                           : components_to_magnitude_angle(arena, derivation, input, output_unit,
                                                           backend, budget);
    }
    return set_vector_components_result(L, arena, derivation, result);
}

int l_integrate(lua_State *L) { return integrate_into(L, true); }

int l_integrate_local(lua_State *L) { return integrate_into(L, false); }

#if NPS_GIAC
// The shell's own channel to Giac, which is what Ki V1 reached through luagiac and what every
// KhiCAS feature outside the step modes runs on. It carries whatever the user typed, so it is
// outside section 12.3's boundary by design, and it is the only entry here that is: no step entry
// takes a Giac command from Lua. The GC is held for the call because a collection inside Giac has
// nothing of ours to protect and a long C call is where it would land.
int l_caseval(lua_State *L) {
    const char *command = scalar_string_argument(L, 1);
    GcPause paused(L);
    const char *reply = giac_caseval(command);
    lua_pushstring(L, reply ? reply : "");
    return 1;
}

// Report typed and string disagreements from the library loaded in this module.
int l_typed_check(lua_State *L) {
    GcPause paused(L);
    std::string report;
    const int disagreements = typed_differential_check(&report);
    // Written here rather than from Lua: the calculator's Lua has no io library, so the document
    // cannot open a file even for a diagnostic. Found by trying it on the emulator.
    FILE *f = fopen("/documents/ndl/typedcheck.txt.tns", "wb");
    if (f) {
        fwrite(report.data(), 1, report.size(), f);
        fclose(f);
    }
    lua_pushinteger(L, disagreements);
    lua_pushstring(L, report.c_str());
    return 2;
}
#endif

const luaL_Reg lib[] = {
#if NPS_DIAG
    {"trace", l_trace},
    {"trace_clear", l_trace_clear},
    {"largest_block", l_largest_block},
    {"disable_watchdog", l_disable_watchdog},
    {"peek", l_peek},
    {"watchdog_state", l_watchdog_state},
#endif
#if NPS_RELEASE_MANIFEST
    {"capability_manifest", l_capability_manifest},
#endif
    {"integrity_status", l_integrity_status},
#if NPS_RESOURCE_PROFILE
    {"resource_profile_begin", l_resource_profile_begin},
    {"resource_profile_finish", l_resource_profile_finish},
#endif
    {"allocator_headroom_kb", l_allocator_headroom_kb},
    {"heap_free", l_heap_free},
    {"device_identity", l_device_identity},
    {"os_msgbox", l_os_msgbox},
#if NPS_GIAC
    {"os_menu", l_os_menu},
#endif
    {"os_number_input", l_os_number_input},
    {"canonical", l_canonical},
    {"math_display", l_math_display},
    {"giac", l_giac},
    {"walkthrough", l_walkthrough},
    {"ui_panel", l_ui_panel},
    {"ui_icon", l_ui_icon},
    {"ui_icon_image", l_ui_icon_image},
#if NPS_RETAINED_UI
    {"ui_menu_open", l_ui_menu_open},
    {"ui_menu_frame", l_ui_menu_frame},
    {"ui_menu_select", l_ui_menu_select},
    {"ui_menu_scroll", l_ui_menu_scroll},
    {"ui_menu_close", l_ui_menu_close},
#if NPS_RETAINED_FAULT_INJECTION
    {"ui_menu_test_failure", l_ui_menu_test_failure},
    {"ui_menu_test_status", l_ui_menu_test_status},
#endif
#endif
    {"solve", l_solve},
    {"solve_begin", l_solve_begin},
    {"solve_advance", l_solve_advance},
    {"solve_cancel", l_solve_cancel},
    {"solve_close", l_solve_close},
    {"solve_local", l_solve_local},
    {"differentiate", l_differentiate},
    {"differentiate_local", l_differentiate_local},
    {"integrate", l_integrate},
    {"integrate_local", l_integrate_local},
    {"kinematics", l_kinematics},
    {"kinematics_local", l_kinematics_local},
    {"catch_up", l_catch_up},
    {"unit_conversion", l_unit_conversion},
    {"density", l_density},
    {"optics", l_optics},
    {"vector_addition", l_vector_addition},
    {"vector_cross", l_vector_cross},
    {"relative_motion", l_relative_motion},
    {"relative_motion_local", l_relative_motion_local},
    {"forces", l_forces},
    {"work", l_work},
    {"work_local", l_work_local},
    {"planar_kinematics", l_planar_kinematics},
    {"magnitude_angle_to_components", l_magnitude_angle_to_components},
    {"components_to_magnitude_angle", l_components_to_magnitude_angle},
#if NPS_GIAC
    {"caseval", l_caseval},
    {"typed_check", l_typed_check},
#endif
    {NULL, NULL},
};

const luaL_Reg integrity_failure_lib[] = {
    {"capability_manifest", l_capability_manifest},
    {"integrity_status", l_integrity_status},
    {NULL, NULL},
};

void register_integrity_failure_surface(lua_State *L, const char *module_name,
                                        IntegrityStatus status) {
    module_integrity_status = status;
    lua_newtable(L);
    luaL_register(L, nullptr, integrity_failure_lib);
    if (!module_name)
        return;

    lua_pushvalue(L, -1);
    lua_setglobal(L, module_name);

    lua_getfield(L, LUA_REGISTRYINDEX, "_LOADED");
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        lua_newtable(L);
        lua_pushvalue(L, -1);
        lua_setfield(L, LUA_REGISTRYINDEX, "_LOADED");
    }
    lua_pushvalue(L, -2);
    lua_setfield(L, -2, module_name);
    lua_pop(L, 1);
}

}  // namespace

#if NPS_DIAG
// Diagnostic. Lua calls this when an error is raised with nothing protecting it, just before it
// gives up, and it is the one place that can say so before the calculator resets. If the reset is a
// Lua error escaping our C function, this line appears in the trace and names it. If the trace ends
// without it, the fault is not a Lua error and the whole longjmp theory is wrong.
int on_panic(lua_State *L) {
    const char *msg = lua_tostring(L, -1);
    trace("PANIC");
    trace(msg ? msg : "(no message)");
    return 0;
}
#endif

int main(int argc, char **argv) {
    lua_State *L = nl_lua_getstate();
    if (!L)
        return 0;  // not being called as a Lua module
#if NPS_DIAG
    lua_atpanic(L, on_panic);
#endif
    if (!lua_number_abi_works(L))
        return luaL_error(L, "StepCAS module rejected an incompatible Ndl Lua number ABI");
    // Ki V4 is one module, so the step entries and Giac's caseval arrive under one name. The two
    // builds register different names on purpose: a document written for one cannot silently load
    // the other and find half of what it expects.
    // A file association appends its document after argv[0], so exactly one would refuse a valid load.
    module_integrity_status = argc >= 1 && argv && argv[0]
                                  ? verify_package_integrity(argv[0], kModulePackageBasename)
                                  : IntegrityStatus::PathInvalid;
    if (module_integrity_status != IntegrityStatus::Verified) {
        register_integrity_failure_surface(L, kModuleName, module_integrity_status);
        _exit(0);
    }
    luaL_register(L, kModuleName, lib);

    // _exit rather than return, because this module stays loaded after main and its functions are
    // called long afterwards. nucleus.h:127-129 states the rule: returning from main runs
    // __cpp_fini and newlib's exit (crt0.S), which destroys every static object in this image and
    // shuts down the C runtime while the image is still registered and reachable from Lua.
    _exit(0);
}
