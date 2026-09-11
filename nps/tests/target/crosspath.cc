// Runs exactly what differentiate_into does on a cross-checked call, minus the Lua half, so a
// sanitizer can watch it. The device dies only on calls that reach Giac, and the difference between
// those and a local call is this sequence: parse, differentiate, ask Giac, build the difference in
// the same arena, ask Giac again, print, canonicalise, then walk the derivation the way push_steps
// does. Every one of those steps except the Giac calls themselves is our code.
//
// The replies are the ones the handheld actually produced for x^2*sin(x), so the arena grows here
// the way it grows there.

#include <cstdio>
#include <string>
#include <vector>

#include "nps/core/canonical.h"
#include "nps/steps/derivation.h"
#include "nps/steps/differentiate.h"
#include "nps/cas/giac_adapter.h"
#include "nps/core/parser.h"
#include "nps/core/print.h"

namespace nps {
namespace {

// Answers each call in turn, so the two halves of a cross-check get different replies, which a
// single fixed reply cannot express.
class SequencedBackend : public Backend {
  public:
    explicit SequencedBackend(const std::vector<std::string> &replies) : replies_(replies) {}

    bool eval(const std::string &command, std::string *out, std::string *error) override {
        commands.push_back(command);
        if (next_ >= replies_.size()) {
            *error = "the fake backend ran out of replies";
            return false;
        }
        *out = replies_[next_++];
        return true;
    }

    std::vector<std::string> commands;

  private:
    std::vector<std::string> replies_;
    size_t next_ = 0;
};

bool is_literal_zero(const Arena &arena, NodeId id) {
    if (id == kNoNode)
        return false;
    const Node &n = arena.at(id);
    return n.kind == Kind::Integer && n.small_valid && n.small == 0;
}

// The walk push_step does over every row: read the payload pointers, print both NodeIds, and touch
// every string. If a payload pointer or a NodeId goes stale when the arena grows during the
// cross-check, this is where it reads the wrong memory.
size_t walk(const Arena &arena, const Derivation &d, StepId id, int depth) {
    const Step &s = d.at(id);
    size_t touched = s.goal.size() + s.explanation_short.size() + s.rule_name.size();
    touched += static_cast<size_t>(depth);

    const TransformationPayload *t = d.transformation(id);
    if (t) {
        if (t->before != kNoNode)
            touched += print(arena, t->before).size();
        if (t->after != kNoNode)
            touched += print(arena, t->after).size();
        touched += t->concrete_action.size();
    }
    const CheckPayload *c = d.check(id);
    if (c)
        touched += c->expected_relation.size() + c->observed_result.size() + c->check_method.size();

    for (size_t i = 0; i < s.children.size(); ++i)
        touched += walk(arena, d, s.children[i], depth + 1);
    return touched;
}

int one_round(const char *input, const std::vector<std::string> &replies) {
    Arena arena;
    ParseResult parsed = parse(arena, input);
    if (!parsed.ok()) {
        std::printf("  parse failed\n");
        return 1;
    }

    Derivation d;
    const NodeId var = arena.symbol("x");
    DiffResult r = differentiate(arena, d, parsed.root, var);

    SequencedBackend backend(replies);
    Adapter adapter(arena, backend);

    Request req;
    req.op = Op::Differentiate;
    req.target = parsed.root;
    req.variable = var;
    Response first = adapter.run(req);

    bool agrees = false;
    if (first.usable() && r.derivative != kNoNode) {
        // The arena grows here, after every NodeId in the derivation was recorded.
        const NodeId negated = arena.binary(Kind::Mul, arena.integer("-1"), first.value);
        Request zero;
        zero.op = Op::IsZero;
        zero.target = arena.binary(Kind::Add, r.derivative, negated);
        Response z = adapter.run(zero);
        if (z.usable())
            agrees = is_literal_zero(arena, z.value);
    }

    const std::string printed = r.derivative == kNoNode ? std::string() : print(arena, r.derivative);
    const NodeId canon = r.derivative == kNoNode ? kNoNode : canonicalize(arena, r.derivative);
    const std::string canon_text = canon == kNoNode ? std::string() : print(arena, canon);

    size_t touched = 0;
    const std::vector<StepId> &roots = d.roots();
    for (size_t i = 0; i < roots.size(); ++i)
        touched += walk(arena, d, roots[i], 0);

    std::printf("  nodes %u steps %u agrees %d printed %u canon %u touched %u calls %u\n",
                static_cast<unsigned>(arena.node_count()), static_cast<unsigned>(d.size()),
                agrees ? 1 : 0, static_cast<unsigned>(printed.size()),
                static_cast<unsigned>(canon_text.size()), static_cast<unsigned>(touched),
                static_cast<unsigned>(adapter.call_count()));
    return 0;
}

}  // namespace
}  // namespace nps

int main() {
    using namespace nps;

    // What the handheld's Giac actually returns for these two commands.
    const char *input = "x^2*sin(x)";
    std::vector<std::string> replies;
    replies.push_back("x^2*cos(x)+2*x*sin(x)");
    replies.push_back("0");

    std::printf("round trips on %s\n", input);
    for (int i = 0; i < 200; ++i) {
        if (one_round(input, replies) != 0)
            return 1;
    }

    // Replies the adapter has to survive without trusting: an error string, an unevaluated echo, a
    // float, an empty answer, and something with no closing bracket.
    const char *inputs[] = {"x^2*sin(x)", "sin(sin(sin(x)))", "x^3/(x+1)", "exp(x)*ln(x)"};
    const char *odd[] = {"GIAC_ERROR: bad", "diff(x^2*sin(x),x)", "1.4142135", "",
                         "x^2*cos(x", "undef", "+", "((((", "0.0", "x^2*cos(x)+2*x*sin(x)"};
    std::printf("odd replies\n");
    for (size_t i = 0; i < sizeof(inputs) / sizeof(inputs[0]); ++i) {
        for (size_t a = 0; a < sizeof(odd) / sizeof(odd[0]); ++a) {
            for (size_t b = 0; b < sizeof(odd) / sizeof(odd[0]); ++b) {
                std::vector<std::string> r;
                r.push_back(odd[a]);
                r.push_back(odd[b]);
                Arena scratch;
                (void)scratch;
                if (one_round(inputs[i], r) != 0)
                    return 1;
            }
        }
    }

    std::printf("crosspath: done\n");
    return 0;
}
