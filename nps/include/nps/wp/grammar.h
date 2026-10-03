#ifndef NPS_WP_GRAMMAR_H
#define NPS_WP_GRAMMAR_H

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "nps/wp/lexical.h"
#include "nps/wp/problem_ir.h"

namespace nps::wp {

// PRD section 26.16, WP Milestone 2. A controlled grammar for one body moving along one line, which
// covers constant-acceleration kinematics and, with a confirmed zero acceleration, rate and distance.
constexpr const char *kGrammarVersion = "wp2-motion 1";
constexpr const char *kMotionFamily = "physics.kinematics.constant-acceleration.one-dimension";

// The WP1 gate's three answers, a proposed ProblemIR, a required clarification or a typed failure,
// with the lexical layer's resource outcomes kept apart from all three.
enum class GrammarOutcome : uint8_t {
    Interpreted,
    NeedsClarification,
    Unsupported,
    ResourceExhausted,
    Cancelled,
};

const char *grammar_outcome_name(GrammarOutcome outcome);

// A grounded quantity and the role a production gave it. An empty role is one the user has to settle.
struct RoleAssignment {
    size_t quantity = 0;
    std::string role;
    std::string production;
    bool negated = false;
};

// A quantity no number in the text supplies, stated by a phrase or inferred by a named production.
struct ImpliedQuantity {
    std::string role;
    std::string value;
    std::string unit;
    std::string production;
    bool inferred = false;
    Span span;
};

struct Clarification {
    std::string id;
    size_t quantity = 0;
    std::string question;
    std::vector<std::string> options;
    Span span;
};

struct GrammarResult {
    GrammarOutcome outcome = GrammarOutcome::Unsupported;
    std::string detail;
    LexicalReading lexical;
    std::vector<RoleAssignment> roles;
    std::vector<ImpliedQuantity> implied;
    std::vector<Clarification> clarifications;
    std::string goal_role;
    Span goal_span;
    std::string entity;
    Span entity_span;
    std::vector<Span> unused;
    std::vector<Span> stated_constant_acceleration;
    // Set when the text says the speed changes, which rules out inferring a uniform motion.
    bool acceleration_mentioned = false;
    // The proposal, unconfirmed. Present only when nothing is left to ask.
    std::optional<ProblemIR> draft;
    InterpretationSet set;
};

GrammarResult interpret_motion(const std::string &source_id, const std::string &text,
                               const LexicalLimits &limits = LexicalLimits());

struct ClarificationAnswer {
    std::string clarification_id;
    std::string option;
};

enum class ConfirmStatus : uint8_t {
    Confirmed,
    NeedsClarification,
    InvalidAnswer,
    SourceChanged,
    Rejected,
};

const char *confirm_status_name(ConfirmStatus status);

struct ConfirmResult {
    ConfirmStatus status = ConfirmStatus::Rejected;
    std::string detail;
    IrValidation validation;
    std::optional<CommittedProblem> committed;
};

// WP-011, WP-039, WP-041 and WP-043. Confirmation answers every clarification against the text as it
// stands now, binds what was approved, and still has to pass validation before anything commits.
ConfirmResult confirm_motion(const GrammarResult &interpreted, const std::string &current_text,
                             const std::vector<ClarificationAnswer> &answers, const std::string &confirmed_by);

// One line per role, goal, assumption, clarification and unused span, as the corpus annotates them.
std::string grammar_summary(const GrammarResult &result);

}  // namespace nps::wp

#endif
