#ifndef NPS_GIAC_TYPED_H
#define NPS_GIAC_TYPED_H

#include <string>

#include "nps/cas/giac_adapter.h"

namespace nps {

// The adapter's typed path, for the build that links Giac in. PRD section 12.3 and the agent pack's
// section 17.2 both say to hand the engine objects rather than a command string, and this is that:
// the AST becomes giac::gen directly, the operation is an ordinary C++ call, and the answer comes
// back as a gen that is walked into the arena. No printing on the way in, no parsing on the way out.
//
// Nothing in this header names a Giac type, so the core still compiles without Giac's headers on the
// include path. The one file that includes both is giac_typed.cc.
//
// eval is still here because the shell's plain-line path wants text in and text out, and because a
// caller that has a command string rather than a Request has nowhere else to go.
class TypedGiacBackend : public Backend {
  public:
    bool eval(const std::string &command, std::string *out, std::string *error) override;
    bool typed(const Request &request, Arena &arena, TypedResult *out) override;
    bool matrix_steps(const Request &request, Arena &arena, MatrixRowSink &sink,
                      TypedResult *out) override;
};

// Compare typed and string responses. tests/giac also exercises the actual host library.
int typed_differential_check(std::string *report);

}  // namespace nps

#endif
