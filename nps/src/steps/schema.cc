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
    {"recompute each stored value as the given times its table scale",
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

// modern
const EvidenceAlternative kModernRelationModel[] = {
    {"registered relation model", EvidenceStrength::StructurallyValid},
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
const EvidenceAlternative kHalfPlaceComparison[] = {
    {"exact half-place comparison", EvidenceStrength::CandidateChecked},
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

// vector components and polar form
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

    // physics.kinematics.catch-up.equal-position
    {"physics.catch-up.constant-velocity", ClaimType::NoClaim, kCatchUpStrategy, 4,
     FailureBehavior::WithholdResult},
    {"physics.catch-up.equal-position", ClaimType::Definition, kEqualPositionIsTheEvent, 1,
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
    {"physics.relative-motion.interpret-direction", ClaimType::Definition, kRelativeDirection, 1,
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
