#include "nps/steps/schema.h"

namespace nps {

const char *failure_behavior_name(FailureBehavior b) {
    switch (b) {
        case FailureBehavior::WithholdResult:
            return "withhold the result";
        case FailureBehavior::CannotFail:
            return "cannot fail";
    }
    return "unknown";
}

namespace {

// A strategy is declared the same way a rule is, because a plan step's rule_id is its strategy id
// and its preconditions are its obligations: each one names a method, the method's evidence has a
// worth, and a precondition that does not pass stops the strategy. The one difference is the claim,
// which add_plan forces to NoClaim, and the summary record every plan carries.
const EvidenceAlternative kRegisteredPreconditions[] = {
    {"registered strategy preconditions", EvidenceStrength::StructurallyValid},
};

const EvidenceAlternative kExactLinearAnalysis[] = {
    {"exact linear analysis", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kLinearStrategy[] = {
    {"pre.linear.degree-one", "the equation is degree one in the unknown", kExactLinearAnalysis, 1},
    {"pre.linear.rational-constants", "every other quantity is a rational constant",
     kExactLinearAnalysis, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};

const EvidenceAlternative kEqualityInvariant[] = {
    {"rule-local equality invariant", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kSameSolutions[] = {
    {"obl.eq.same-solutions", "the rewritten equation has the solutions the original had",
     kEqualityInvariant, 1},
};

const EvidenceAlternative kCoefficientInspection[] = {
    {"inspection of the collected coefficient", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kCoefficientDecides[] = {
    {"obl.linear.coefficient-decides",
     "the collected coefficient of the unknown is what decides between no solution and every value",
     kCoefficientInspection, 1},
};

const EvidenceAlternative kSubstitution[] = {
    {"substitution", EvidenceStrength::CandidateChecked},
};

const ObligationSchema kCandidateSatisfies[] = {
    {"obl.linear.candidate-satisfies", "the candidate satisfies the original equation",
     kSubstitution, 1},
};

// algebra.quadratic.pure-square.one-unknown
const EvidenceAlternative kSquaredTermAnalysis[] = {
    {"exact linear analysis in the squared term", EvidenceStrength::StructurallyValid},
};

const EvidenceAlternative kExactSquareRoot[] = {
    {"exact rational square root", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kSquareRootStrategy[] = {
    {"pre.quadratic.pure-square", "the equation is degree two in the unknown with no term of "
     "degree one", kSquaredTermAnalysis, 1},
    {"pre.quadratic.exact-square-root",
     "the isolated square has an exact rational square root or is negative", kExactSquareRoot, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};

const EvidenceAlternative kExactRationalSquare[] = {
    {"exact rational square", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kCaseIsARoot[] = {
    {"obl.quadratic.case-is-a-root", "this case's value squares to the isolated value",
     kExactRationalSquare, 1},
};

const ObligationSchema kQuadraticCandidateSatisfies[] = {
    {"obl.quadratic.candidate-satisfies", "the candidate satisfies the original equation",
     kSubstitution, 1},
};

const EvidenceAlternative kSignOfARealSquare[] = {
    {"sign of a real square", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kRejectedCaseIsInfeasible[] = {
    {"obl.quadratic.rejected-case-is-infeasible",
     "no real value satisfies the condition this case stands for", kSignOfARealSquare, 1},
};

// VER-017's completeness half, and the two ways this rule has of arguing it. A split with cases
// argues by rebuilding the quadratic they imply; a split with none argues from the sign, because
// there is nothing to rebuild. Either discharges the obligation, which is what an alternative is
// for, and requiring the reconstruction of both would make the empty split unprovable.
const EvidenceAlternative kSplitIsComplete[] = {
    {"root-coefficient reconstruction",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
    {"sign of a real square", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kCasesAreComplete[] = {
    {"obl.quadratic.cases-are-complete",
     "every real value satisfying the equation is one of the cases recorded", kSplitIsComplete, 2},
};

// Every rewriting rule in the two calculus engines carries the same obligation and discharges it
// the same way, through the rule_invariant helper each file has. It is one schema rather than
// twenty because it is one obligation: the rule rewrote a subexpression into one with the same
// value, and the argument is the rule's own form rather than anything about the problem.
const EvidenceAlternative kRuleInvariant[] = {
    {"rule-local invariant", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kRulePreservesValue[] = {
    {"obl.calculus.rule-preserves-value",
     "the rewritten subexpression has the value the original had", kRuleInvariant, 1},
};

// The constant of integration is the one calculus step that does not preserve the value it was
// given, so it cannot raise the obligation above. What it owes instead is that the family it names
// is the antiderivative it was handed plus one free constant and nothing else.
const ObligationSchema kFamilyAddsAConstant[] = {
    {"obl.calculus.family-adds-a-constant",
     "the after state is the antiderivative found plus one constant free nowhere before it",
     kRuleInvariant, 1},
};

const EvidenceAlternative kDerivativeDispatch[] = {
    {"registered derivative rule dispatch", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kDifferentiateStrategy[] = {
    {"pre.differentiate.registered-rules", "every form in the expression has a derivative rule",
     kDerivativeDispatch, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};

const EvidenceAlternative kAntiderivativeDispatch[] = {
    {"registered antiderivative rule dispatch", EvidenceStrength::StructurallyValid},
};

const EvidenceAlternative kInnerFormAnalysis[] = {
    {"registered inner-form analysis", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kIntegrateStrategy[] = {
    {"pre.integrate.registered-rules", "every form in the integrand has an antiderivative rule",
     kAntiderivativeDispatch, 1},
    {"pre.integrate.linear-inner-forms",
     "every function argument and every power base is the variable or linear in it",
     kInnerFormAnalysis, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};

const EvidenceAlternative kDifferentiateBack[] = {
    {"differentiate the antiderivative", EvidenceStrength::CandidateChecked},
    {"native differentiation and Giac exact difference", EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};

const ObligationSchema kDerivativeReturnsIntegrand[] = {
    {"obl.integrate.derivative-returns-integrand",
     "the derivative of the antiderivative is the integrand", kDifferentiateBack, 2},
};

const EvidenceAlternative kExactRationalArithmetic[] = {
    {"exact rational arithmetic", EvidenceStrength::StructurallyValid},
};

const EvidenceAlternative kLimitClassificationEvidence[] = {
    {"polynomial order and sign comparison", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kLimitClassification[] = {
    {"obl.limit.classification", "the polynomial orders and signs determine the limiting behavior",
     kLimitClassificationEvidence, 1},
};
const EvidenceAlternative kCalculusGiacEvidence[] = {
    {"Giac exact difference", EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};
const EvidenceAlternative kDerivativeCrossCheckEvidence[] = {
    {"Giac Adapter Op::Differentiate compared after canonicalization",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions, true},
};
const ObligationSchema kDerivativeCrossCheck[] = {
    {"obl.differentiate.matches-backend",
     "the derivative the rules produced is the derivative Giac produces",
     kDerivativeCrossCheckEvidence, 1},
};
const EvidenceAlternative kTangentAgreementEvidence[] = {
    {"exact evaluation at the point and one unit away",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};
const ObligationSchema kTangentAgreement[] = {
    {"obl.calculus.tangent-agreement",
     "the line meets the curve at the point and has the derivative as its slope",
     kTangentAgreementEvidence, 1},
};

const ObligationSchema kCalculusGiac[] = {
    {"obl.calculus.giac-agreement", "the native result agrees with Giac's independent calculation",
     kCalculusGiacEvidence, 1},
};

const ObligationSchema kFoldPreservesValue[] = {
    {"obl.alg.fold-preserves-value", "folding the constants leaves the expression's value alone",
     kExactRationalArithmetic, 1},
};

const ObligationSchema kAlgRulePreservesValue[] = {
    {"obl.alg.rule-preserves-value",
     "the rewritten subexpression has the value the original had", kRuleInvariant, 1},
};

// Factoring checks itself by multiplying back out, which two rules spell "brackets" and one spells
// "factors" because that is what each one produced. Both are alternatives for the same obligation.
const EvidenceAlternative kMultiplyBackOut[] = {
    {"multiply the brackets out and compare term by term",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
    {"multiply the factors out and compare term by term",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};

const ObligationSchema kFactorMultipliesBack[] = {
    {"obl.alg.factor-multiplies-back", "the factored form multiplies back out to the original",
     kMultiplyBackOut, 2},
};

// The sampled evaluation names its own sample count, so this string moves if kSamples does. That is
// the right coupling rather than an accident: a different number of assignments is different
// evidence, and the conformance check saying so is what stops the count drifting unremarked.
const EvidenceAlternative kSampledOrBackend[] = {
    {"exact evaluation at 6 rational assignments", EvidenceStrength::NumericallyCorroborated},
    {"Giac Adapter Op::IsZero on the substituted difference",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};

const ObligationSchema kRewriteSameValue[] = {
    {"obl.rewrite.same-value", "the rewritten expression has the value the original had",
     kSampledOrBackend, 2},
};

const ObligationSchema kSubstitutionIdentity[] = {
    {"obl.rearrange.substitution-identity",
     "the original formula holds once the subject is replaced by the rearranged right side",
     kSampledOrBackend, 2},
};

const EvidenceAlternative kLiteralInspection[] = {
    {"literal inspection", EvidenceStrength::StructurallyValid},
};

const EvidenceAlternative kRewritingDispatch[] = {
    {"registered rewriting rule dispatch", EvidenceStrength::StructurallyValid},
};

// The three rewriting strategies differ in which rules they reach for, not in what they promise, so
// they share one precondition set.
const ObligationSchema kRewriteStrategy[] = {
    {"pre.rewrite.exact-numbers", "every number in the expression is exact", kLiteralInspection, 1},
    {"pre.rewrite.registered-rules", "every move comes from a registered rewriting rule",
     kRewritingDispatch, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};

const EvidenceAlternative kOccurrenceCount[] = {
    {"occurrence count", EvidenceStrength::StructurallyValid},
};

const EvidenceAlternative kInverseDispatch[] = {
    {"registered inverse-operation dispatch", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kRearrangeStrategy[] = {
    {"pre.rearrange.single-occurrence", "the subject appears exactly once", kOccurrenceCount, 1},
    {"pre.rearrange.invertible-path",
     "every operation between the subject and the formula's edge can be inverted", kInverseDispatch,
     1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};

const ObligationSchema kEquationSameSolutions[] = {
    {"obl.rearrange.same-solutions", "the rearranged equation has the solutions the original had",
     kEqualityInvariant, 1},
};

// The physics families share a vocabulary of checks, so the alternatives below are named once and
// referred to by several obligations. Each is the method string the engine records, and the
// strength it declares when it passes.
const EvidenceAlternative kRankComparison[] = {
    {"rank comparison", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kFrameDeclaration[] = {
    {"frame declaration", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kFrameIdentity[] = {
    {"frame identity", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kDimensionalAnalysis[] = {
    {"dimensional analysis", EvidenceStrength::DimensionallyValid},
};
const EvidenceAlternative kDimensionComparison[] = {
    {"dimension comparison", EvidenceStrength::DimensionallyValid},
};
const EvidenceAlternative kDimensionalMultiplication[] = {
    {"dimensional multiplication", EvidenceStrength::DimensionallyValid},
};
const EvidenceAlternative kTypedDimensionalAnalysis[] = {
    {"typed dimensional analysis", EvidenceStrength::DimensionallyValid},
};
const EvidenceAlternative kAngleUnitValidation[] = {
    {"angle-unit validation", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kUnitTable[] = {
    {"unit table", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kSignificantFigures[] = {
    {"significant figures", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kOperationOrdering[] = {
    {"operation ordering", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kExactRationalSign[] = {
    {"exact rational sign", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kLawApplicability[] = {
    {"checked physical-law applicability", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kGiacZero[] = {
    {"Giac zero check", EvidenceStrength::SymbolicallyEquivalentUnderAssumptions, true},
};
const EvidenceAlternative kRelativeComponentSubtraction[] = {
    {"nps vector_sub", EvidenceStrength::StructurallyValid},
    {"Giac Simplify and local canonical comparison",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};
const EvidenceAlternative kUnroundedComparison[] = {
    {"exact comparison against the unrounded value", EvidenceStrength::CandidateChecked},
};
const EvidenceAlternative kBoundaryComparison[] = {
    {"exact rational boundary comparison", EvidenceStrength::CandidateChecked},
};
const EvidenceAlternative kDeclaredForceProfile[] = {
    {"declared force profile", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kCoordinateConvention[] = {
    {"coordinate convention validation", EvidenceStrength::StructurallyValid},
};

// kinematics
const EvidenceAlternative kModelValidation[] = {
    {"problem-family model validation", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kRouteSearch[] = {
    {"exact route search through the linear solver", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kKinematicsStrategy[] = {
    {"pre.kinematics.constant-acceleration", "acceleration is constant over the interval",
     kModelValidation, 1},
    {"pre.kinematics.one-axis", "motion is along one axis with one positive direction",
     kModelValidation, 1},
    {"pre.kinematics.route-applicable",
     "every selected equation contains its target and only quantities known by that hop",
     kRouteSearch, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kSubstitutionPreservesSolutions[] = {
    {"obl.kinematics.substitution-preserves-solutions",
     "putting a known quantity in place of its symbol leaves the equation's solutions alone",
     kRuleInvariant, 1},
};
// The kinematics engine converts through its own rule invariant where the density engine reaches
// for the unit table, so the two conversions carry different obligations rather than one shared.
const ObligationSchema kKinematicsConversion[] = {
    {"obl.kinematics.conversion-preserves-solutions",
     "converting a quantity to SI leaves the equation's solutions alone", kRuleInvariant, 1},
};
// The one rule in the table that no host run reaches: it asks Giac to isolate the unknown while the
// equation is still symbolic, so the catalog marks it device and the invariant pass never sees it.
// It is declared here because the catalog names it, which is the whole reason the coverage tool
// checks the catalog against this table rather than trusting the two runtime populations.
const EvidenceAlternative kBackendSolve[] = {
    {"backend solve, checked by backend is_zero",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions, true},
};
const ObligationSchema kSymbolicIsolation[] = {
    {"obl.kinematics.symbolic-isolation",
     "the backend's isolated form has the solutions the symbolic equation had", kBackendSolve, 1},
};
const ObligationSchema kKinematicsDimensions[] = {
    {"obl.kinematics.dimensions-agree",
     "dimension of the left side equals dimension of the right side", kDimensionalAnalysis, 1},
};
const ObligationSchema kRoundingWithinHalfPlace[] = {
    {"obl.kinematics.rounding-within-half-place",
     "the reported value is within half a unit in the last place of the exact one",
     kUnroundedComparison, 1},
};

const EvidenceAlternative kForcesArrangement[] = {
    {"arrangement check", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kForcesAngle[] = {
    {"trigonometric identity", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kForcesDeclaration[] = {
    {"declaration check", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kForcesStrategy[] = {
    {"pre.forces.single-body", "exactly one body carries the force inventory",
     kForcesArrangement, 1},
    {"pre.forces.exact-angle", "the incline angle has an exact sine and cosine", kForcesAngle, 1},
    {"pre.forces.input-dimensions", "every supplied quantity has its required dimension",
     kDimensionalAnalysis, 1},
    {"pre.forces.friction-declared",
     "the friction model and, for kinetic friction, the direction of motion are declared",
     kForcesDeclaration, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kForcesInputDimensions[] = {
    {"obl.forces.input-dimensions", "every supplied quantity carries its required dimension",
     kDimensionalAnalysis, 1},
};
const ObligationSchema kForcesExactAngle[] = {
    {"obl.forces.exact-angle", "the angle's sine and cosine are exact rationals", kForcesAngle, 1},
};
const EvidenceAlternative kForcesExactProduct[] = {
    {"exact rational product", EvidenceStrength::DimensionallyValid},
};
const ObligationSchema kForcesWeightComponents[] = {
    {"obl.forces.weight-components",
     "weight is mass times gravity and its components are its exact incline projections",
     kForcesExactProduct, 1},
};
const EvidenceAlternative kForcesAcrossAxis[] = {
    {"exact across-axis sum", EvidenceStrength::DimensionallyValid},
};
const ObligationSchema kForcesNormalFromBalance[] = {
    {"obl.forces.normal-from-balance", "the normal force makes the exact across-axis sum zero",
     kForcesAcrossAxis, 1},
};
const EvidenceAlternative kForcesPairComparison[] = {
    {"inventory and pair comparison", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kForcesPairsSeparate[] = {
    {"obl.forces.pairs-separate", "no third-law reaction appears in the inventory of this body",
     kForcesPairComparison, 1},
};
const EvidenceAlternative kForcesStaticLimit[] = {
    {"exact comparison of required friction against mu_s N", EvidenceStrength::DimensionallyValid},
};
const ObligationSchema kForcesStaticWithinLimit[] = {
    {"obl.forces.static-within-limit",
     "the friction equilibrium requires does not exceed mu_s N", kForcesStaticLimit, 1},
};
const ObligationSchema kForcesKineticFriction[] = {
    {"obl.forces.kinetic-friction",
     "kinetic friction has magnitude mu_k N and points opposite the declared motion",
     kForcesExactProduct, 1},
};
const EvidenceAlternative kForcesExactRearrangement[] = {
    {"exact rearrangement", EvidenceStrength::DimensionallyValid},
};
const ObligationSchema kForcesUnknownIsolated[] = {
    {"obl.forces.unknown-isolated",
     "exact rearrangement isolates the requested unknown from its force-balance equation",
     kForcesExactRearrangement, 1},
};
const EvidenceAlternative kForcesResidualEvidence[] = {
    {"exact substitution into the along-axis sum",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};
const ObligationSchema kForcesResidualZero[] = {
    {"obl.forces.residual-zero", "the along-axis force sum minus m a is exactly zero",
     kForcesResidualEvidence, 1},
};
const ObligationSchema kForcesResultDimension[] = {
    {"obl.forces.result-dimension", "the reported answer has the dimension its unit claims",
     kDimensionalAnalysis, 1},
};
const EvidenceAlternative kZeroFactorInvariant[] = {
    {"rule-local invariant", EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};
const ObligationSchema kZeroFactorEliminatesTerm[] = {
    {"obl.physics.zero-factor-eliminates-term", "a product with a zero factor equals zero",
     kZeroFactorInvariant, 1},
};

// catch-up
const EvidenceAlternative kMotionModels[] = {
    {"validated motion models", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kCatchUpStrategy[] = {
    {"pre.catch-up.constant-velocity",
     "each body has constant velocity after its stated start time", kMotionModels, 1},
    {"pre.catch-up.shared-axis", "both positions use the same one-dimensional coordinate axis",
     kFrameIdentity, 1},
    {"pre.catch-up.shared-active-domain", "an event is admissible only when both bodies are active",
     kBoundaryComparison, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const EvidenceAlternative kBodyIntervalIdentity[] = {
    {"typed body and interval identity", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kEqualPositionIsTheEvent[] = {
    {"obl.catch-up.equal-position-is-the-event",
     "the event the two bodies share is one position at one time on one axis",
     kBodyIntervalIdentity, 1},
};
const ObligationSchema kCatchUpDimensions[] = {
    {"obl.catch-up.position-law-dimensions",
     "both active-interval position laws have length dimension", kTypedDimensionalAnalysis, 1},
};
const ObligationSchema kCatchUpInDomain[] = {
    {"obl.catch-up.candidate-in-domain",
     "the candidate time belongs to both bodies' active intervals", kBoundaryComparison, 1},
};
const EvidenceAlternative kSubstituteFirstLaw[] = {
    {"exact substitution into the original position law", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kCatchUpFirstPosition[] = {
    {"obl.catch-up.first-position",
     "the candidate can be substituted into the first original position law", kSubstituteFirstLaw,
     1},
};
const EvidenceAlternative kSubstituteBothLaws[] = {
    {"exact substitution into both original position laws", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kCatchUpSecondPosition[] = {
    {"obl.catch-up.second-position",
     "the candidate gives the same position in the second original position law",
     kSubstituteBothLaws, 1},
};
const ObligationSchema kReportedWithinHalfPlace[] = {
    {"obl.physics.reported-within-half-place",
     "the reported value is within half a unit in the last place of the exact one",
     kUnroundedComparison, 1},
};

// density
const EvidenceAlternative kDensityVariableModel[] = {
    {"registered density-variable model", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kDensityStrategy[] = {
    {"pre.density.compatible-dimensions", "mass, volume and density use compatible dimensions",
     kDimensionalAnalysis, 1},
    {"pre.density.linear-unknown", "the density definition is linear in the requested unknown",
     kDensityVariableModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kDensityDimensions[] = {
    {"obl.density.dimensions-agree", "dimension of mass equals dimension of density times volume",
     kDimensionalAnalysis, 1},
};
const EvidenceAlternative kSubstituteOriginalRelation[] = {
    {"exact substitution into the original relation", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kDensityCandidate[] = {
    {"obl.density.candidate-satisfies", "the candidate satisfies the original density definition",
     kSubstituteOriginalRelation, 1},
};
const EvidenceAlternative kUnitTableScale[] = {
    {"divide each stored value by its table scale and compare with the given",
     EvidenceStrength::CandidateChecked},
};
const ObligationSchema kScalePreservesSolutions[] = {
    {"obl.physics.scale-preserves-solutions",
     "each quantity's stored SI value is its given value times the table factor",
     kUnitTableScale, 1},
};
const EvidenceAlternative kKnownQuantityLookup[] = {
    {"typed known-quantity lookup", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kLookupPreservesSolutions[] = {
    {"obl.physics.lookup-preserves-solutions",
     "the value put in place of a symbol is the one the problem declared for it",
     kKnownQuantityLookup, 1},
};

// Relation-driven families, whose obligation ids embed the model's own rule prefix.
const EvidenceAlternative kRelationPositionModel[] = {
    {"registered relation-position model", EvidenceStrength::StructurallyValid},
};

const ObligationSchema kCircularPeriodStrategy[] = {
    {"physics.circular-motion.period.compatible-dimensions",
     "every quantity in T = 2*pi*r*v^-1 uses compatible dimensions", kDimensionalAnalysis, 1},
    {"physics.circular-motion.period.linear-unknown",
     "the relation is linear in the requested unknown", kRelationPositionModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kCircularPeriodDimensions[] = {
    {"physics.circular-motion.period.dimensions-agree",
     "both sides of T = 2*pi*r*v^-1 have the same dimension", kDimensionalAnalysis, 1},
};
const ObligationSchema kCircularPeriodCandidate[] = {
    {"physics.circular-motion.period.candidate-satisfies",
     "the candidate satisfies T = 2*pi*r*v^-1", kSubstituteOriginalRelation, 1},
};

const ObligationSchema kCircularSpeedStrategy[] = {
    {"physics.circular-motion.speed.compatible-dimensions",
     "every quantity in v = 2*pi*r*T^-1 uses compatible dimensions", kDimensionalAnalysis, 1},
    {"physics.circular-motion.speed.linear-unknown",
     "the relation is linear in the requested unknown", kRelationPositionModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kCircularSpeedDimensions[] = {
    {"physics.circular-motion.speed.dimensions-agree",
     "both sides of v = 2*pi*r*T^-1 have the same dimension", kDimensionalAnalysis, 1},
};
const ObligationSchema kCircularSpeedCandidate[] = {
    {"physics.circular-motion.speed.candidate-satisfies",
     "the candidate satisfies v = 2*pi*r*T^-1", kSubstituteOriginalRelation, 1},
};

const ObligationSchema kCircularRadiusStrategy[] = {
    {"physics.circular-motion.radius.compatible-dimensions",
     "every quantity in r = (1/(2*pi))*v*T uses compatible dimensions", kDimensionalAnalysis, 1},
    {"physics.circular-motion.radius.linear-unknown",
     "the relation is linear in the requested unknown", kRelationPositionModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kCircularRadiusDimensions[] = {
    {"physics.circular-motion.radius.dimensions-agree",
     "both sides of r = (1/(2*pi))*v*T have the same dimension", kDimensionalAnalysis, 1},
};
const ObligationSchema kCircularRadiusCandidate[] = {
    {"physics.circular-motion.radius.candidate-satisfies",
     "the candidate satisfies r = (1/(2*pi))*v*T", kSubstituteOriginalRelation, 1},
};

const ObligationSchema kCircularAccelerationStrategy[] = {
    {"physics.circular-motion.acceleration.compatible-dimensions",
     "every quantity in a = v^2*r^-1 uses compatible dimensions", kDimensionalAnalysis, 1},
    {"physics.circular-motion.acceleration.linear-unknown",
     "the relation is linear in the requested unknown", kRelationPositionModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kCircularAccelerationDimensions[] = {
    {"physics.circular-motion.acceleration.dimensions-agree",
     "both sides of a = v^2*r^-1 have the same dimension", kDimensionalAnalysis, 1},
};
const ObligationSchema kCircularAccelerationCandidate[] = {
    {"physics.circular-motion.acceleration.candidate-satisfies",
     "the candidate satisfies a = v^2*r^-1", kSubstituteOriginalRelation, 1},
};

const ObligationSchema kGravitationStrategy[] = {
    {"physics.gravitation.compatible-dimensions",
     "every quantity in F = G*m1*m2*r^-2 uses compatible dimensions", kDimensionalAnalysis, 1},
    {"physics.gravitation.linear-unknown", "the relation is linear in the requested unknown",
     kRelationPositionModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kGravitationDimensions[] = {
    {"physics.gravitation.dimensions-agree",
     "both sides of F = G*m1*m2*r^-2 have the same dimension", kDimensionalAnalysis, 1},
};
const ObligationSchema kGravitationCandidate[] = {
    {"physics.gravitation.candidate-satisfies", "the candidate satisfies F = G*m1*m2*r^-2",
     kSubstituteOriginalRelation, 1},
};

const ObligationSchema kOscillationStrategy[] = {
    {"physics.oscillation.compatible-dimensions",
     "every quantity in F = k*x uses compatible dimensions", kDimensionalAnalysis, 1},
    {"physics.oscillation.linear-unknown", "the relation is linear in the requested unknown",
     kRelationPositionModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kOscillationDimensions[] = {
    {"physics.oscillation.dimensions-agree", "both sides of F = k*x have the same dimension",
     kDimensionalAnalysis, 1},
};
const ObligationSchema kOscillationCandidate[] = {
    {"physics.oscillation.candidate-satisfies", "the candidate satisfies F = k*x",
     kSubstituteOriginalRelation, 1},
};

const ObligationSchema kWaveStrategy[] = {
    {"physics.wave.compatible-dimensions",
     "every quantity in v = f*lambda uses compatible dimensions", kDimensionalAnalysis, 1},
    {"physics.wave.linear-unknown", "the relation is linear in the requested unknown",
     kRelationPositionModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kWaveDimensions[] = {
    {"physics.wave.dimensions-agree", "both sides of v = f*lambda have the same dimension",
     kDimensionalAnalysis, 1},
};
const ObligationSchema kWaveCandidate[] = {
    {"physics.wave.candidate-satisfies", "the candidate satisfies v = f*lambda",
     kSubstituteOriginalRelation, 1},
};

// modern
const EvidenceAlternative kModernRelationModel[] = {
    {"registered relation model", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kLightSpeedComparison[] = {
    {"exact comparison against the speed of light", EvidenceStrength::CandidateChecked},
};
const EvidenceAlternative kExactRationalIdentity[] = {
    {"exact rational identity", EvidenceStrength::CandidateChecked},
};
const EvidenceAlternative kInvariantComparison[] = {
    {"exact invariant comparison", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kRelativityStrategy[] = {
    {"pre.relativity.frames-named", "the two frames are named and distinct", kFrameDeclaration, 1},
    {"pre.relativity.subluminal-boost", "the boost speed is below the speed of light",
     kLightSpeedComparison, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kRelativityFrames[] = {
    {"obl.relativity.frames-distinct", "the answer names two distinct inertial frames",
     kFrameDeclaration, 1},
};
const ObligationSchema kRelativitySpeedLimit[] = {
    {"obl.relativity.speed-below-light", "the boost speed is strictly below the speed of light",
     kLightSpeedComparison, 1},
};
const ObligationSchema kRelativityFactor[] = {
    {"obl.relativity.factor-identity",
     "the reported factor satisfies gamma^2 (1 - beta^2) = 1", kExactRationalIdentity, 1},
};
const ObligationSchema kRelativityInvariant[] = {
    {"obl.relativity.invariant-holds", "the reported values satisfy the frame invariant",
     kInvariantComparison, 1},
};
const ObligationSchema kModernStrategy[] = {
    {"pre.modern.compatible-dimensions", "both sides of the relation carry the same dimension",
     kDimensionalAnalysis, 1},
    {"pre.modern.linear-unknown", "the relation is linear in the requested unknown",
     kModernRelationModel, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kModernDimensions[] = {
    {"obl.modern.dimensions-agree", "the two sides of the relation carry the same dimension",
     kDimensionalAnalysis, 1},
};
const ObligationSchema kModernCandidate[] = {
    {"obl.modern.candidate-satisfies", "the candidate satisfies the original relation",
     kSubstituteOriginalRelation, 1},
};

// relative motion
const ObligationSchema kRelativeMotionStrategy[] = {
    {"pre.relative-motion.rank-two", "both inputs are two-dimensional velocities", kRankComparison,
     1},
    {"pre.relative-motion.frames-declared", "both velocities declare named frames",
     kFrameDeclaration, 1},
    {"pre.relative-motion.frames-match", "both velocities use the same frame", kFrameIdentity, 1},
    {"pre.relative-motion.axes", "positive i is east and positive j is north",
     kCoordinateConvention, 1},
    {"pre.relative-motion.velocity-dimensions", "both inputs have velocity dimension",
     kDimensionalAnalysis, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kRelativeFramesDeclared[] = {
    {"obl.relative-motion.frames-declared", "both velocity vectors declare a nonempty frame",
     kFrameDeclaration, 1},
};
const ObligationSchema kRelativeFramesMatch[] = {
    {"obl.relative-motion.frames-match", "both velocities use the same frame", kFrameIdentity, 1},
};
const ObligationSchema kRelativeVelocityDimensions[] = {
    {"obl.relative-motion.velocity-dimensions", "both supplied vectors are velocities",
     kDimensionalAnalysis, 1},
};
const ObligationSchema kRelativeRankTwo[] = {
    {"obl.relative-motion.rank-two", "both velocity vectors have rank two", kRankComparison, 1},
};
const ObligationSchema kRelativeResultDimension[] = {
    {"obl.relative-motion.result-dimension", "the relative velocity has dimension L T^-1",
     kDimensionalAnalysis, 1},
};
const ObligationSchema kRelativeComponentI[] = {
    {"obl.relative-motion.component-i",
     "the i component equals subject velocity minus reference velocity",
     kRelativeComponentSubtraction, 2},
};
const ObligationSchema kRelativeComponentJ[] = {
    {"obl.relative-motion.component-j",
     "the j component equals subject velocity minus reference velocity",
     kRelativeComponentSubtraction, 2},
};
const ObligationSchema kRelativeDefinitionAfterChecks[] = {
    {"obl.relative-motion.definition-after-checks",
     "the relative velocity definition is applied only after its conditions pass", kLawApplicability,
     1},
};
const ObligationSchema kRelativeDirection[] = {
    {"obl.relative-motion.direction-interpreted",
     "the relative velocity direction follows both component signs", kExactRationalSign, 1},
};
const EvidenceAlternative kRelativeBearingEvidence[] = {
    {"nearest cardinal by component magnitude", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kRelativeBearingConvention[] = {
    {"obl.relative-motion.bearing-convention-stated",
     "the reported angle names both the cardinal it starts from and the one it turns toward",
     kRelativeBearingEvidence, 1},
};
const EvidenceAlternative kRelativeSubscriptEvidence[] = {
    {"subscript chain", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kRelativeSubscriptCancellation[] = {
    {"obl.relative-motion.subscript-cancellation",
     "the inner subscript cancels between the two added velocities", kRelativeSubscriptEvidence, 1},
};
const EvidenceAlternative kRelativeIsolationEvidence[] = {
    {"symbolic rearrangement of the subscript identity", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kRelativeIsolation[] = {
    {"obl.relative-motion.isolate-before-substitute",
     "the unknown is isolated symbolically before a number is substituted",
     kRelativeIsolationEvidence, 1},
};
const EvidenceAlternative kRelativeRoundingEvidence[] = {
    {"exact comparison against the unrounded value", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kRelativeRounding[] = {
    {"obl.relative-motion.rounding-within-half-place",
     "the reported value is within half a unit in its last place of the exact value",
     kRelativeRoundingEvidence, 1},
};
const ObligationSchema kConvertsByTable[] = {
    {"obl.physics.converts-by-table",
     "the SI form comes from the unit table's exact factor for the entered unit", kUnitTable, 1},
};

// physics.optics
const EvidenceAlternative kOpticalDomainRules[] = {
    {"registered optical domain rules", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kRelationConvention[] = {
    {"registered relation convention", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kOpticsRelationSubstitution[] = {
    {"exact substitution into the original relation", EvidenceStrength::CandidateChecked},
};
const EvidenceAlternative kCriticalSineComparison[] = {
    {"exact comparison against one", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kOpticsStrategy[] = {
    {"pre.optics.compatible-dimensions", "both sides of the relation have the same dimension",
     kDimensionalAnalysis, 1},
    {"pre.optics.declared-convention", "the sign and order convention is declared before it is used",
     kRelationConvention, 1},
    {"pre.optics.physical-givens", "every given quantity is inside this family's stated domain",
     kOpticalDomainRules, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kOpticsDimensions[] = {
    {"obl.optics.dimensions-agree", "both sides of the optical relation have one dimension",
     kDimensionalAnalysis, 1},
};
const ObligationSchema kOpticsDomain[] = {
    {"obl.optics.domain-holds",
     "every quantity the relation uses lies inside this family's stated domain",
     kOpticalDomainRules, 1},
};
const ObligationSchema kOpticsConvention[] = {
    {"obl.optics.convention-declared",
     "the sign and order convention is stated before any signed answer is read",
     kRelationConvention, 1},
};
const ObligationSchema kOpticsCandidate[] = {
    {"obl.optics.candidate-satisfies", "the candidate satisfies the original optical relation",
     kOpticsRelationSubstitution, 1},
};
const ObligationSchema kOpticsCriticalAngle[] = {
    {"obl.optics.critical-angle",
     "a transmitted sine above one is reported as total internal reflection",
     kCriticalSineComparison, 1},
};

// work
const ObligationSchema kWorkStrategy[] = {
    {"pre.work.constant-force", "the supplied force is constant over the displacement",
     kDeclaredForceProfile, 1},
    {"pre.work.matching-rank", "both vectors have matching rank two or three", kRankComparison, 1},
    {"pre.work.frames-declared", "both vectors declare a named frame", kFrameDeclaration, 1},
    {"pre.work.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
    {"pre.work.input-dimensions", "force and displacement have their required dimensions",
     kDimensionalAnalysis, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kWorkConstantForce[] = {
    {"obl.work.constant-force", "the force is constant over the displacement", kDeclaredForceProfile,
     1},
};
const EvidenceAlternative kVectorDotConstruction[] = {
    {"exact construction through vector_dot", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kWorkCandidate[] = {
    {"obl.work.candidate-satisfies",
     "the candidate is the exact value of the original component dot product",
     kVectorDotConstruction, 1},
};
const ObligationSchema kWorkFramesDeclared[] = {
    {"obl.work.frames-declared", "both vectors declare a nonempty frame", kFrameDeclaration, 1},
};
const ObligationSchema kWorkFramesMatch[] = {
    {"obl.work.frames-match", "force and displacement use the same frame", kFrameIdentity, 1},
};
const ObligationSchema kWorkInputDimensions[] = {
    {"obl.work.input-dimensions", "force is M L T^-2 and displacement is L", kDimensionalAnalysis,
     1},
};
const ObligationSchema kWorkRanksMatch[] = {
    {"obl.work.ranks-match", "both vectors have matching rank two or three", kRankComparison, 1},
};
const ObligationSchema kWorkResultDimension[] = {
    {"obl.work.result-dimension", "work has dimension M L^2 T^-2", kDimensionalMultiplication, 1},
};
const ObligationSchema kWorkLawAfterChecks[] = {
    {"obl.work.law-applied-after-checks", "all applicability checks precede the work definition",
     kLawApplicability, 1},
};
const EvidenceAlternative kVectorDot[] = {
    {"nps vector_dot", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kDotIsTheDefinition[] = {
    {"obl.work.dot-is-the-definition",
     "the value written down is the component dot product the definition names", kVectorDot, 1},
};
const ObligationSchema kWorkSign[] = {
    {"obl.work.sign-interpreted", "the work sign is interpreted from the exact value",
     kExactRationalSign, 1},
};
const EvidenceAlternative kWorkBackendEvidence[] = {
    {"Giac Adapter Op::Dot and local canonical comparison",
     EvidenceStrength::SymbolicallyEquivalentUnderAssumptions},
};
const ObligationSchema kWorkBackendAgreement[] = {
    {"obl.work.backend-agrees", "Giac's Op::Dot result equals the local result",
     kWorkBackendEvidence, 1},
};
const EvidenceAlternative kHalfPlaceComparison[] = {
    {"exact half-place comparison", EvidenceStrength::CandidateChecked},
};

// physics.planar-kinematics
const EvidenceAlternative kStageIdentity[] = {
    {"stage identity", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kProjectileSpecialization[] = {
    {"projectile specialization", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kVectorScaleAndAdd[] = {
    {"nps vector_scale and vector_add", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kAverageVelocityIdentity[] = {
    {"average-velocity identity", EvidenceStrength::CandidateChecked},
};
const EvidenceAlternative kIndependentRoutes[] = {
    {"two independent routes", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kPlanarStrategy[] = {
    {"pre.planar-kinematics.rank-two", "velocity and acceleration are two-dimensional",
     kRankComparison, 1},
    {"pre.planar-kinematics.frames-declared", "both vectors declare named frames", kFrameDeclaration,
     1},
    {"pre.planar-kinematics.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
    {"pre.planar-kinematics.axes", "positive i is right and positive j is up", kCoordinateConvention,
     1},
    {"pre.planar-kinematics.stages", "each quantity is used at the stage it belongs to",
     kStageIdentity, 1},
    {"pre.planar-kinematics.dimensions",
     "velocity, acceleration and time carry their own dimensions", kDimensionalAnalysis, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
// The projectile plan is its own rule because it registers one precondition more than the general
// one, and a schema is a fixed obligation set rather than a superset a run may skip part of.
const ObligationSchema kProjectilePlanStrategy[] = {
    {"pre.planar-kinematics.rank-two", "velocity and acceleration are two-dimensional",
     kRankComparison, 1},
    {"pre.planar-kinematics.frames-declared", "both vectors declare named frames", kFrameDeclaration,
     1},
    {"pre.planar-kinematics.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
    {"pre.planar-kinematics.axes", "positive i is right and positive j is up", kCoordinateConvention,
     1},
    {"pre.planar-kinematics.stages", "each quantity is used at the stage it belongs to",
     kStageIdentity, 1},
    {"pre.planar-kinematics.dimensions",
     "velocity, acceleration and time carry their own dimensions", kDimensionalAnalysis, 1},
    {"pre.planar-kinematics.projectile",
     "the horizontal axis is unaccelerated and gravity acts down", kProjectileSpecialization, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kPlanarApexStrategy[] = {
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kPlanarRankTwo[] = {
    {"obl.planar-kinematics.rank-two", "both vectors have rank two", kRankComparison, 1},
};
const ObligationSchema kPlanarFramesDeclared[] = {
    {"obl.planar-kinematics.frames-declared", "both vectors declare a nonempty frame",
     kFrameDeclaration, 1},
};
const ObligationSchema kPlanarFramesMatch[] = {
    {"obl.planar-kinematics.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
};
const ObligationSchema kPlanarStageIdentity[] = {
    {"obl.planar-kinematics.stage-identity",
     "the velocity is a state and the acceleration and time span the interval", kStageIdentity, 1},
};
const ObligationSchema kPlanarInputDimensions[] = {
    {"obl.planar-kinematics.input-dimensions",
     "the inputs are a velocity, an acceleration and a time", kDimensionalAnalysis, 1},
};
const ObligationSchema kPlanarProjectile[] = {
    {"obl.planar-kinematics.projectile",
     "the horizontal component is zero and the vertical one points down", kProjectileSpecialization,
     1},
};
const ObligationSchema kPlanarResultDimensions[] = {
    {"obl.planar-kinematics.result-dimensions",
     "the displacement is a length and the final velocity is a velocity", kDimensionalAnalysis, 1},
};
const ObligationSchema kPlanarDefinitionAfterChecks[] = {
    {"obl.planar-kinematics.definition-after-checks",
     "the constant-acceleration relations are applied only after their conditions pass",
     kLawApplicability, 1},
};
const ObligationSchema kPlanarComponentI[] = {
    {"obl.planar-kinematics.component-i", "the i displacement equals v0 t + a t^2 / 2",
     kVectorScaleAndAdd, 1},
};
const ObligationSchema kPlanarComponentJ[] = {
    {"obl.planar-kinematics.component-j", "the j displacement equals v0 t + a t^2 / 2",
     kVectorScaleAndAdd, 1},
};
const ObligationSchema kPlanarSharedTime[] = {
    {"obl.planar-kinematics.shared-time",
     "each axis satisfies s = (v0 + v) t / 2 with the same elapsed time", kAverageVelocityIdentity,
     1},
};
const ObligationSchema kPlanarApexRoutes[] = {
    {"obl.planar-kinematics.apex-routes-agree",
     "the time-to-apex route and the direct v^2 = v0^2 + 2 a x route reach the same height",
     kIndependentRoutes, 1},
};
const ObligationSchema kWorkRounding[] = {
    {"obl.work.rounding-within-half-place",
     "the reported value is within half a unit in the last place of the exact one",
     kHalfPlaceComparison, 1},
};

// unit conversion
const EvidenceAlternative kExactRationalOperations[] = {
    {"exact rational operations", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kUnitConvertStrategy[] = {
    {"pre.unit-convert.dimensions-match", "source and target units have equal dimensions",
     kDimensionComparison, 1},
    {"pre.unit-convert.exact-factors", "every conversion factor fits exact rational arithmetic",
     kExactRationalOperations, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kUnitDimensionsMatch[] = {
    {"obl.unit-conversion.dimensions-match", "source and target dimensions are equal",
     kDimensionComparison, 1},
};
const ObligationSchema kUnitRoundingFinal[] = {
    {"obl.unit-conversion.rounding-final", "precision is applied once to the final value",
     kSignificantFigures, 1},
    // Its own obligation rather than a second method under the one above, because when the report
    // was rounded and whether it was rounded correctly are different claims, and alternatives to
    // one obligation are interchangeable: the first matching record would answer for both.
    {"obl.unit-conversion.rounding-within-half-place",
     "the reported value is within half a unit in the last place of the exact one",
     kHalfPlaceComparison, 1},
};
const ObligationSchema kUnitSourceFactor[] = {
    {"obl.unit-conversion.source-factor-exact", "the source-to-SI factor is exact", kUnitTable, 1},
};
const ObligationSchema kUnitTargetFactor[] = {
    {"obl.unit-conversion.target-factor-exact", "the SI-to-target factor is exact", kUnitTable, 1},
};

// vector addition
const ObligationSchema kVectorAddStrategy[] = {
    {"pre.vector-add.ranks-match", "both vectors have matching rank two or three",
     kRankComparison, 1},
    {"pre.vector-add.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
    {"pre.vector-add.dimensions-match", "both vectors have the same physical dimension",
     kDimensionComparison, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kVectorAddDimensions[] = {
    {"obl.vector-add.dimensions-match", "both input vectors have the same physical dimension",
     kDimensionComparison, 1},
};
const ObligationSchema kVectorAddFrames[] = {
    {"obl.vector-add.frames-match", "both input vectors use the same frame", kFrameIdentity, 1},
};
const ObligationSchema kVectorAddRank[] = {
    {"obl.vector-add.ranks-match", "both input vectors have matching rank two or three",
     kRankComparison, 1},
};
const ObligationSchema kVectorAddRounding[] = {
    {"obl.vector-add.rounding-final",
     "precision is applied once after exact component addition", kSignificantFigures, 1},
    // Separate from the obligation above for the reason given at kUnitRoundingFinal: when the
    // report was rounded and whether it was rounded correctly are different claims.
    {"obl.vector-add.rounding-within-half-place",
     "the reported vector is within half a unit in the last place of the exact one",
     kHalfPlaceComparison, 1},
};
const EvidenceAlternative kExactRationalAddition[] = {
    {"exact rational addition", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kComponentSum[] = {
    {"obl.vector-add.component-sum",
     "the component written down is the exact sum of the two input components",
     kExactRationalAddition, 1},
};

// vector cross product
const ObligationSchema kVectorCrossStrategy[] = {
    {"pre.vector-cross.rank-three", "both vectors have exactly rank three", kRankComparison, 1},
    {"pre.vector-cross.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
    {"pre.vector-cross.dimension-product",
     "the product of the two vectors' dimensions fits the dimension table",
     kDimensionalMultiplication, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kVectorCrossRank[] = {
    {"obl.vector-cross.rank-three", "both input vectors have exactly rank three", kRankComparison,
     1},
};
const ObligationSchema kVectorCrossFrames[] = {
    {"obl.vector-cross.frames-match", "both input vectors use the same frame", kFrameIdentity, 1},
};
const ObligationSchema kVectorCrossDimensions[] = {
    {"obl.vector-cross.dimension-product",
     "the product of the two operand dimensions fits the dimension table",
     kDimensionalMultiplication, 1},
};
const ObligationSchema kComponentCross[] = {
    {"obl.vector-cross.component-cross",
     "the component written down is the exact determinant expansion of the two input components",
     kExactRationalArithmetic, 1},
};
// Its own method rather than kVectorDot, which asserts where a value came from at
// StructurallyValid. This one is a candidate checked against zero after the fact, which is a
// different claim and a different strength.
const EvidenceAlternative kExactDotProduct[] = {
    {"exact dot product", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kVectorCrossOrthogonalFirst[] = {
    {"obl.vector-cross.orthogonal-first", "the result vector is orthogonal to the first operand",
     kExactDotProduct, 1},
};
const ObligationSchema kVectorCrossOrthogonalSecond[] = {
    {"obl.vector-cross.orthogonal-second", "the result vector is orthogonal to the second operand",
     kExactDotProduct, 1},
};
const EvidenceAlternative kReversedCrossProduct[] = {
    {"exact reversed cross product", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kVectorCrossAnticommutative[] = {
    {"obl.vector-cross.anticommutative",
     "crossing the operands in the opposite order negates the result exactly",
     kReversedCrossProduct, 1},
};
const ObligationSchema kVectorCrossRounding[] = {
    {"obl.vector-cross.rounding-final", "precision is applied once after exact component cross",
     kSignificantFigures, 1},
    // Separate from the obligation above for the reason given at kUnitRoundingFinal: when the
    // report was rounded and whether it was rounded correctly are different claims.
    {"obl.vector-cross.rounding-within-half-place",
     "the reported vector is within half a unit in the last place of the exact one",
     kHalfPlaceComparison, 1},
};

// scalar product
const EvidenceAlternative kComponentComparison[] = {
    {"component comparison", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kScalarProductStrategy[] = {
    {"pre.scalar-product.ranks-match", "both vectors have the same number of components",
     kRankComparison, 1},
    {"pre.scalar-product.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
    {"pre.scalar-product.dimension-product",
     "the product of the two vectors' dimensions fits the dimension table",
     kDimensionalMultiplication, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
// The angle plan is its own rule because it registers one precondition more than the product plan,
// and a schema is a fixed obligation set rather than a superset a run may skip part of.
const ObligationSchema kScalarAngleStrategy[] = {
    {"pre.scalar-product.ranks-match", "both vectors have the same number of components",
     kRankComparison, 1},
    {"pre.scalar-product.frames-match", "both vectors use the same frame", kFrameIdentity, 1},
    {"pre.scalar-product.dimension-product",
     "the product of the two vectors' dimensions fits the dimension table",
     kDimensionalMultiplication, 1},
    {"pre.scalar-product.nonzero", "neither vector is the zero vector", kComponentComparison, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kScalarProductRanks[] = {
    {"obl.scalar-product.ranks-match",
     "both input vectors have the same number of components", kRankComparison, 1},
};
const ObligationSchema kScalarProductFrames[] = {
    {"obl.scalar-product.frames-match", "both input vectors use the same frame", kFrameIdentity, 1},
};
const ObligationSchema kScalarProductDimensions[] = {
    {"obl.scalar-product.dimension-product",
     "the product of the two operand dimensions fits the dimension table",
     kDimensionalMultiplication, 1},
};
const ObligationSchema kScalarProductNonzero[] = {
    {"obl.scalar-product.nonzero", "neither operand is the zero vector", kComponentComparison, 1},
};
const ObligationSchema kScalarProductDefinition[] = {
    {"obl.scalar-product.definition-after-checks",
     "the scalar product definition is applied only after its conditions pass", kLawApplicability,
     1},
};
const ObligationSchema kScalarProductSum[] = {
    {"obl.scalar-product.sum-is-the-definition",
     "the value written down is the component sum the definition names", kVectorDot, 1},
};
const EvidenceAlternative kReversedDotProduct[] = {
    {"exact reversed dot product", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kScalarProductCommutative[] = {
    {"obl.scalar-product.commutative",
     "dotting the operands in the opposite order gives the same value", kReversedDotProduct, 1},
};
// The geometric reading checked without a square root: a b cos(phi) has |cos(phi)| at most one.
const EvidenceAlternative kCauchySchwarz[] = {
    {"exact Cauchy-Schwarz comparison", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kScalarProductBound[] = {
    {"obl.scalar-product.within-magnitude-bound",
     "the square of the scalar product is at most the product of the two self-products",
     kCauchySchwarz, 1},
};
const EvidenceAlternative kExactSignComparison[] = {
    {"exact sign comparison", EvidenceStrength::CandidateChecked},
};
const ObligationSchema kScalarProductAngle[] = {
    {"obl.scalar-product.angle-from-sign",
     "the sign of the scalar product places the angle against a right angle", kExactSignComparison,
     1},
};
// vector components and polar form
const ObligationSchema kVectorSphericalStrategy[] = {
    {"pre.vector-components.rank-three", "the conversion is three-dimensional", kRankComparison, 1},
    {"pre.vector-components.spherical-convention",
     "the spherical angle convention is declared before any angle is computed", kFrameDeclaration,
     1},
    {"pre.vector-components.frame-declared", "the vector frame is declared", kFrameDeclaration, 1},
    {"pre.vector-components.angle-unit", "the angle unit is degrees or radians",
     kAngleUnitValidation, 1},
    {"pre.vector-components.dimensions-preserved",
     "magnitude and Cartesian components carry the same physical dimension", kDimensionalAnalysis,
     1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kVectorComponentsStrategy[] = {
    {"pre.vector-components.rank-two", "the conversion is two-dimensional", kRankComparison, 1},
    {"pre.vector-components.frame-declared", "the vector frame is declared", kFrameDeclaration, 1},
    {"pre.vector-components.angle-unit", "the angle unit is degrees or radians",
     kAngleUnitValidation, 1},
    {"pre.vector-components.dimensions-preserved",
     "magnitude and Cartesian components carry the same physical dimension", kDimensionalAnalysis,
     1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const ObligationSchema kAngleUnitExplicit[] = {
    {"obl.vector-components.angle-unit-explicit",
     "the angle unit is explicitly degrees or radians", kAngleUnitValidation, 1},
};
const ObligationSchema kComponentsDimensions[] = {
    {"obl.vector-components.dimensions-preserved",
     "magnitude and Cartesian components have the same physical dimension", kDimensionalAnalysis,
     1},
};
const ObligationSchema kComponentsFrame[] = {
    {"obl.vector-components.frame-declared", "the vector frame is declared", kFrameDeclaration, 1},
};
const ObligationSchema kComponentsRankTwo[] = {
    {"obl.vector-components.rank-two", "the Cartesian vector has rank two", kRankComparison, 1},
};
const ObligationSchema kComponentsRankThree[] = {
    {"obl.vector-components.rank-three", "the Cartesian vector has rank three", kRankComparison, 1},
};
const ObligationSchema kSphericalConvention[] = {
    {"obl.vector-components.spherical-convention",
     "the polar angle is measured from the positive z axis and the azimuth from the positive x axis",
     kFrameDeclaration, 1},
};
const ObligationSchema kPolarAngleRelation[] = {
    {"obl.vector-components.polar-angle",
     "the polar angle satisfies theta = atan2(sqrt(x^2 + y^2), z)", kGiacZero, 1},
};
// The one obligation whose text differs between two correct runs: the Cartesian pass says
// "component formulas" where the polar pass says "polar formulas". The id is what both share, and
// it is why the conformance check compares ids rather than sentences.
const ObligationSchema kComponentsPrecision[] = {
    {"obl.vector-components.precision-final",
     "precision is applied only after the exact formulas are verified", kOperationOrdering, 1},
};
const ObligationSchema kComponentRelations[] = {
    {"obl.vector-components.component-relations",
     "the Cartesian components satisfy x = r cos(theta) and y = r sin(theta)", kGiacZero, 1},
};
const ObligationSchema kMagnitudeRelation[] = {
    {"obl.vector-components.magnitude-relation", "the magnitude satisfies r = sqrt(x^2 + y^2)",
     kGiacZero, 1},
};
const ObligationSchema kQuadrantDirection[] = {
    {"obl.vector-components.quadrant-direction",
     "the direction uses atan2(y, x) and preserves the Cartesian quadrant", kGiacZero, 1},
};

// numeric mode
const ObligationSchema kDecimalReportEqualsExact[] = {
    {"obl.numeric.decimal-report-equals-exact",
     "each decimal written here equals the fraction it replaced", kRuleInvariant, 1},
};
const ObligationSchema kDecimalReadsAsWritten[] = {
    {"obl.numeric.decimal-reads-as-written",
     "the fraction written here is the one the decimal literal names", kRuleInvariant, 1},
};

const EvidenceAlternative kIntegerEnvelope[] = {
    {"integer envelope validation", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerStrategy[] = {
    {"pre.int.literal-envelope", "the arguments are integer literals within this method's bounds",
     kIntegerEnvelope, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence",
     kRegisteredPreconditions, 1},
};
const EvidenceAlternative kIntegerDivisionEvidence[] = {
    {"exact integer division identity", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerDivision[] = {
    {"obl.int.division-identity", "the dividend equals divisor times quotient plus a bounded remainder",
     kIntegerDivisionEvidence, 1},
};
const EvidenceAlternative kIntegerFactorialEvidence[] = {
    {"factorial recurrence", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerFactorial[] = {
    {"obl.int.factorial-product", "the product satisfies the factorial recurrence",
     kIntegerFactorialEvidence, 1},
};
const EvidenceAlternative kIntegerPermutationEvidence[] = {
    {"falling product recurrence", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerPermutation[] = {
    {"obl.int.permutation-product", "the product satisfies the falling product recurrence",
     kIntegerPermutationEvidence, 1},
};
const EvidenceAlternative kIntegerCombinationEvidence[] = {
    {"binomial recurrence", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerCombination[] = {
    {"obl.int.combination-product", "the binomial recurrence divides exactly",
     kIntegerCombinationEvidence, 1},
};
const ObligationSchema kIntegerTrial[] = {
    {"obl.int.trial-division", "the trial divisor gives the recorded quotient and remainder",
     kIntegerDivisionEvidence, 1},
};
const EvidenceAlternative kIntegerPrimeEvidence[] = {
    {"complete trial division", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerPrime[] = {
    {"obl.int.prime-decision", "all necessary trial divisors were checked",
     kIntegerPrimeEvidence, 1},
};
const EvidenceAlternative kIntegerNextPrimeEvidence[] = {
    {"consecutive candidate exclusion", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerNextPrime[] = {
    {"obl.int.next-prime", "every smaller candidate above the input is excluded",
     kIntegerNextPrimeEvidence, 1},
};
const EvidenceAlternative kIntegerModularPowerEvidence[] = {
    {"binary modular recurrence", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerModularPower[] = {
    {"obl.int.modular-power", "the modular multiplication preserves the stated congruence",
     kIntegerModularPowerEvidence, 1},
};
const EvidenceAlternative kIntegerFactorsEvidence[] = {
    {"prime factor reconstruction", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerFactors[] = {
    {"obl.int.factor-product", "the recorded prime factors multiply to the input",
     kIntegerFactorsEvidence, 1},
};
const EvidenceAlternative kIntegerGcdSignEvidence[] = {
    {"integer magnitude normalization", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerGcdSign[] = {
    {"obl.int.gcd-sign", "absolute values preserve the common divisors",
     kIntegerGcdSignEvidence, 1},
};
const EvidenceAlternative kIntegerGcdRemainderEvidence[] = {
    {"exact Euclidean reduction", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerGcdRemainder[] = {
    {"obl.int.gcd-remainder", "exact division preserves the common divisors of the pair",
     kIntegerGcdRemainderEvidence, 1},
};
const EvidenceAlternative kIntegerGcdCertificateEvidence[] = {
    {"common divisibility and Bezout identity", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kIntegerGcdCertificate[] = {
    {"obl.int.gcd-certificate", "the nonnegative common divisor satisfies a Bezout identity",
     kIntegerGcdCertificateEvidence, 1},
};

const EvidenceAlternative kMatrixRowEvidence[] = {
    {"exact reversible row operation", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kMatrixRow[] = {
    {"obl.matrix.row-equivalent", "an exact reversible row operation produces every recorded cell",
     kMatrixRowEvidence, 1},
};

const EvidenceAlternative kMatrixEnvelope[] = {
    {"exact matrix envelope validation", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kMatrixStrategy[] = {
    {"pre.matrix.rational-envelope", "a nonempty matrix of at most 4 by 6 has exact rational cells and provenance", kMatrixEnvelope, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence", kRegisteredPreconditions, 1},
};
const EvidenceAlternative kMatrixTrace[] = {
    {"exact row trace continuity", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kMatrixRefForm[] = {
    {"exact row echelon form", EvidenceStrength::StructurallyValid},
};
const EvidenceAlternative kMatrixRrefForm[] = {
    {"exact reduced row echelon form", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kMatrixRefConclusion[] = {
    {"obl.matrix.trace-complete", "the final matrix ends a complete verified row-operation trace", kMatrixTrace, 1},
    {"obl.matrix.ref-form", "the final matrix satisfies exact row echelon form", kMatrixRefForm, 1},
};
const ObligationSchema kMatrixRrefConclusion[] = {
    {"obl.matrix.trace-complete", "the final matrix ends a complete verified row-operation trace", kMatrixTrace, 1},
    {"obl.matrix.rref-form", "the final matrix satisfies exact reduced row echelon form", kMatrixRrefForm, 1},
};

const ObligationSchema kMatrixDeterminantStrategy[] = {
    {"pre.matrix.square-rational-envelope", "a square matrix of order 1 through 4 has exact rational cells and provenance", kMatrixEnvelope, 1},
    {"obl.plan.preconditions-hold", "every registered strategy precondition has passing evidence", kRegisteredPreconditions, 1},
};
const EvidenceAlternative kMatrixDeterminantFactor[] = {
    {"exact determinant factor law", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kMatrixDeterminantRow[] = {
    {"obl.matrix.row-equivalent", "the actual reversible row operation matches every matrix cell", kMatrixRowEvidence, 1},
    {"obl.matrix.det-factor", "the nonzero accumulated factor follows the elementary determinant law", kMatrixDeterminantFactor, 1},
};
const EvidenceAlternative kMatrixDeterminantDiagonal[] = {
    {"exact triangular determinant product", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kMatrixDeterminantProduct[] = {
    {"obl.matrix.trace-complete", "the final matrix ends the verified row trace", kMatrixTrace, 1},
    {"obl.matrix.ref-form", "the square final matrix has exact row echelon form", kMatrixRefForm, 1},
    {"obl.matrix.det-diagonal-product", "the scalar equals the triangular diagonal product", kMatrixDeterminantDiagonal, 1},
};
const EvidenceAlternative kMatrixDeterminantCorrection[] = {
    {"exact determinant factor correction", EvidenceStrength::StructurallyValid},
};
const ObligationSchema kMatrixDeterminantResult[] = {
    {"obl.matrix.det-correction", "the original determinant equals the diagonal product divided by the nonzero accumulated factor", kMatrixDeterminantCorrection, 1},
};

// One entry per rule and strategy. Ordered by family so a reader can find the block a rule belongs
// to, and looked up linearly, which costs nothing beside the work a step already did.
const RuleSchema kRules[] = {
    // algebra.linear-equation.one-unknown
    {"eq.linear.inverse-operations", ClaimType::NoClaim, kLinearStrategy, 3,
     FailureBehavior::WithholdResult},
    {"eq.collect-like-terms", ClaimType::SolutionSetPreserved, kSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"eq.divide-both-sides", ClaimType::SolutionSetPreserved, kSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"eq.linear.inspect-collected-coefficient", ClaimType::SolutionSetPreserved, kCoefficientDecides,
     1, FailureBehavior::CannotFail},
    {"eq.linear.check-by-substitution", ClaimType::SolutionSetPreserved, kCandidateSatisfies, 1,
     FailureBehavior::WithholdResult},

    // algebra.quadratic.pure-square.one-unknown. The two that cannot fail are the two whose
    // obligation is settled before the step is reached: the isolation divides by a coefficient
    // already checked non-zero, and the rejection is only recorded for a square already read as
    // negative. The three that withhold are the three that can come back false about the answer.
    {"eq.quadratic.square-root", ClaimType::NoClaim, kSquareRootStrategy, 3,
     FailureBehavior::WithholdResult},
    {"eq.quadratic.isolate-the-square", ClaimType::SolutionSetPreserved, kSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"eq.quadratic.square-root-case", ClaimType::SolutionSetNarrowed, kCaseIsARoot, 1,
     FailureBehavior::WithholdResult},
    {"eq.quadratic.check-by-substitution", ClaimType::SolutionSetPreserved,
     kQuadraticCandidateSatisfies, 1, FailureBehavior::WithholdResult},
    {"eq.quadratic.reject-negative-square", ClaimType::SolutionSetPreserved,
     kRejectedCaseIsInfeasible, 1, FailureBehavior::CannotFail},
    {"eq.quadratic.cases-reconstruct-the-original", ClaimType::SolutionSetPreserved,
     kCasesAreComplete, 1, FailureBehavior::WithholdResult},

    // calculus.differentiate. Every rule below records its invariant unconditionally, which is what
    // CannotFail says: the rule matched the form or it was never reached, so there is no run in
    // which it applies and disagrees with itself.
    {"calculus.differentiate.rules", ClaimType::NoClaim, kDifferentiateStrategy, 2,
     FailureBehavior::WithholdResult},
    {"calculus.differentiate.giac-cross-check", ClaimType::NoClaim, kDerivativeCrossCheck, 1,
     FailureBehavior::WithholdResult},
    {"d.chain", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.chain-power", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.constant", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.constant-multiple", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.function", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.power", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.product", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.quotient", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.sum", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"d.variable", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},

    {"defint.interval", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"defint.zero-width", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"defint.fundamental-theorem", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"defint.subtract", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"limit.continuity", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"limit.real-domain", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"limit.rational-form", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"limit.lhopital", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"limit.evaluate", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"limit.infinity", ClaimType::EquivalentExpression, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"limit.classify", ClaimType::NoClaim, kLimitClassification, 1, FailureBehavior::CannotFail},
    {"calculus.check-giac", ClaimType::EquivalentExpression, kCalculusGiac, 1, FailureBehavior::WithholdResult},

    // calculus.tangent-line, CALC-010
    {"tangent.point-value", ClaimType::Definition, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"tangent.slope", ClaimType::Definition, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"tangent.line", ClaimType::Definition, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"tangent.linearization", ClaimType::NoClaim, kRulePreservesValue, 1, FailureBehavior::CannotFail},
    {"tangent.check-line", ClaimType::EquivalentExpression, kTangentAgreement, 1, FailureBehavior::WithholdResult},

    // calculus.integrate
    {"calculus.integrate.rules", ClaimType::NoClaim, kIntegrateStrategy, 3,
     FailureBehavior::WithholdResult},
    {"calculus.integrate.check-by-differentiation", ClaimType::EquivalentExpression,
     kDerivativeReturnsIntegrand, 1, FailureBehavior::WithholdResult},
    {"i.constant", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"i.constant-multiple", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"i.constant-of-integration", ClaimType::FamilyUpToConstant, kFamilyAddsAConstant, 1,
     FailureBehavior::CannotFail},
    {"i.function", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"i.linear-substitution", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"i.logarithm-parts", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"i.power", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"i.reciprocal", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"i.sum", ClaimType::EquivalentExpression, kRulePreservesValue, 1,
     FailureBehavior::CannotFail},

    // algebra.expression-rewriting
    {"alg.expand.distribute-and-collect", ClaimType::NoClaim, kRewriteStrategy, 3,
     FailureBehavior::WithholdResult},
    {"alg.factor.common-then-quadratic", ClaimType::NoClaim, kRewriteStrategy, 3,
     FailureBehavior::WithholdResult},
    {"alg.simplify.fold-and-collect", ClaimType::NoClaim, kRewriteStrategy, 3,
     FailureBehavior::WithholdResult},
    {"alg.collect-like-terms", ClaimType::EquivalentExpression, kAlgRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"alg.distribute", ClaimType::EquivalentExpression, kAlgRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"alg.drop-zero-term", ClaimType::EquivalentExpression, kAlgRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"alg.gather-powers", ClaimType::EquivalentExpression, kAlgRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"alg.power-as-product", ClaimType::EquivalentExpression, kAlgRulePreservesValue, 1,
     FailureBehavior::CannotFail},
    {"alg.fold-arithmetic", ClaimType::EquivalentExpression, kFoldPreservesValue, 1,
     FailureBehavior::CannotFail},
    {"alg.factor.common-factor", ClaimType::EquivalentExpression, kFactorMultipliesBack, 1,
     FailureBehavior::CannotFail},
    {"alg.factor.difference-of-squares", ClaimType::EquivalentExpression, kFactorMultipliesBack, 1,
     FailureBehavior::CannotFail},
    {"alg.factor.product-and-sum", ClaimType::EquivalentExpression, kFactorMultipliesBack, 1,
     FailureBehavior::CannotFail},
    {"alg.rewrite.check-by-evaluation", ClaimType::EquivalentExpression, kRewriteSameValue, 1,
     FailureBehavior::WithholdResult},

    // algebra.formula-rearrangement
    {"alg.rearrange.inverse-operations", ClaimType::NoClaim, kRearrangeStrategy, 3,
     FailureBehavior::WithholdResult},
    {"alg.rearrange.divide-both-sides", ClaimType::SolutionSetPreserved, kEquationSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"alg.rearrange.drop-unit-factor", ClaimType::SolutionSetPreserved, kEquationSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"alg.rearrange.first-power", ClaimType::SolutionSetPreserved, kEquationSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"alg.rearrange.negate-both-sides", ClaimType::SolutionSetPreserved, kEquationSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"alg.rearrange.reciprocal-both-sides", ClaimType::SolutionSetPreserved, kEquationSameSolutions,
     1, FailureBehavior::CannotFail},
    {"alg.rearrange.subtract-both-sides", ClaimType::SolutionSetPreserved, kEquationSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"alg.rearrange.swap-sides", ClaimType::SolutionSetPreserved, kEquationSameSolutions, 1,
     FailureBehavior::CannotFail},
    {"alg.rearrange.check-by-substitution", ClaimType::SolutionSetPreserved, kSubstitutionIdentity,
     1, FailureBehavior::WithholdResult},

    // numeric mode
    {"num.decimal-to-rational", ClaimType::EquivalentExpression, kDecimalReadsAsWritten, 1,
     FailureBehavior::CannotFail},
    {"num.rational-to-decimal", ClaimType::EquivalentExpression, kDecimalReportEqualsExact, 1,
     FailureBehavior::CannotFail},

    // physics.kinematics.constant-acceleration. Every physics check below declares WithholdResult,
    // because a physics engine that cannot satisfy one of its own conditions refuses rather than
    // reporting a number with a caveat, which the two refusal fixtures show reaching Unsupported.
    {"physics.kinematics.constant-acceleration", ClaimType::NoClaim, kKinematicsStrategy, 4,
     FailureBehavior::WithholdResult},
    {"kin.convert-units", ClaimType::SolutionSetPreserved, kKinematicsConversion, 1,
     FailureBehavior::WithholdResult},
    {"physics.kinematics.check-dimensions", ClaimType::SolutionSetPreserved, kKinematicsDimensions,
     1, FailureBehavior::WithholdResult},
    {"kin.rearrange", ClaimType::SolutionSetPreserved, kSymbolicIsolation, 1,
     FailureBehavior::WithholdResult},
    {"kin.substitute", ClaimType::SolutionSetPreserved, kSubstitutionPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"kin.significant-figures", ClaimType::NoClaim, kRoundingWithinHalfPlace, 1,
     FailureBehavior::WithholdResult},
    {"kin.coupled.handover", ClaimType::NoClaim, nullptr, 0, FailureBehavior::CannotFail},

    {"physics.forces.plan", ClaimType::NoClaim, kForcesStrategy, 5,
     FailureBehavior::WithholdResult},
    {"physics.forces.check-input-dimensions", ClaimType::Definition, kForcesInputDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.forces.check-angle", ClaimType::Definition, kForcesExactAngle, 1,
     FailureBehavior::WithholdResult},
    {"physics.forces.weight", ClaimType::EquivalentExpression, kForcesWeightComponents, 1,
     FailureBehavior::CannotFail},
    {"physics.forces.normal-force", ClaimType::SolutionSetPreserved, kForcesNormalFromBalance, 1,
     FailureBehavior::CannotFail},
    {"physics.forces.third-law-pairs", ClaimType::Definition, kForcesPairsSeparate, 1,
     FailureBehavior::WithholdResult},
    {"physics.forces.kinetic-friction", ClaimType::EquivalentExpression, kForcesKineticFriction, 1,
     FailureBehavior::CannotFail},
    {"physics.forces.static-friction-limit", ClaimType::Definition, kForcesStaticWithinLimit, 1,
     FailureBehavior::WithholdResult},
    {"physics.forces.solve-unknown", ClaimType::SolutionSetPreserved, kForcesUnknownIsolated, 1,
     FailureBehavior::CannotFail},
    {"physics.forces.check-residual", ClaimType::Definition, kForcesResidualZero, 1,
     FailureBehavior::WithholdResult},
    {"physics.forces.check-result-dimension", ClaimType::Definition, kForcesResultDimension, 1,
     FailureBehavior::WithholdResult},
    {"physics.term-vanishes", ClaimType::EquivalentExpression, kZeroFactorEliminatesTerm, 1,
     FailureBehavior::CannotFail},

    // physics.kinematics.catch-up.equal-position
    {"physics.catch-up.constant-velocity", ClaimType::NoClaim, kCatchUpStrategy, 4,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.equal-position", ClaimType::Definition, kEqualPositionIsTheEvent, 1,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.check-dimensions", ClaimType::Definition, kCatchUpDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.shared-domain", ClaimType::Implication, kCatchUpInDomain, 1,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.verify-first-position", ClaimType::Implication, kCatchUpFirstPosition, 1,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.verify-second-position", ClaimType::Implication, kCatchUpSecondPosition, 1,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.significant-figures", ClaimType::NoClaim, kReportedWithinHalfPlace, 1,
     FailureBehavior::WithholdResult},

    // physics.density.mass-volume
    {"physics.density.definition", ClaimType::NoClaim, kDensityStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.density.check-dimensions", ClaimType::Definition, kDensityDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.density.check-candidate", ClaimType::Implication, kDensityCandidate, 1,
     FailureBehavior::WithholdResult},
    {"physics.density.convert-units", ClaimType::SolutionSetPreserved, kScalePreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.density.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.density.significant-figures", ClaimType::NoClaim, kReportedWithinHalfPlace, 1,
     FailureBehavior::WithholdResult},

    // physics.circular-motion.uniform, one rule prefix per unknown plus the acceleration's own.
    {"physics.circular-motion.period.definition", ClaimType::NoClaim, kCircularPeriodStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.circular-motion.period.check-dimensions", ClaimType::Definition,
     kCircularPeriodDimensions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.period.substitute", ClaimType::SolutionSetPreserved,
     kLookupPreservesSolutions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.period.check-candidate", ClaimType::Implication,
     kCircularPeriodCandidate, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.speed.definition", ClaimType::NoClaim, kCircularSpeedStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.circular-motion.speed.check-dimensions", ClaimType::Definition,
     kCircularSpeedDimensions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.speed.substitute", ClaimType::SolutionSetPreserved,
     kLookupPreservesSolutions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.speed.check-candidate", ClaimType::Implication,
     kCircularSpeedCandidate, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.speed.significant-figures", ClaimType::NoClaim,
     kReportedWithinHalfPlace, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.radius.definition", ClaimType::NoClaim, kCircularRadiusStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.circular-motion.radius.check-dimensions", ClaimType::Definition,
     kCircularRadiusDimensions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.radius.substitute", ClaimType::SolutionSetPreserved,
     kLookupPreservesSolutions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.radius.check-candidate", ClaimType::Implication,
     kCircularRadiusCandidate, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.radius.significant-figures", ClaimType::NoClaim,
     kReportedWithinHalfPlace, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.acceleration.definition", ClaimType::NoClaim,
     kCircularAccelerationStrategy, 3, FailureBehavior::WithholdResult},
    {"physics.circular-motion.acceleration.check-dimensions", ClaimType::Definition,
     kCircularAccelerationDimensions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.acceleration.substitute", ClaimType::SolutionSetPreserved,
     kLookupPreservesSolutions, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.acceleration.check-candidate", ClaimType::Implication,
     kCircularAccelerationCandidate, 1, FailureBehavior::WithholdResult},
    {"physics.circular-motion.acceleration.significant-figures", ClaimType::NoClaim,
     kReportedWithinHalfPlace, 1, FailureBehavior::WithholdResult},

    // physics.gravitation.point-masses
    {"physics.gravitation.definition", ClaimType::NoClaim, kGravitationStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.gravitation.check-dimensions", ClaimType::Definition, kGravitationDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.gravitation.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions,
     1, FailureBehavior::WithholdResult},
    {"physics.gravitation.check-candidate", ClaimType::Implication, kGravitationCandidate, 1,
     FailureBehavior::WithholdResult},

    // physics.oscillation.restoring-force
    {"physics.oscillation.definition", ClaimType::NoClaim, kOscillationStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.oscillation.check-dimensions", ClaimType::Definition, kOscillationDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.oscillation.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions,
     1, FailureBehavior::WithholdResult},
    {"physics.oscillation.check-candidate", ClaimType::Implication, kOscillationCandidate, 1,
     FailureBehavior::WithholdResult},

    // physics.wave.speed-frequency-wavelength
    {"physics.wave.definition", ClaimType::NoClaim, kWaveStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.wave.check-dimensions", ClaimType::Definition, kWaveDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.wave.convert-units", ClaimType::SolutionSetPreserved, kScalePreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.wave.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.wave.check-candidate", ClaimType::Implication, kWaveCandidate, 1,
     FailureBehavior::WithholdResult},

    // physics.modern.photon-wavelength, physics.modern.photoelectric and
    // physics.modern.mass-energy. One plan rule each, and one shared set of steps after it.
    {"physics.modern.planck-relation", ClaimType::NoClaim, kModernStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.modern.einstein-photoelectric", ClaimType::NoClaim, kModernStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.modern.mass-energy-equivalence", ClaimType::NoClaim, kModernStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.modern.check-dimensions", ClaimType::Definition, kModernDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.modern.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.modern.check-candidate", ClaimType::Implication, kModernCandidate, 1,
     FailureBehavior::WithholdResult},
    {"physics.modern.significant-figures", ClaimType::NoClaim, kReportedWithinHalfPlace, 1,
     FailureBehavior::WithholdResult},

    {"physics.ranking.criterion", ClaimType::NoClaim, nullptr, 0,
     FailureBehavior::WithholdResult},
    {"physics.ranking.order", ClaimType::NoClaim, nullptr, 0, FailureBehavior::WithholdResult},

    // physics.relativity.time-dilation, physics.relativity.length-contraction,
    // physics.relativity.lorentz-transformation, physics.relativity.velocity-addition and
    // physics.relativity.energy-momentum. One shared plan rule and one shared set of steps after it.
    {"physics.relativity.plan", ClaimType::NoClaim, kRelativityStrategy, 3,
     FailureBehavior::WithholdResult},
    {"physics.relativity.check-frames", ClaimType::Definition, kRelativityFrames, 1,
     FailureBehavior::WithholdResult},
    {"physics.relativity.check-boost", ClaimType::Definition, kRelativitySpeedLimit, 1,
     FailureBehavior::WithholdResult},
    {"physics.relativity.lorentz-factor", ClaimType::Definition, kRelativityFactor, 1,
     FailureBehavior::WithholdResult},
    {"physics.relativity.apply", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.relativity.check-invariant", ClaimType::Implication, kRelativityInvariant, 1,
     FailureBehavior::WithholdResult},
    {"physics.relativity.significant-figures", ClaimType::NoClaim, kReportedWithinHalfPlace, 1,
     FailureBehavior::WithholdResult},

    // physics.relative-motion
    {"physics.relative-motion.plan", ClaimType::NoClaim, kRelativeMotionStrategy, 6,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.check-frame-declared", ClaimType::Definition, kRelativeFramesDeclared,
     1, FailureBehavior::WithholdResult},
    {"physics.relative-motion.check-frame-match", ClaimType::Definition, kRelativeFramesMatch, 1,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.check-input-dimensions", ClaimType::Definition,
     kRelativeVelocityDimensions, 1, FailureBehavior::WithholdResult},
    {"physics.relative-motion.check-rank", ClaimType::Definition, kRelativeRankTwo, 1,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.check-result-dimension", ClaimType::Definition,
     kRelativeResultDimension, 1, FailureBehavior::WithholdResult},
    {"physics.relative-motion.component-i", ClaimType::EquivalentExpression, kRelativeComponentI, 1,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.component-j", ClaimType::EquivalentExpression, kRelativeComponentJ, 1,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.convert-si", ClaimType::EquivalentExpression, kConvertsByTable, 1,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.definition", ClaimType::Definition, kRelativeDefinitionAfterChecks, 1,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.substitute", ClaimType::SolutionSetPreserved,
     kLookupPreservesSolutions, 1, FailureBehavior::WithholdResult},
    {"physics.relative-motion.interpret-direction", ClaimType::Definition, kRelativeDirection, 1,
     FailureBehavior::WithholdResult},
    {"physics.relative-motion.bearing-convention", ClaimType::Definition,
     kRelativeBearingConvention, 1, FailureBehavior::WithholdResult},
    {"physics.relative-motion.subscript-cancellation", ClaimType::Definition,
     kRelativeSubscriptCancellation, 1, FailureBehavior::WithholdResult},
    {"physics.relative-motion.isolate-unknown", ClaimType::EquivalentExpression,
     kRelativeIsolation, 1, FailureBehavior::CannotFail},
    {"physics.relative-motion.significant-figures", ClaimType::NoClaim, kRelativeRounding, 1,
     FailureBehavior::WithholdResult},

    // physics.optics
    {"physics.optics.refraction.snell", ClaimType::NoClaim, kOpticsStrategy, 4,
     FailureBehavior::WithholdResult},
    {"physics.optics.thin-lens.image", ClaimType::NoClaim, kOpticsStrategy, 4,
     FailureBehavior::WithholdResult},
    {"physics.optics.spherical-mirror.image", ClaimType::NoClaim, kOpticsStrategy, 4,
     FailureBehavior::WithholdResult},
    {"physics.optics.double-slit.maxima", ClaimType::NoClaim, kOpticsStrategy, 4,
     FailureBehavior::WithholdResult},
    {"physics.optics.single-slit.minima", ClaimType::NoClaim, kOpticsStrategy, 4,
     FailureBehavior::WithholdResult},
    {"physics.optics.check-dimensions", ClaimType::Definition, kOpticsDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.optics.check-domain", ClaimType::Definition, kOpticsDomain, 1,
     FailureBehavior::WithholdResult},
    {"physics.optics.sign-convention", ClaimType::Definition, kOpticsConvention, 1,
     FailureBehavior::CannotFail},
    {"physics.optics.convert-units", ClaimType::SolutionSetPreserved, kScalePreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.optics.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"physics.optics.total-internal-reflection", ClaimType::Definition, kOpticsCriticalAngle, 1,
     FailureBehavior::WithholdResult},
    {"physics.optics.check-candidate", ClaimType::Implication, kOpticsCandidate, 1,
     FailureBehavior::WithholdResult},
    {"physics.optics.significant-figures", ClaimType::NoClaim, kReportedWithinHalfPlace, 1,
     FailureBehavior::WithholdResult},

    // physics.planar-kinematics
    {"physics.planar-kinematics.plan", ClaimType::NoClaim, kPlanarStrategy, 7,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.projectile-plan", ClaimType::NoClaim, kProjectilePlanStrategy, 8,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.apex-plan", ClaimType::NoClaim, kPlanarApexStrategy, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-rank", ClaimType::Definition, kPlanarRankTwo, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-frame-declared", ClaimType::Definition, kPlanarFramesDeclared,
     1, FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-frame-match", ClaimType::Definition, kPlanarFramesMatch, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-stages", ClaimType::Definition, kPlanarStageIdentity, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-input-dimensions", ClaimType::Definition,
     kPlanarInputDimensions, 1, FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-projectile", ClaimType::Definition, kPlanarProjectile, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-result-dimension", ClaimType::Definition,
     kPlanarResultDimensions, 1, FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-shared-time", ClaimType::Definition, kPlanarSharedTime, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.check-apex-routes", ClaimType::Definition, kPlanarApexRoutes, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.definition", ClaimType::Definition, kPlanarDefinitionAfterChecks, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.substitute", ClaimType::SolutionSetPreserved,
     kLookupPreservesSolutions, 1, FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.convert-si", ClaimType::EquivalentExpression, kConvertsByTable, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.component-i", ClaimType::EquivalentExpression, kPlanarComponentI, 1,
     FailureBehavior::WithholdResult},
    {"physics.planar-kinematics.component-j", ClaimType::EquivalentExpression, kPlanarComponentJ, 1,
     FailureBehavior::WithholdResult},
    // The rounding step raises nothing: it reports a value the steps above already verified, and
    // its own comparison against the unrounded value is what the record carries instead.
    {"physics.planar-kinematics.significant-figures", ClaimType::NoClaim, nullptr, 0,
     FailureBehavior::WithholdResult},

    // physics.work.constant-force
    {"physics.work.plan", ClaimType::NoClaim, kWorkStrategy, 6, FailureBehavior::WithholdResult},
    {"physics.work.check-applicability", ClaimType::Definition, kWorkConstantForce, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.check-candidate", ClaimType::Definition, kWorkCandidate, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.check-frame-declared", ClaimType::Definition, kWorkFramesDeclared, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.check-frame-match", ClaimType::Definition, kWorkFramesMatch, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.check-input-dimensions", ClaimType::Definition, kWorkInputDimensions, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.check-rank", ClaimType::Definition, kWorkRanksMatch, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.check-result-dimension", ClaimType::Definition, kWorkResultDimension, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.constant-force-definition", ClaimType::Definition, kWorkLawAfterChecks, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.convert-si", ClaimType::EquivalentExpression, kConvertsByTable, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.evaluate-dot", ClaimType::EquivalentExpression, kDotIsTheDefinition, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.interpret-sign", ClaimType::Definition, kWorkSign, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.giac-cross-check", ClaimType::Definition, kWorkBackendAgreement, 1,
     FailureBehavior::WithholdResult},
    {"physics.work.significant-figures", ClaimType::NoClaim, kWorkRounding, 1,
     FailureBehavior::WithholdResult},

    // units.conversion
    {"unit.convert.plan", ClaimType::NoClaim, kUnitConvertStrategy, 3,
     FailureBehavior::WithholdResult},
    {"unit.convert.check-dimension", ClaimType::Definition, kUnitDimensionsMatch, 1,
     FailureBehavior::WithholdResult},
    {"unit.convert.report-precision", ClaimType::Definition, kUnitRoundingFinal, 2,
     FailureBehavior::WithholdResult},
    {"unit.convert.si-to-target", ClaimType::EquivalentExpression, kUnitTargetFactor, 1,
     FailureBehavior::WithholdResult},
    {"unit.convert.source-to-si", ClaimType::EquivalentExpression, kUnitSourceFactor, 1,
     FailureBehavior::WithholdResult},

    // vectors.addition
    {"vec.add.plan", ClaimType::NoClaim, kVectorAddStrategy, 4, FailureBehavior::WithholdResult},
    {"vec.add.check-dimension", ClaimType::Definition, kVectorAddDimensions, 1,
     FailureBehavior::WithholdResult},
    {"vec.add.check-frame", ClaimType::Definition, kVectorAddFrames, 1,
     FailureBehavior::WithholdResult},
    {"vec.add.check-rank", ClaimType::Definition, kVectorAddRank, 1,
     FailureBehavior::WithholdResult},
    {"vec.add.component-i", ClaimType::EquivalentExpression, kComponentSum, 1,
     FailureBehavior::WithholdResult},
    {"vec.add.component-j", ClaimType::EquivalentExpression, kComponentSum, 1,
     FailureBehavior::WithholdResult},
    {"vec.add.component-k", ClaimType::EquivalentExpression, kComponentSum, 1,
     FailureBehavior::WithholdResult},
    {"vec.add.convert-si", ClaimType::EquivalentExpression, kConvertsByTable, 1,
     FailureBehavior::WithholdResult},
    {"vec.add.report-precision", ClaimType::Definition, kVectorAddRounding, 2,
     FailureBehavior::WithholdResult},

    // vectors.cross-product
    {"vec.cross.plan", ClaimType::NoClaim, kVectorCrossStrategy, 4,
     FailureBehavior::WithholdResult},
    {"vec.cross.check-rank", ClaimType::Definition, kVectorCrossRank, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.check-frame", ClaimType::Definition, kVectorCrossFrames, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.check-dimension", ClaimType::Definition, kVectorCrossDimensions, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.convert-si", ClaimType::EquivalentExpression, kConvertsByTable, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.component-i", ClaimType::EquivalentExpression, kComponentCross, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.component-j", ClaimType::EquivalentExpression, kComponentCross, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.component-k", ClaimType::EquivalentExpression, kComponentCross, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.check-orthogonal-first", ClaimType::Definition, kVectorCrossOrthogonalFirst, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.check-orthogonal-second", ClaimType::Definition, kVectorCrossOrthogonalSecond, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.check-anticommutative", ClaimType::Definition, kVectorCrossAnticommutative, 1,
     FailureBehavior::WithholdResult},
    {"vec.cross.report-precision", ClaimType::Definition, kVectorCrossRounding, 2,
     FailureBehavior::WithholdResult},

    // vectors.scalar-product
    {"vec.dot.plan", ClaimType::NoClaim, kScalarProductStrategy, 4,
     FailureBehavior::WithholdResult},
    {"vec.dot.angle-plan", ClaimType::NoClaim, kScalarAngleStrategy, 5,
     FailureBehavior::WithholdResult},
    {"vec.dot.check-rank", ClaimType::Definition, kScalarProductRanks, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.check-frame", ClaimType::Definition, kScalarProductFrames, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.check-dimension", ClaimType::Definition, kScalarProductDimensions, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.check-nonzero", ClaimType::Definition, kScalarProductNonzero, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.definition", ClaimType::Definition, kScalarProductDefinition, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.substitute", ClaimType::SolutionSetPreserved, kLookupPreservesSolutions, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.convert-si", ClaimType::EquivalentExpression, kConvertsByTable, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.component-sum", ClaimType::EquivalentExpression, kScalarProductSum, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.check-commutative", ClaimType::Definition, kScalarProductCommutative, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.check-magnitude-bound", ClaimType::Definition, kScalarProductBound, 1,
     FailureBehavior::WithholdResult},
    {"vec.dot.interpret-angle", ClaimType::Definition, kScalarProductAngle, 1,
     FailureBehavior::WithholdResult},
    // The reporting step raises the half-place obligation and nothing else: the value it reports is
    // one the steps above already verified.
    {"vec.dot.report-precision", ClaimType::NoClaim, kReportedWithinHalfPlace, 1,
     FailureBehavior::WithholdResult},

    // vectors.components and vectors.polar
    {"vec.components.plan", ClaimType::NoClaim, kVectorComponentsStrategy, 5,
     FailureBehavior::WithholdResult},
    {"vec.components.check-angle-unit", ClaimType::Definition, kAngleUnitExplicit, 1,
     FailureBehavior::WithholdResult},
    {"vec.components.check-dimension", ClaimType::Definition, kComponentsDimensions, 1,
     FailureBehavior::WithholdResult},
    {"vec.components.check-frame", ClaimType::Definition, kComponentsFrame, 1,
     FailureBehavior::WithholdResult},
    {"vec.components.report-precision", ClaimType::Definition, kComponentsPrecision, 1,
     FailureBehavior::WithholdResult},
    {"vec.components.x", ClaimType::EquivalentExpression, kComponentRelations, 1,
     FailureBehavior::WithholdResult},
    {"vec.components.y", ClaimType::EquivalentExpression, kComponentRelations, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.plan", ClaimType::NoClaim, kVectorComponentsStrategy, 5,
     FailureBehavior::WithholdResult},
    {"vec.polar.check-angle-unit", ClaimType::Definition, kAngleUnitExplicit, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.check-dimension", ClaimType::Definition, kComponentsDimensions, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.check-frame", ClaimType::Definition, kComponentsFrame, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.check-rank", ClaimType::Definition, kComponentsRankTwo, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.check-rank-three", ClaimType::Definition, kComponentsRankThree, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.check-convention", ClaimType::Definition, kSphericalConvention, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.plan-three", ClaimType::NoClaim, kVectorSphericalStrategy, 6,
     FailureBehavior::WithholdResult},
    {"vec.polar.polar-angle", ClaimType::EquivalentExpression, kPolarAngleRelation, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.direction", ClaimType::EquivalentExpression, kQuadrantDirection, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.magnitude", ClaimType::EquivalentExpression, kMagnitudeRelation, 1,
     FailureBehavior::WithholdResult},
    {"vec.polar.report-precision", ClaimType::Definition, kComponentsPrecision, 1,
     FailureBehavior::WithholdResult},

    {"plan.integer-method", ClaimType::NoClaim, kIntegerStrategy, 2,
     FailureBehavior::WithholdResult},
    {"int.division", ClaimType::EquivalentExpression, kIntegerDivision, 1,
     FailureBehavior::WithholdResult},
    {"int.factorial-product", ClaimType::EquivalentExpression, kIntegerFactorial, 1,
     FailureBehavior::WithholdResult},
    {"int.permutation-product", ClaimType::EquivalentExpression, kIntegerPermutation, 1,
     FailureBehavior::WithholdResult},
    {"int.combination-product", ClaimType::EquivalentExpression, kIntegerCombination, 1,
     FailureBehavior::WithholdResult},
    {"int.trial-division", ClaimType::Definition, kIntegerTrial, 1,
     FailureBehavior::WithholdResult},
    {"int.prime-conclusion", ClaimType::EquivalentExpression, kIntegerPrime, 1,
     FailureBehavior::WithholdResult},
    {"int.next-prime", ClaimType::EquivalentExpression, kIntegerNextPrime, 1,
     FailureBehavior::WithholdResult},
    {"int.modular-power", ClaimType::EquivalentExpression, kIntegerModularPower, 1,
     FailureBehavior::WithholdResult},
    {"int.factor-product", ClaimType::EquivalentExpression, kIntegerFactors, 1,
     FailureBehavior::WithholdResult},
    {"int.gcd-sign", ClaimType::EquivalentExpression, kIntegerGcdSign, 1,
     FailureBehavior::WithholdResult},
    {"int.gcd-remainder", ClaimType::EquivalentExpression, kIntegerGcdRemainder, 1,
     FailureBehavior::WithholdResult},
    {"int.gcd-conclusion", ClaimType::EquivalentExpression, kIntegerGcdCertificate, 1,
     FailureBehavior::WithholdResult},
    {"matrix.row-swap", ClaimType::RowEquivalent, kMatrixRow, 1,
     FailureBehavior::WithholdResult},
    {"matrix.row-scale", ClaimType::RowEquivalent, kMatrixRow, 1,
     FailureBehavior::WithholdResult},
    {"matrix.row-add-multiple", ClaimType::RowEquivalent, kMatrixRow, 1,
     FailureBehavior::WithholdResult},
    {"plan.matrix-method", ClaimType::NoClaim, kMatrixStrategy, 2,
     FailureBehavior::WithholdResult},
    {"matrix.ref-conclusion", ClaimType::RowEquivalent, kMatrixRefConclusion, 2,
     FailureBehavior::WithholdResult},
    {"matrix.rref-conclusion", ClaimType::RowEquivalent, kMatrixRrefConclusion, 2,
     FailureBehavior::WithholdResult},
    {"plan.matrix-determinant", ClaimType::NoClaim, kMatrixDeterminantStrategy, 2,
     FailureBehavior::WithholdResult},
    {"matrix.det-row-swap", ClaimType::EquivalentExpression, kMatrixDeterminantRow, 2,
     FailureBehavior::WithholdResult},
    {"matrix.det-row-scale", ClaimType::EquivalentExpression, kMatrixDeterminantRow, 2,
     FailureBehavior::WithholdResult},
    {"matrix.det-row-add-multiple", ClaimType::EquivalentExpression, kMatrixDeterminantRow, 2,
     FailureBehavior::WithholdResult},
    {"matrix.det-diagonal-product", ClaimType::EquivalentExpression, kMatrixDeterminantProduct, 3,
     FailureBehavior::WithholdResult},
    {"matrix.det-correction", ClaimType::EquivalentExpression, kMatrixDeterminantResult, 1,
     FailureBehavior::WithholdResult},
};

}  // namespace

const RuleSchema *rule_schema(const std::string &rule_id) {
    for (size_t i = 0; i < sizeof(kRules) / sizeof(kRules[0]); ++i) {
        if (rule_id == kRules[i].rule_id)
            return &kRules[i];
    }
    return nullptr;
}

size_t declared_rule_count() { return sizeof(kRules) / sizeof(kRules[0]); }

const RuleSchema &declared_rule(size_t index) { return kRules[index]; }

}  // namespace nps
