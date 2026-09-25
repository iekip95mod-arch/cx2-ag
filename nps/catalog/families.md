# Coverage catalog

PRD section 27 wants the coverage catalog to be a versioned product artifact rather than an informal
checklist, and section 19.9 wants public coverage claims generated from it rather than from topic
labels somebody wrote. This is that catalog. tools/coverage.cc checks it against the golden fixtures
and refuses a claim with nothing behind it.

One block per family. A line is a field name and its value. A `rule` line names a rule the family
emits and where the evidence for it comes from: `fixture` means a golden fixture records it, and
`device` means only a run on the calculator reaches it, which the host corpus cannot.

`test_group_ids` names the groups in tests/unit/run_tests.cc that exercise the family. It is how the
traceability report links a requirement to the families it applies to, and a group named here that
no test run reports fails that report.

`proof_obligation_ids` names the obligations the family's steps raise, which section 19.9 wants
reported on its own rather than folded into rule or corpus coverage. Same rule as the rules: one the
fixtures raise and the catalog does not name is a failure, and so is one named here that no fixture
raises.

The tool lists every missing or unanswered section 27 field for each family. Its separate schema
inventory counts field names present anywhere in the catalog, which cannot establish completeness
for any family. Missing release metadata remains visible rather than being filled with placeholders.

An explicit unqualified or unreleased status answers a metadata field without establishing acceptance.
Metadata gaps are reported without changing the existing rule, envelope and evidence checks. A passing
coverage run does not establish that the release meets section 27.

A field whose answer is nothing opens with the word none and then says why. That is a different
statement from a line left out, which is the question nobody answered, and the eight fields section
5.5 calls the family envelope have to carry one or the other for every family. On
proof_obligation_ids the reader treats a leading none as the empty list rather than as an id, so a
family that raises no obligation says so and the join still fails the day a fixture raises one.

The required_assumptions field is prose and an assumption is a sentence, so the join is over the
engine strings the line puts in double quotes. Each quoted string has to be recorded by a fixture of that family,
and the line is free to say whatever else it needs around it. The channels that count are the ones
that carry an assumption, the solve-level summary and a step's assumptions before, not a domain
restriction, which is a condition one step met along the way rather than something the family
requires. A line that declares none has to have nothing carried against it, and a line declaring an
assumption has to quote at least one string, because a sentence nothing joins is present without
being true. What the join does not catch, and this is deliberate: a family already quoting one string
can start carrying a second one nobody wrote down. Completeness is not checkable here, because many
carried strings hold problem data, as in the frame name in vector frame is lab.

family id algebra.linear-equation.one-unknown
topic_and_level Single-variable linear equations and formula isolation, PRD section 22.1
accepted_expression_grammar an equation over sums, products, negations, division by a constant and integer powers with a constant base, where the unknown appears to the first power and any other symbol has to be supplied as a known
accepted_input_forms an equation in one unknown, written in the project's own grammar
domains_and_parameter_assumptions the coefficient of the unknown is checked to be non-zero before dividing
supported_branches_and_degenerate_cases no solution when the unknown cancels and the sides differ, every value when they agree
exact_special_function_and_numerical_result_policy exact rationals over int64, refusing rather than wrapping
parser_module_ids src/core/parser.cc, src/steps/linear.cc
required_assumptions none, the one condition the method needs is the coefficient of the unknown being non-zero, and that is checked before dividing rather than assumed
test_group_ids linear
proof_obligation_ids obl.linear.candidate-satisfies, obl.eq.same-solutions, obl.linear.coefficient-decides, obl.plan.preconditions-hold
supported_methods collect like terms to one side, then divide both sides by the coefficient
unsupported_near_neighbors quadratic and higher degree, systems in more than one unknown, inequalities, parametric coefficients
solution_soundness_status verified, every candidate is substituted into the collected equation
solution_completeness_status partial, degree one only
corpus_case_ids the golden fixtures naming this family
device_performance_status measured on the physical calculator, see STATUS.md
direct_keypad_entry_status entered from the calculator keypad, measured
isolated_runtime_status unqualified, the complete supported corpus must pass with USB physically disconnected
release_status unreleased
rule eq.linear.inverse-operations fixture
rule eq.linear.inspect-collected-coefficient fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture
rule eq.linear.check-by-substitution fixture

family id algebra.quadratic.pure-square.one-unknown
topic_and_level Degree-two equations in one unknown with no term of degree one, solved by isolating the square
family_envelope_version 1
accepted_expression_grammar the same equation grammar read as linear in the square, so sums, products, negations and constant powers, with the square of the unknown as the only power of it
accepted_input_forms an equation whose only power of the unknown is the square, written in the project's own grammar
domains_and_parameter_assumptions the real domain, and the coefficient of the square is checked to be non-zero before dividing
supported_branches_and_degenerate_cases two roots when the isolated square is positive, one repeated root when it is zero, and no real solution when it is negative
exact_special_function_and_numerical_result_policy exact rationals only, refusing a square whose root is irrational rather than reporting a decimal
parser_module_ids src/core/parser.cc, src/steps/quadratic.cc, src/steps/linear.cc
required_assumptions none, the coefficient of the square is checked before dividing and the sign of the isolated square decides the case rather than being assumed
test_group_ids quadratic
proof_obligation_ids obl.eq.same-solutions, obl.quadratic.case-is-a-root, obl.quadratic.candidate-satisfies, obl.quadratic.rejected-case-is-infeasible, obl.quadratic.cases-are-complete, obl.plan.preconditions-hold
supported_methods read the equation as linear in the square, isolate it, then record one case per real root
unsupported_near_neighbors equations with a term of degree one, squares with no exact rational root, higher degree, complex roots
solution_soundness_status verified, every case is substituted into the equation as it was typed rather than into the isolated form
solution_completeness_status verified within the envelope, the recorded cases are multiplied back out and compared with the quadratic they were split from
corpus_case_ids the golden fixtures naming this family
device_performance_status not yet measured on the physical calculator
direct_keypad_entry_status not yet entered from the calculator keypad
isolated_runtime_status unqualified, host build results do not establish isolated calculator execution
release_status unreleased
rule eq.quadratic.square-root fixture
rule eq.quadratic.isolate-the-square fixture
rule eq.quadratic.square-root-case fixture
rule eq.quadratic.check-by-substitution fixture
rule eq.quadratic.reject-negative-square fixture
rule eq.quadratic.cases-reconstruct-the-original fixture

family id physics.kinematics.catch-up.equal-position
reference_curriculum_set_ids StepCAS product requirements PHYS-002, PHYS-014, PHYS-015, PHYS-025, PHYS-026, PHYS-027, PHYS-028, PHYS-029, M1 archetype 4
curriculum_source_locations StepCAS_Product_Requirements_Document.md sections 9.4 through 9.7, .Internal/agent-pack/tasks/M1_VERTICAL_SLICE.md archetype 4
topic_and_level Two-stage one-dimensional catch-up and equal-position events, M1 archetype 4
family_envelope_version 2
accepted_expression_grammar existing quantity grammar for each position, velocity, acceleration, and start-time field
domains_and_parameter_assumptions two distinct named bodies share one declared one-dimensional frame and an event is admissible only after both start times
supported_branches_and_degenerate_cases delayed starts, a meeting at the shared-domain boundary, an algebraic meeting before that boundary, no meeting, coincident laws, and zero acceleration reduced to constant velocity while retaining measured precision
exact_special_function_and_numerical_result_policy compatible units convert exactly to SI, the existing linear solver isolates event time exactly, and measured precision is applied only to final event reports
accepted_input_forms one typed CatchUpProblem containing two CatchUpBody records with identity, frame, motion model, and typed quantities
word_language_profile_ids none, typed entry only
parser_module_ids src/units/units.cc, src/physics/catch_up.cc, src/steps/linear.cc
required_assumptions velocity is constant on each active interval, or a constant-acceleration model carries zero acceleration, and the solve carries "motion is one-dimensional with positive position along the declared axis"
supported_methods build both active-interval position laws, equate positions, use the existing exact linear solver, enforce the shared domain, and substitute into both original laws
unsupported_near_neighbors nonzero acceleration, nonlinear motion in time, more than two bodies, two-dimensional pursuit, implicit frame conversion, and collision dynamics
strategy_ids physics.catch-up.constant-velocity
test_group_ids catch up
proof_obligation_ids obl.catch-up.position-law-dimensions, obl.physics.lookup-preserves-solutions, obl.linear.candidate-satisfies, obl.catch-up.candidate-in-domain, obl.catch-up.first-position, obl.catch-up.second-position, obl.eq.same-solutions, obl.catch-up.equal-position-is-the-event, obl.physics.reported-within-half-place, obl.plan.preconditions-hold
solution_soundness_status verified by typed dimensions, exact linear candidate substitution, exact shared-domain comparison, and substitution into both original position laws
solution_completeness_status complete for two constant-velocity active-interval laws within exact rational and resource limits, partial for broader catch-up motion
corpus_case_ids catch_up_delayed_start, catch_up_measured_report, catch_up_before_shared_domain, catch_up_nonlinear_refused
explanation_review_status solved and refusal derivations covered by golden fixtures, independent explanation review not yet recorded
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native typed Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status host core and native Lua bridge validated, ARM module compiles and packages, calculator runtime not yet measured
capability_manifest_ids physics.kinematics.catch-up.equal-position
release_status in development, unreleased
rule physics.catch-up.constant-velocity fixture
rule physics.catch-up.check-dimensions fixture
rule physics.catch-up.equal-position fixture
rule physics.catch-up.substitute fixture
rule eq.linear.inverse-operations fixture
rule eq.linear.check-by-substitution fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture
rule physics.catch-up.shared-domain fixture
rule physics.catch-up.verify-first-position fixture
rule physics.catch-up.verify-second-position fixture
rule physics.catch-up.significant-figures fixture

family id physics.kinematics.relative-motion.components.two-dimension
reference_curriculum_set_ids StepCAS product requirements PHYS-016, M1 archetype 7
curriculum_source_locations StepCAS_Product_Requirements_Document.md section 11, .Internal/agent-pack/tasks/M1_VERTICAL_SLICE.md archetype 7
topic_and_level Two-dimensional Cartesian relative velocity, M1 archetype 7
family_envelope_version 1
accepted_expression_grammar existing scalar expression grammar for each velocity component
accepted_input_forms one typed RelativeMotionProblem naming a subject, a reference, two framed rank-two velocity vectors and a coordinate convention
domains_and_parameter_assumptions both velocities are rank two, declare the same named frame, and carry dimension L T^-1
supported_branches_and_degenerate_cases exact and measured components, negative components, and compatible velocity units with different SI scales
exact_special_function_and_numerical_result_policy exact rational SI conversion and component subtraction, with measured precision applied only to the final report
word_language_profile_ids none, typed entry only
parser_module_ids src/units/units.cc, src/physics/relative_motion.cc
required_assumptions both velocities are expressed in the declared Cartesian frame, which for the compass frame is carried as "positive i is east and positive j is north"
test_group_ids relative motion
proof_obligation_ids obl.relative-motion.rank-two, obl.relative-motion.frames-declared, obl.relative-motion.frames-match, obl.relative-motion.velocity-dimensions, obl.relative-motion.definition-after-checks, obl.physics.lookup-preserves-solutions, obl.relative-motion.result-dimension, obl.relative-motion.component-i, obl.relative-motion.component-j, obl.relative-motion.direction-interpreted, obl.physics.converts-by-table, obl.plan.preconditions-hold
strategy_ids physics.relative-motion.plan
supported_methods validate ranks, frames and dimensions, convert both velocities exactly to SI, subtract matching components with Giac checking each one, then interpret the component signs in the declared axes
unsupported_near_neighbors implicit frame transformation, rank-three relative motion, mismatched-rank inputs, rotating frames, relativistic addition, and unequal dimensions
solution_soundness_status verified by rank, frame declaration, frame identity, input and result dimension, and a Giac comparison on each exact component
solution_completeness_status partial, two-dimensional Cartesian relative velocity in one shared frame
corpus_case_ids relative_motion_mixed_units
explanation_review_status core rule sequence covered by a golden fixture, independent explanation review not yet recorded
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native typed Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status ARM core compiles, calculator runtime not yet measured
capability_manifest_ids physics.kinematics.relative-motion.components.two-dimension
release_status in development, unreleased
rule physics.relative-motion.plan fixture
rule physics.relative-motion.check-rank fixture
rule physics.relative-motion.check-frame-declared fixture
rule physics.relative-motion.check-frame-match fixture
rule physics.relative-motion.check-input-dimensions fixture
rule physics.relative-motion.definition fixture
rule physics.relative-motion.substitute fixture
rule physics.relative-motion.convert-si fixture
rule physics.relative-motion.check-result-dimension fixture
rule physics.relative-motion.component-i fixture
rule physics.relative-motion.component-j fixture
rule physics.relative-motion.interpret-direction fixture
# src/physics/relative_motion.cc also emits physics.relative-motion.significant-figures. No fixture
# records it, since the only pinned case is exact, so it is not claimed here.

family id physics.kinematics.relative-motion.components.one-dimension
reference_curriculum_set_ids StepCAS product requirements PHYS-016, chapter 4 section 4-6
curriculum_source_locations StepCAS_Product_Requirements_Document.md section 11
topic_and_level One-dimensional relative velocity along a single declared axis, chapter 4 section 4-6
family_envelope_version 1
accepted_expression_grammar existing scalar expression grammar for each velocity component
accepted_input_forms one typed RelativeMotionProblem naming a subject, a reference, two framed rank-one velocity vectors and a coordinate convention
domains_and_parameter_assumptions both velocities are rank one, declare the same named frame whose positive sense is the single active axis, and carry dimension L T^-1
supported_branches_and_degenerate_cases exact and measured components, negative components, and compatible velocity units with different SI scales
exact_special_function_and_numerical_result_policy exact rational SI conversion and component subtraction, with measured precision applied only to the final report
word_language_profile_ids none, typed entry only
parser_module_ids src/units/units.cc, src/physics/relative_motion.cc
required_assumptions none, no golden fixture yet exercises this family so no engine string is joined against one
test_group_ids relative motion
proof_obligation_ids none, no golden fixture yet exercises this family so no obligation is joined against one
strategy_ids physics.relative-motion.plan
supported_methods validate ranks, frames and dimensions, convert both velocities exactly to SI, subtract matching components with Giac checking each one, then interpret the sign along the declared axis
unsupported_near_neighbors implicit frame transformation, two-dimension or three-dimension relative motion, mismatched-rank inputs, rotating frames, relativistic addition, and unequal dimensions
solution_soundness_status verified by rank, frame declaration, frame identity, input and result dimension, and a Giac comparison on each exact component
solution_completeness_status partial, one-dimensional relative velocity in one shared frame
corpus_case_ids not yet filed
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native typed Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status ARM core compiles, calculator runtime not yet measured
capability_manifest_ids physics.kinematics.relative-motion.components.two-dimension
release_status in development, unreleased
rule physics.relative-motion.plan device
rule physics.relative-motion.check-rank device
rule physics.relative-motion.check-frame-declared device
rule physics.relative-motion.check-frame-match device
rule physics.relative-motion.check-input-dimensions device
rule physics.relative-motion.definition device
rule physics.relative-motion.substitute device
rule physics.relative-motion.convert-si device
rule physics.relative-motion.check-result-dimension device
rule physics.relative-motion.component-i device
rule physics.relative-motion.component-j device
rule physics.relative-motion.interpret-direction device
# The one-dimension family shares every rule id with the two-dimension family above, because
# src/physics/relative_motion.cc is one engine widened to accept rank one, not a second engine.

family id units.chain-link-conversion
topic_and_level Exact multiplicative unit conversion for scalar quantities, M1 chain-link conversion
accepted_expression_grammar a source quantity written as a number and a unit spelling from the table, as in 5 m/s or 2.5 km/h or a bare number for a pure number, and a target unit in that same spelling, rather than a free expression
accepted_input_forms a typed source Quantity and target Unit, with a wrapper for one supported quantity spelling and one supported target-unit spelling
domains_and_parameter_assumptions source and target dimensions must match and every unit scale must fit exact rational arithmetic
supported_branches_and_degenerate_cases exact and measured scalars, compound units, and SI prefix chains raised to area or volume powers
exact_special_function_and_numerical_result_policy exact rational factors and intermediates, with measured precision applied only to the final report
parser_module_ids src/units/units.cc, src/physics/unit_conversion.cc
required_assumptions none, the source and target units are required to share a dimension and that is checked rather than assumed
test_group_ids unit conversion, units
proof_obligation_ids obl.unit-conversion.dimensions-match, obl.unit-conversion.source-factor-exact, obl.unit-conversion.target-factor-exact, obl.unit-conversion.rounding-final, obl.unit-conversion.rounding-within-half-place, obl.plan.preconditions-hold
supported_methods source unit to SI followed by SI to target using the registered exact scales
unsupported_near_neighbors affine conversions, incompatible dimensions, unsupported unit spellings, and uncertainty propagation
solution_soundness_status verified by a dimension check, both exact unit-table factors, and the final precision check
solution_completeness_status complete for compatible multiplicative units in the current unit table while every exact rational operation fits
corpus_case_ids unit_conversion_powered_chain
device_performance_status not measured
direct_keypad_entry_status native Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status ARM module compiles and packages, calculator runtime not yet measured
release_status in development, unreleased
rule unit.convert.plan fixture
rule unit.convert.check-dimension fixture
rule unit.convert.source-to-si fixture
rule unit.convert.si-to-target fixture
rule unit.convert.report-precision fixture

family id physics.density.mass-volume
topic_and_level Mass, volume, and density through the definition m = rho*V, M1 density
accepted_expression_grammar existing quantity grammar for each mass, volume and density field, rather than a free expression
accepted_input_forms one typed unknown and exactly two distinct typed known quantities parsed with the existing quantity parser
domains_and_parameter_assumptions the three quantities must have mass, volume, and mass-per-volume dimensions and the requested unknown must be uniquely determined
supported_branches_and_degenerate_cases any one of mass, volume, or density may be unknown, with exact prefix conversion and explicit refusal of a zero divisor that does not determine one value
exact_special_function_and_numerical_result_policy exact rational SI conversion and linear isolation, with measured precision applied only after candidate verification
parser_module_ids src/units/units.cc, src/physics/density.cc, src/steps/linear.cc
required_assumptions the supplied scalars describe one uniform-density relation, carried as "density is uniform across the sample"
test_group_ids density, units
proof_obligation_ids obl.density.dimensions-agree, obl.density.candidate-satisfies, obl.linear.candidate-satisfies, obl.eq.same-solutions, obl.physics.scale-preserves-solutions, obl.physics.lookup-preserves-solutions, obl.physics.reported-within-half-place, obl.plan.preconditions-hold
supported_methods exact SI substitution into m = rho*V followed by the existing exact linear solver
unsupported_near_neighbors buoyancy, mixtures, spatially varying density, geometric volume derivation, and more than one unknown
solution_soundness_status verified by dimensional analysis and exact substitution into both the density definition and collected linear equation
solution_completeness_status complete for one unknown and two compatible knowns when the unique exact rational answer fits
corpus_case_ids density_volume_mixed_units, density_mass_cubic_prefix
device_performance_status not measured
direct_keypad_entry_status native Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status ARM module compiles and packages, calculator runtime not yet measured
release_status in development, unreleased
rule physics.density.definition fixture
rule physics.density.check-dimensions fixture
rule physics.density.convert-units fixture
rule physics.density.substitute fixture
rule physics.density.check-candidate fixture
rule physics.density.significant-figures fixture
rule eq.linear.inverse-operations fixture
rule eq.linear.check-by-substitution fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture

family id physics.modern.photon-wavelength
topic_and_level Photon energy and wavelength through E*lambda = hc, and the de Broglie wavelength of a momentum written as pc, PRD section 9 PHYS-024
accepted_expression_grammar existing quantity grammar for the numeric field, with the unit supplied by the variable rather than typed
accepted_input_forms one typed unknown and one distinct typed known, each in the unit its variable declares
domains_and_parameter_assumptions photon energy in eV and wavelength in nm, both strictly positive, with hc taken as the tabulated 1239.8 eV nm
supported_branches_and_degenerate_cases either the energy or the wavelength may be unknown, with an explicit refusal of a non-positive given and of a given handed over in another unit of the same dimension
exact_special_function_and_numerical_result_policy exact rational substitution in the declared eV and nm units with no SI conversion, since the SI values of these quantities do not fit the exact integer rationals every step here checks with, and measured precision applied only after candidate verification
parser_module_ids src/units/units.cc, src/physics/modern.cc, src/steps/linear.cc
required_assumptions the quantum travels in free space, carried as "the quantum travels in free space, where hc is the tabulated 1239.8 eV nm", together with the recorded working-unit assumption
test_group_ids modern, units
proof_obligation_ids obl.modern.dimensions-agree, obl.modern.candidate-satisfies, obl.linear.candidate-satisfies, obl.eq.same-solutions, obl.physics.lookup-preserves-solutions, obl.physics.reported-within-half-place, obl.plan.preconditions-hold
supported_methods exact substitution into E*lambda = hc followed by the existing exact linear solver
unsupported_near_neighbors the Bohr model and hydrogen transition energies, radioactive decay and half-life, Compton scattering, the uncertainty principle, blackbody and Wien displacement, and any input written in joules or metres
solution_soundness_status verified by dimensional analysis and exact substitution into both the Planck relation and the collected linear equation
solution_completeness_status complete for one unknown and one compatible known when the unique exact rational answer fits
corpus_case_ids not yet filed
device_performance_status not measured
direct_keypad_entry_status native Lua bridge not yet implemented
isolated_runtime_status not measured
release_status in development, unreleased
rule physics.modern.planck-relation fixture
rule physics.modern.check-dimensions fixture
rule physics.modern.substitute fixture
rule physics.modern.check-candidate fixture
rule physics.modern.significant-figures fixture
rule eq.linear.inverse-operations fixture
rule eq.linear.check-by-substitution fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture

family id physics.modern.photoelectric
topic_and_level The Einstein photoelectric equation Kmax = E - phi above the threshold, PRD section 9 PHYS-024
accepted_expression_grammar existing quantity grammar for each numeric field, with the unit supplied by the variable rather than typed
accepted_input_forms one typed unknown and exactly two distinct typed knowns, each in electronvolts
domains_and_parameter_assumptions photon energy, work function and maximum kinetic energy all in eV and all strictly positive
supported_branches_and_degenerate_cases any one of the three may be unknown, with an explicit refusal when the photon energy is at or below the work function, since that surface emits nothing rather than emitting a negative kinetic energy
exact_special_function_and_numerical_result_policy exact rational substitution in electronvolts with no SI conversion, and measured precision applied only after candidate verification
parser_module_ids src/units/units.cc, src/physics/modern.cc, src/steps/linear.cc
required_assumptions the surface is clean and each photon ejects at most one electron, carried as "one photon ejects one electron from a clean surface with no collision losses", together with the recorded working-unit assumption
test_group_ids modern, units
proof_obligation_ids obl.modern.dimensions-agree, obl.modern.candidate-satisfies, obl.linear.candidate-satisfies, obl.eq.same-solutions, obl.physics.lookup-preserves-solutions, obl.physics.reported-within-half-place, obl.plan.preconditions-hold
supported_methods exact substitution into Kmax = E - phi followed by the existing exact linear solver
unsupported_near_neighbors the stopping potential and its retarding voltage, the threshold frequency read from a slope, photocurrent and intensity, and the work function read off a plotted line
solution_soundness_status verified by dimensional analysis and exact substitution into both the photoelectric equation and the collected linear equation
solution_completeness_status complete for one unknown and two compatible knowns above the threshold when the unique exact rational answer fits
corpus_case_ids not yet filed
device_performance_status not measured
direct_keypad_entry_status native Lua bridge not yet implemented
isolated_runtime_status not measured
release_status in development, unreleased
rule physics.modern.einstein-photoelectric fixture
rule physics.modern.check-dimensions fixture
rule physics.modern.substitute fixture
rule physics.modern.check-candidate fixture
rule physics.modern.significant-figures fixture
rule eq.linear.inverse-operations fixture
rule eq.linear.check-by-substitution fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture

family id physics.modern.mass-energy
topic_and_level Mass-energy equivalence in binding-energy problems through E = dm*c^2, PRD section 9 PHYS-024
accepted_expression_grammar existing quantity grammar for the numeric field, with the unit supplied by the variable rather than typed
accepted_input_forms one typed unknown and one distinct typed known, the mass defect in u and the rest energy in MeV
domains_and_parameter_assumptions both quantities strictly positive, with c^2 taken as the tabulated 931.49 MeV per atomic mass unit
supported_branches_and_degenerate_cases either the mass defect or the released energy may be unknown, with an explicit refusal of a non-positive given
exact_special_function_and_numerical_result_policy exact rational substitution in the declared u and MeV units with no SI conversion, and measured precision applied only after candidate verification
parser_module_ids src/units/units.cc, src/physics/modern.cc, src/steps/linear.cc
required_assumptions the mass defect is the whole energy release, carried as "the mass defect is the whole energy release, with 931.49 MeV per atomic mass unit", together with the recorded working-unit assumption
test_group_ids modern, units
proof_obligation_ids obl.modern.dimensions-agree, obl.modern.candidate-satisfies, obl.linear.candidate-satisfies, obl.eq.same-solutions, obl.physics.lookup-preserves-solutions, obl.physics.reported-within-half-place, obl.plan.preconditions-hold
supported_methods exact substitution into E = dm*c^2 followed by the existing exact linear solver
unsupported_near_neighbors the mass defect assembled from a nuclide table, binding energy per nucleon, Q values of a written reaction, relativistic kinetic energy, and the relativistic energy-momentum relation which PHYS-023 owns
solution_soundness_status verified by dimensional analysis and exact substitution into both the mass-energy relation and the collected linear equation
solution_completeness_status complete for one unknown and one compatible known when the unique exact rational answer fits
corpus_case_ids not yet filed
device_performance_status not measured
direct_keypad_entry_status native Lua bridge not yet implemented
isolated_runtime_status not measured
release_status in development, unreleased
rule physics.modern.mass-energy-equivalence fixture
rule physics.modern.check-dimensions fixture
rule physics.modern.substitute fixture
rule physics.modern.check-candidate fixture
rule physics.modern.significant-figures fixture
rule eq.linear.inverse-operations fixture
rule eq.linear.check-by-substitution fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture

family id physics.vectors.cartesian-addition.two-dimension
topic_and_level Two- and three-dimensional Cartesian vector addition, PRD section 11 and M1 archetype 5. The family id keeps its original spelling so existing manifests, menus and fixtures stay resolvable
accepted_expression_grammar existing vector grammar for each addend, either unit-vector form such as 3 i + 4 j m/s or 1 i + 2 j + 3 k m, or an ordered tuple such as (3, 4) m/s
accepted_input_forms structured Vector values read from Cartesian unit-vector or ordered-tuple form
domains_and_parameter_assumptions both vectors have the same rank, two or three, with matching named frames and matching dimensions
supported_branches_and_degenerate_cases exact zero and negative components, compatible units with different SI scales
exact_special_function_and_numerical_result_policy exact rational SI conversion and addition, rounded only for final reporting
parser_module_ids src/units/units.cc, src/physics/vector_addition.cc
required_assumptions each vector is expressed in the named Cartesian frame, carried per vector with the name in it, as in "first vector frame is lab"
test_group_ids vector addition, units
proof_obligation_ids obl.vector-add.ranks-match, obl.vector-add.frames-match, obl.vector-add.dimensions-match, obl.vector-add.rounding-final, obl.vector-add.rounding-within-half-place, obl.physics.converts-by-table, obl.vector-add.component-sum, obl.plan.preconditions-hold
supported_methods exact SI conversion followed by matching component addition on every declared axis
unsupported_near_neighbors addition of vectors whose ranks differ, rank above three, implicit frame transformation, unequal dimensions
solution_soundness_status verified by rank, frame, dimension, exact conversion and exact rational addition records
solution_completeness_status complete for two- and three-dimensional Cartesian addition within exact int64 rational bounds
corpus_case_ids vector_addition_mixed_units
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status ARM artifact compiles, runtime not yet measured
release_status unreleased
rule vec.add.plan fixture
rule vec.add.check-rank fixture
rule vec.add.check-frame fixture
rule vec.add.check-dimension fixture
rule vec.add.convert-si fixture
rule vec.add.component-i fixture
rule vec.add.component-j fixture
rule vec.add.component-k fixture
rule vec.add.report-precision fixture

family id physics.vectors.cartesian-cross-product.three-dimension
topic_and_level Three-dimensional Cartesian vector cross product, PRD section 9 ALG-011 and chapter 3 of the PHYS 2410 scope
accepted_expression_grammar existing vector grammar for each operand, either unit-vector form such as 1 i + 2 j + 3 k m or an ordered tuple such as (1, 2, 3) m
accepted_input_forms structured Vector values read from Cartesian unit-vector or ordered-tuple form, rank three only
domains_and_parameter_assumptions both vectors have rank three, matching named frames, dimensions that may differ and whose product fits the dimension table
supported_branches_and_degenerate_cases exact zero and negative components, compatible dimensions that differ between the two operands, compatible units with different SI scales
exact_special_function_and_numerical_result_policy exact rational SI conversion and determinant expansion, rounded only for final reporting
parser_module_ids src/units/units.cc, src/physics/vector_cross.cc
required_assumptions each vector is expressed in the named Cartesian frame, carried per vector with the name in it, as in "first vector frame is lab"
test_group_ids vector cross product, units
proof_obligation_ids obl.vector-cross.rank-three, obl.vector-cross.frames-match, obl.vector-cross.dimension-product, obl.vector-cross.rounding-final, obl.vector-cross.rounding-within-half-place, obl.physics.converts-by-table, obl.vector-cross.component-cross, obl.vector-cross.orthogonal-first, obl.vector-cross.orthogonal-second, obl.vector-cross.anticommutative, obl.plan.preconditions-hold
supported_methods exact SI conversion followed by determinant expansion into three components, with exact orthogonality and anticommutativity checks against the inputs
unsupported_near_neighbors cross product of vectors whose rank is not three, implicit frame transformation, direct keypad entry
solution_soundness_status verified by rank, frame, dimension-product, exact conversion, exact rational determinant expansion, exact orthogonality and exact anticommutativity records
solution_completeness_status complete for three-dimensional Cartesian cross products within exact int64 rational bounds; the typed Lua bridge is nps.vector_cross, source with src/platform/nspire/lua_module.cc and test/target/luax_host.lua, reachable from the Ki V4 guided physics browser as of #443
corpus_case_ids vector_cross_torque_mixed_units
device_performance_status not measured
direct_keypad_entry_status not implemented, typed Lua bridge only
isolated_runtime_status not yet built for the device target
release_status unreleased
rule vec.cross.plan fixture
rule vec.cross.check-rank fixture
rule vec.cross.check-frame fixture
rule vec.cross.check-dimension fixture
rule vec.cross.convert-si fixture
rule vec.cross.component-i fixture
rule vec.cross.component-j fixture
rule vec.cross.component-k fixture
rule vec.cross.check-orthogonal-first fixture
rule vec.cross.check-orthogonal-second fixture
rule vec.cross.check-anticommutative fixture
rule vec.cross.report-precision fixture

family id physics.vectors.cartesian-scalar-product
topic_and_level The scalar product of two Cartesian vectors and the angle between them, PRD section 9 ALG-011 and chapter 3 of the PHYS 2410 scope
accepted_expression_grammar existing vector grammar for each operand, either unit-vector form such as 1 i + 2 j m or an ordered tuple such as (1, 2) m
accepted_input_forms structured Vector values read from Cartesian unit-vector or ordered-tuple form, of equal rank, with a flag asking for the angle as well as the product
domains_and_parameter_assumptions both vectors have the same rank and the same named frame, dimensions that may differ and whose product fits the dimension table, and for the angle, neither vector is the zero vector
supported_branches_and_degenerate_cases exact zero and negative components, a zero operand whose product is defined and whose angle is not, compatible dimensions that differ between the two operands, and compatible units with different SI scales
exact_special_function_and_numerical_result_policy exact rational SI conversion and component summation, rounded only for final reporting. The angle is placed against a right angle from the exact sign of the product, since a numeric angle needs an inverse cosine no exact rational route reaches
parser_module_ids src/units/units.cc, src/physics/scalar_product.cc
required_assumptions each vector is expressed in the named Cartesian frame, carried per vector with the name in it, as in "first vector frame is lab", and the two readings are related by "the two readings of the scalar product name one quantity"
test_group_ids scalar product, units
proof_obligation_ids obl.scalar-product.ranks-match, obl.scalar-product.frames-match, obl.scalar-product.dimension-product, obl.scalar-product.nonzero, obl.scalar-product.definition-after-checks, obl.physics.lookup-preserves-solutions, obl.physics.converts-by-table, obl.scalar-product.sum-is-the-definition, obl.scalar-product.commutative, obl.scalar-product.within-magnitude-bound, obl.scalar-product.angle-from-sign, obl.physics.reported-within-half-place, obl.plan.preconditions-hold
supported_methods write both readings of the definition, substitute the declared vectors into the component one, convert exactly to SI, sum the matching component products through the existing nps vector_dot, then check the sum against a reversed dot product and against the bound the geometric reading puts on it
unsupported_near_neighbors a numeric angle in degrees or radians, operands of unequal rank, implicit frame transformation, direct keypad entry, Lua bridge and Ki V4 menu exposure
solution_soundness_status verified by rank, frame and dimension-product checks, exact conversion, exact rational summation, an exact reversed dot product and an exact Cauchy-Schwarz comparison against the geometric reading
solution_completeness_status partial. The product is complete for equal-rank Cartesian operands within exact int64 rational bounds, and the angle is placed against a right angle rather than measured, because an inverse cosine is not reachable without the backend
corpus_case_ids scalar_product_torque_free_mixed_units, scalar_product_obtuse_angle
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet built for the device target
release_status unreleased
rule vec.dot.plan fixture
rule vec.dot.angle-plan fixture
rule vec.dot.check-rank fixture
rule vec.dot.check-frame fixture
rule vec.dot.check-dimension fixture
rule vec.dot.check-nonzero fixture
rule vec.dot.definition fixture
rule vec.dot.substitute fixture
rule vec.dot.convert-si fixture
rule vec.dot.component-sum fixture
rule vec.dot.check-commutative fixture
rule vec.dot.check-magnitude-bound fixture
rule vec.dot.interpret-angle fixture
rule vec.dot.report-precision fixture

family id physics.vectors.magnitude-components.two-dimension
reference_curriculum_set_ids StepCAS product requirements PHYS-016, M1 archetype 7
curriculum_source_locations StepCAS_Product_Requirements_Document.md PHYS-016 and section 11, .Internal/agent-pack/tasks/M1_VERTICAL_SLICE.md archetype 7
topic_and_level Two-dimensional magnitude and direction conversion with Cartesian components, PHYS-016 and M1 archetype 7
family_envelope_version 1
accepted_expression_grammar existing scalar expression grammar for magnitude, angle, x, and y fields
domains_and_parameter_assumptions a named Cartesian frame, a physical unit with exact positive SI scale, and an explicit degree or radian angle unit
supported_branches_and_degenerate_cases exact symbolic components, measured final-only approximation, and quadrant-aware inverse conversion including negative x and y, and rank-three reconstruction into a magnitude, a polar angle from the positive z axis and an azimuth from the positive x axis
exact_special_function_and_numerical_result_policy exact degree-to-radian factors and exact formulas require adapter zero checks before any requested final approximation, while host fixtures use scripted backend replies to test the protocol and evidence flow
accepted_input_forms typed MagnitudeAngleExpr or rank-two and rank-three Vector and VectorExpr values with explicit frame, unit, precision, and angle unit
word_language_profile_ids none, typed entry only
parser_module_ids src/core/parser.cc, src/physics/vector_components.cc, src/cas/giac/giac_adapter.cc
required_assumptions a supplied magnitude is nonnegative, a rank-three direction uses the declared spherical convention with the polar angle from the positive z axis and the azimuth from the positive x axis in the xy plane, and all components use the declared Cartesian frame and shared unit, carried with the names in them, as in "vector frame is lab" and "the supplied unit has dimension L"
supported_methods x = r cos(theta), y = r sin(theta), the existing exact vector magnitude, r = sqrt(x^2 + y^2 + z^2), quadrant-aware atan2(y, x), and the spherical polar angle atan2(sqrt(x^2 + y^2), z)
unsupported_near_neighbors the vector cross product, implicit basis transforms, implicit angle units, automatic symbolic nonnegativity proof, and a direction for the zero vector
strategy_ids vec.components.plan, vec.polar.plan, vec.polar.plan-three
test_group_ids vector components
proof_obligation_ids obl.vector-components.rank-two, obl.vector-components.rank-three, obl.vector-components.spherical-convention, obl.vector-components.polar-angle, obl.vector-components.frame-declared, obl.vector-components.angle-unit-explicit, obl.vector-components.dimensions-preserved, obl.vector-components.component-relations, obl.vector-components.magnitude-relation, obl.vector-components.quadrant-direction, obl.vector-components.precision-final, obl.plan.preconditions-hold
solution_soundness_status production answers are withheld unless metadata checks and backend zero checks pass, while golden fixtures validate that evidence flow with scripted replies and do not independently establish Giac algebra
solution_completeness_status partial, covering two-dimensional conversion in both directions and rank-three reconstruction into magnitude and direction, dependent on an available symbolic backend
corpus_case_ids vector_components_exact, vector_components_negative_quadrant, vector_components_spherical
explanation_review_status core rule sequence and proof evidence covered by golden fixtures, independent explanation review not yet recorded
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native typed Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status ARM module compiles and packages, calculator runtime not yet measured
capability_manifest_ids physics.vectors.magnitude-components.two-dimension
release_status in development, unreleased
rule vec.components.plan fixture
rule vec.components.check-frame fixture
rule vec.components.check-angle-unit fixture
rule vec.components.check-dimension fixture
rule vec.components.x fixture
rule vec.components.y fixture
rule vec.components.report-precision fixture
rule vec.polar.plan fixture
rule vec.polar.check-rank fixture
rule vec.polar.check-rank-three fixture
rule vec.polar.check-convention fixture
rule vec.polar.plan-three fixture
rule vec.polar.polar-angle fixture
rule vec.polar.check-frame fixture
rule vec.polar.check-angle-unit fixture
rule vec.polar.check-dimension fixture
rule vec.polar.magnitude fixture
rule vec.polar.direction fixture
rule vec.polar.report-precision fixture

family id physics.work.constant-force-dot-product
topic_and_level Constant-force component-vector work within PHYS-009
accepted_expression_grammar existing vector grammar for the force and the displacement, in unit-vector or ordered-tuple form with one unit spelling on each
accepted_input_forms a typed WorkProblem with force and displacement vectors, named Cartesian frames, and an explicit force profile
domains_and_parameter_assumptions force has dimension M L T^-2, displacement has dimension L, ranks and frames match, and the force is constant over the displacement
supported_branches_and_degenerate_cases rank two or three, positive, zero, or negative work, exact or measured components, and compatible multiplicative unit prefixes
exact_special_function_and_numerical_result_policy exact rational SI conversion and vector dot product, with measured precision applied only after candidate verification
parser_module_ids src/units/units.cc, src/physics/work.cc
required_assumptions force and displacement components use the same declared Cartesian frame, and the force is constant over the displacement, carried as "force profile is constant"
test_group_ids work
proof_obligation_ids obl.work.constant-force, obl.work.ranks-match, obl.work.frames-declared, obl.work.frames-match, obl.work.input-dimensions, obl.work.law-applied-after-checks, obl.work.result-dimension, obl.work.candidate-satisfies, obl.work.sign-interpreted, obl.physics.converts-by-table, obl.work.dot-is-the-definition, obl.work.rounding-within-half-place, obl.plan.preconditions-hold
supported_methods validate applicability and vector roles, convert exactly to SI, use the existing vector_dot operation, verify the candidate, and interpret its sign
unsupported_near_neighbors variable-force path integrals, implicit frame transformations, work-energy methods, rotational work, and the other PHYS-009 families
solution_soundness_status verified by applicability, rank, frame, input and result dimension, exact dot-product, candidate, and sign checks
solution_completeness_status partial, constant-force Cartesian component-vector work only
corpus_case_ids work_negative_mixed_units, work_variable_force_refused
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status ARM core compiles, calculator runtime not yet measured
release_status in development, unreleased
rule physics.work.plan fixture
rule physics.work.check-applicability fixture
rule physics.work.check-rank fixture
rule physics.work.check-frame-declared fixture
rule physics.work.check-frame-match fixture
rule physics.work.check-input-dimensions fixture
rule physics.work.constant-force-definition fixture
rule physics.work.convert-si fixture
rule physics.work.check-result-dimension fixture
rule physics.work.evaluate-dot fixture
rule physics.work.check-candidate fixture
rule physics.work.interpret-sign fixture
rule physics.work.significant-figures fixture

family id calculus.derivative.single-variable
topic_and_level Supported derivative rules and nested combinations, PRD section 22.1
accepted_expression_grammar sums, products, quotients, negations, integer powers and calls to a named function, over numbers and the one variable, with a typed decimal read as the fraction it names and refused in an exponent
accepted_input_forms an expression in one variable, with the supported functions
domains_and_parameter_assumptions rules apply where the original expression and its derivative are defined. Logarithms, roots, quotients and reciprocal powers carry their corresponding domain restrictions
supported_branches_and_degenerate_cases a constant with respect to the variable, including another symbol
exact_special_function_and_numerical_result_policy exact rationals over int64, refusing rather than wrapping
parser_module_ids src/core/parser.cc, src/steps/differentiate.cc
required_assumptions none carried for the family, a condition a form needs is recorded as a domain restriction on the step that introduces it, as the quotient rule records a denominator that is not zero
test_group_ids differentiate
proof_obligation_ids obl.calculus.rule-preserves-value, obl.plan.preconditions-hold
supported_methods one rule per step, chosen by the shape of the node and recorded by name
unsupported_near_neighbors implicit differentiation, partial derivatives, higher derivatives in one step
solution_soundness_status cross-checked against Giac on the calculator, which is evidence rather than proof
solution_completeness_status partial, the thirteen rules the header lists and no others
corpus_case_ids the golden fixtures naming this family
device_performance_status measured on the physical calculator, see STATUS.md
direct_keypad_entry_status entered from the calculator keypad, measured
isolated_runtime_status unqualified, the complete supported corpus must pass with USB physically disconnected
release_status unreleased
rule calculus.differentiate.rules fixture
rule d.constant fixture
rule d.variable fixture
rule d.sum fixture
rule d.constant-multiple fixture
rule d.product fixture
rule d.quotient fixture
rule d.power fixture
rule d.chain fixture
rule d.function fixture

family id calculus.integral.indefinite.single-variable
topic_and_level Supported elementary indefinite integrals, PRD section 22.1
accepted_expression_grammar sums, products, negations, integer powers including the reciprocal, and calls to a named function, over numbers and the one variable, with a typed decimal read as the fraction it names and refused in an exponent
accepted_input_forms an expression in one variable, within the Milestone 3 envelope
domains_and_parameter_assumptions the logarithm needs a positive argument, and a symbolic coefficient is assumed non-zero
supported_branches_and_degenerate_cases the reciprocal power, which integrates to a logarithm rather than by the power rule. Affine logarithms use integration by parts. Affine square roots use the half-power rule on their nonnegative real branch. Sums and constant multiples use existing rules
exact_special_function_and_numerical_result_policy exact rationals over int64, refusing rather than wrapping
parser_module_ids src/core/parser.cc, src/steps/integrate.cc
required_assumptions whatever the integrand's own form needs, recorded as it is met rather than remembered, as in "x > 0" for the logarithm a reciprocal integrates to
test_group_ids integrate
proof_obligation_ids obl.integrate.derivative-returns-integrand, obl.calculus.rule-preserves-value, obl.calculus.family-adds-a-constant, obl.plan.preconditions-hold
supported_methods one rule per step, then VER-005's check that differentiating the answer returns the integrand
unsupported_near_neighbors general integration by parts, nonlinear substitution, general rational exponents, tan, reciprocal square roots, powers of logarithms and logarithms with nonlinear arguments
solution_soundness_status the answer is differentiated by rule. Canonical agreement verifies simple identities. Inconclusive canonical comparisons require an exact zero difference from Giac under recorded domain restrictions, otherwise the answer is withheld
solution_completeness_status partial, one substitution deep and linear arguments only
corpus_case_ids the golden fixtures naming this family
device_performance_status measured on the physical calculator, see STATUS.md
direct_keypad_entry_status entered from the calculator keypad, measured
isolated_runtime_status unqualified, the complete supported corpus must pass with USB physically disconnected
release_status unreleased
rule calculus.integrate.rules fixture
rule calculus.integrate.check-by-differentiation fixture
rule i.power fixture
rule i.reciprocal fixture
rule i.sum fixture
rule i.constant-multiple fixture
rule i.linear-substitution fixture
rule i.constant-of-integration fixture
rule i.logarithm-parts fixture
rule i.constant fixture
rule i.function fixture

family id calculus.integral.definite.single-variable
topic_and_level Elementary definite integrals in one real variable
family_envelope_version 1
accepted_expression_grammar constants, sums, constant multiples, integer powers and supported sin, cos, exp, ln and sqrt calls with affine arguments, within the existing antiderivative rules
accepted_input_forms int(expression,variable,lower,upper), integrate and the TI integral glyph in Exact mode with finite rational bounds
domains_and_parameter_assumptions the integrand and the computed antiderivative must be continuous on the complete interval, including endpoints. Each logarithmic primitive selects a positive argument using the sign of its affine base. The chosen branch is checked over the complete interval
supported_branches_and_degenerate_cases reversed bounds, equal bounds for continuous integrands, rational endpoints and reciprocal powers away from zero, including negative affine arguments and sums using different logarithm branches. Affine square roots allow zero radicands at endpoints using the continuous primitive
exact_special_function_and_numerical_result_policy exact rational or symbolic endpoint expressions. Checked rational arithmetic refuses overflow. Unsupported native cases may expose a distinctly labeled Giac answer without claiming a complete walkthrough
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/calculus.cc, src/steps/integrate.cc
required_assumptions none carried globally. Interval validity and primitive branch restrictions are checked locally
test_group_ids calculus, command, adapter
proof_obligation_ids obl.calculus.rule-preserves-value, obl.integrate.derivative-returns-integrand, obl.plan.preconditions-hold, obl.calculus.giac-agreement
supported_methods interval continuity checks, a native antiderivative verified by differentiation with Giac exact identity checks when needed, affine logarithm integration by parts, the fundamental theorem of calculus and exact endpoint subtraction
unsupported_near_neighbors improper integrals, symbolic bounds, unproved interval continuity, general integration by parts, logarithm powers and nonlinear substitution
solution_soundness_status native expected-answer, domain refusal, budget and rule-schema tests pass. Actual Giac bridge comparisons pass under host sanitizers. Physical qualification remains pending
solution_completeness_status partial, limited by the stated grammar, checked arithmetic and resource budgets. No native result survives failed verification or terminal cancellation
corpus_case_ids defint_polynomial, defint_zero_width, defint_reciprocal, defint_elementary, defint_logarithm_affine, defint_logarithm_reversed, defint_square_root_endpoint
explanation_review_status semantic fixtures recorded. Independent final review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status command and TI template dispatch implemented. Handheld qualification pending
isolated_runtime_status ARM package built. Emulator and physical-device qualification pending
capability_manifest_ids calculus.integral.definite.single-variable
release_status in development, unreleased
rule defint.interval fixture
rule defint.zero-width fixture
rule defint.fundamental-theorem fixture
rule defint.subtract fixture
rule calculus.check-giac fixture
rule i.constant fixture
rule i.logarithm-parts fixture
rule calculus.integrate.rules fixture
rule calculus.integrate.check-by-differentiation fixture
rule i.power fixture
rule i.reciprocal fixture
rule i.constant-multiple fixture
rule i.linear-substitution fixture

family id calculus.limit.single-variable
topic_and_level Finite and infinite limits in one real variable
family_envelope_version 1
accepted_expression_grammar supported continuous arithmetic and elementary expressions at rational points, polynomial square-root boundaries with an exact real approach-domain proof, rational functions over rational-coefficient polynomials with degree bounds of 32, and smooth elementary numerators over those polynomial denominators when exact rational derivative values establish the required vanishing orders
accepted_input_forms limit(expression,variable,point) and limit(expression,variable,point,direction), including the TI editor alias lim. Direction is -1 for left, 0 for both and 1 for right. Infinite points use infinity, the TI infinity symbol or either negative spelling without a side argument
domains_and_parameter_assumptions original denominators remain excluded after cancellation. Rational denominators must be nonzero polynomials with isolated roots. Real principal branches and radians apply
supported_branches_and_degenerate_cases direct substitution, one-sided and two-sided square-root boundaries with nonnegative polynomial radicands on every requested side, zero-polynomial radicands, removable singularities, repeated polynomial zeros, smooth sine, cosine, exponential, logarithm and positive-radicand square-root numerators, one-sided poles, matching signed infinities, opposite-sided nonexistence and rational limits at infinity
exact_special_function_and_numerical_result_policy exact finite expressions, signed infinity or an explicit nonexistent two-sided limit. Giac fallback answers remain distinct from native walkthrough completion
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/calculus.cc, src/steps/differentiate.cc
required_assumptions none carried globally. Original excluded points and the requested direction are recorded locally
test_group_ids calculus, command, adapter
proof_obligation_ids obl.calculus.rule-preserves-value, obl.limit.classification, obl.plan.preconditions-hold
supported_methods continuity, exact derivative order and sign proofs for polynomial root domains, quotient normalization with excluded points, repeated differentiation for removable zeros with smoothness checks for elementary numerators, exact derivative order and sign comparison, and leading-term degree comparison at infinity
unsupported_near_neighbors symbolic finite points, square-root boundary approaches without a proved real neighborhood, nonpolynomial denominators in indeterminate forms, elementary numerators without proved smoothness or exact derivative values, transcendental limits at infinity, oscillatory singularities, multivariable limits, identically zero denominators and expressions exceeding arithmetic or shared work budgets
solution_soundness_status independent expected answers and native rule-schema checks pass. Actual Giac bridge comparisons pass under host sanitizers. Infinite and nonexistent native classifications use exact polynomial order and sign certificates
solution_completeness_status partial, within the stated grammar and resource bounds. A verified signed infinity is distinct from a finite real result, and opposite one-sided infinities do not establish a two-sided limit
corpus_case_ids limit_continuous, limit_root_boundary, limit_removable, limit_pole, limit_no_two_sided, limit_at_infinity
explanation_review_status semantic fixtures recorded. Independent final review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status command and five limit templates implemented. Handheld qualification pending
isolated_runtime_status ARM package built. Emulator and physical-device qualification pending
capability_manifest_ids calculus.limit.single-variable
release_status in development, unreleased
rule limit.continuity fixture
rule limit.real-domain fixture
rule limit.rational-form fixture
rule limit.lhopital fixture
rule limit.evaluate fixture
rule limit.infinity fixture
rule limit.classify fixture
rule calculus.differentiate.rules fixture
rule d.constant fixture
rule d.variable fixture
rule d.sum fixture
rule d.power fixture

family id physics.kinematics.constant-acceleration.one-dimension
topic_and_level One-dimensional constant-acceleration kinematics, PRD section 22.1
accepted_expression_grammar the five named symbols v0, v, a, t and x, one per part, with the existing quantity grammar on each value and the parts separated by commas, new lines or semicolons
accepted_input_forms structured entry of an unknown and quantities with units, not prose
domains_and_parameter_assumptions acceleration is constant over the interval, and motion is along one axis
supported_branches_and_degenerate_cases a route through an intermediate quantity when no single equation reaches the unknown
exact_special_function_and_numerical_result_policy exact rationals throughout, rounded once at the end to the figures the measured givens allow
parser_module_ids src/units/units.cc, src/physics/kinematics.cc
required_assumptions "acceleration is constant" and "motion is along one axis, positive in the chosen direction", both carried on the solve and the first also carried on the step that plans with it
test_group_ids kinematics, units
# The linear solver's obligation appears here too, for the same reason its rules do: a kinematics
# solve nests it and the record carries what it raised.
proof_obligation_ids obl.kinematics.dimensions-agree, obl.kinematics.rounding-within-half-place, obl.linear.candidate-satisfies, obl.eq.same-solutions, obl.kinematics.conversion-preserves-solutions, obl.kinematics.substitution-preserves-solutions, obl.rearrange.same-solutions, obl.plan.preconditions-hold
supported_methods backward chaining over the four constant-acceleration equations, each candidate offered to the linear solver
unsupported_near_neighbors two bodies at equal position, forces, energy
solution_soundness_status verified, dimensions checked and the answer substituted back by the linear solver
solution_completeness_status partial, one body and one axis
corpus_case_ids the golden fixtures naming this family
device_performance_status measured on the physical calculator, see STATUS.md
direct_keypad_entry_status entered from the calculator keypad, measured
isolated_runtime_status unqualified, the complete supported corpus must pass with USB physically disconnected
release_status unreleased
rule physics.kinematics.constant-acceleration fixture
rule kin.substitute fixture
rule kin.convert-units fixture
rule kin.significant-figures fixture
rule physics.kinematics.check-dimensions fixture
rule kin.rearrange device
# A kinematics solve nests the linear solver, whose steps land in the same derivation, so a reader
# of one of these records sees these rules and the family exercises them.
rule eq.linear.inverse-operations fixture
rule eq.linear.check-by-substitution fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture
# The symbolic isolation is ALG-007's, run with no backend, so its moves land here for the same
# reason and a reader of a kinematics record sees the algebra before the arithmetic.
rule alg.rearrange.swap-sides fixture
rule alg.rearrange.subtract-both-sides fixture
rule alg.rearrange.divide-both-sides fixture

family id physics.kinematics.constant-acceleration.two-dimension
topic_and_level Two-dimensional motion under a constant acceleration in any direction, PRD section 22.1 and PHYS-028's Event/State/Interval distinction
accepted_expression_grammar existing scalar expression grammar for each vector component
accepted_input_forms one typed PlanarKinematicsProblem naming a body, a declared frame, rank-two velocity and acceleration vectors and an interval, with the projectile flag clear
domains_and_parameter_assumptions both vectors are rank two, declare the same named frame and carry matching stage identity, and the acceleration is constant over the interval with no constraint on its direction
supported_branches_and_degenerate_cases the general case where both components of the acceleration may be non-zero, which is what separates this family from the projectile specialization beside it
exact_special_function_and_numerical_result_policy exact rational SI conversion and per-component solving with the existing one-dimensional linear engine, with measured precision applied only to the final report
parser_module_ids src/units/units.cc, src/physics/planar_kinematics.cc
required_assumptions the frame and the axis convention are declared rather than inferred, carried as "positive i is right and positive j is up", "the acceleration is constant over the whole interval" and "one shared time links both axes"
test_group_ids planar kinematics
proof_obligation_ids obl.plan.preconditions-hold, obl.planar-kinematics.rank-two, obl.planar-kinematics.frames-declared, obl.planar-kinematics.frames-match, obl.planar-kinematics.stage-identity, obl.planar-kinematics.input-dimensions, obl.planar-kinematics.definition-after-checks, obl.physics.lookup-preserves-solutions, obl.physics.converts-by-table, obl.planar-kinematics.result-dimensions, obl.planar-kinematics.component-i, obl.planar-kinematics.component-j, obl.planar-kinematics.shared-time
supported_methods validate rank, frame, stage identity and dimensions, then solve each axis independently with the existing one-dimensional kinematics engine under one shared time
unsupported_near_neighbors an acceleration that changes over the interval, air resistance, more than two dimensions, and the apex cross-check, which belongs to the projectile specialization
solution_soundness_status verified by rank, frame, stage and dimension checks and by the shared-time check that links the two axes
solution_completeness_status partial, one body under one constant acceleration over one interval
corpus_case_ids planar_kinematics_general_two_dimension
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status ARM module compiles and packages, calculator runtime not yet measured
release_status in development, unreleased
# No projectile-plan, check-projectile or check-apex-routes rule here. Those three belong to the
# specialization below, which is the whole difference between the two families' recorded steps.
rule physics.planar-kinematics.plan fixture
rule physics.planar-kinematics.check-rank fixture
rule physics.planar-kinematics.check-frame-declared fixture
rule physics.planar-kinematics.check-frame-match fixture
rule physics.planar-kinematics.check-stages fixture
rule physics.planar-kinematics.check-input-dimensions fixture
rule physics.planar-kinematics.definition fixture
rule physics.planar-kinematics.substitute fixture
rule physics.planar-kinematics.convert-si fixture
rule physics.planar-kinematics.check-result-dimension fixture
rule physics.planar-kinematics.component-i fixture
rule physics.planar-kinematics.component-j fixture
rule physics.planar-kinematics.check-shared-time fixture
rule physics.planar-kinematics.significant-figures fixture

family id physics.kinematics.constant-acceleration.projectile.two-dimension
topic_and_level Two-dimensional projectile motion under constant vertical acceleration, PRD section 22.1 and PHYS-028's Event/State/Interval distinction
accepted_expression_grammar existing scalar expression grammar for each vector component
accepted_input_forms one typed PlanarKinematicsProblem naming a body, a declared frame, rank-two position, velocity and acceleration vectors and the projectile flag
domains_and_parameter_assumptions both vectors are rank two, declare the same named frame, carry matching stage identity, and the acceleration has a zero horizontal component and a negative vertical component
supported_branches_and_degenerate_cases the projectile specialization of the general two-dimension constant-acceleration engine, including a second independent route to the apex height that must agree with the first
exact_special_function_and_numerical_result_policy exact rational SI conversion and per-component solving with the existing one-dimensional linear engine, with measured precision applied only to the final report
parser_module_ids src/units/units.cc, src/physics/planar_kinematics.cc
required_assumptions the frame and the axis convention are declared rather than inferred, and the specialization is stated, carried as "positive i is right and positive j is up", "the acceleration is constant over the whole interval", "one shared time links both axes" and "the projectile specialization leaves the horizontal axis unaccelerated"
test_group_ids planar kinematics
proof_obligation_ids obl.plan.preconditions-hold, obl.planar-kinematics.rank-two, obl.planar-kinematics.frames-declared, obl.planar-kinematics.frames-match, obl.planar-kinematics.stage-identity, obl.planar-kinematics.input-dimensions, obl.planar-kinematics.projectile, obl.planar-kinematics.definition-after-checks, obl.physics.lookup-preserves-solutions, obl.physics.converts-by-table, obl.planar-kinematics.result-dimensions, obl.planar-kinematics.component-i, obl.planar-kinematics.component-j, obl.planar-kinematics.shared-time
supported_methods validate rank, frame, stage identity and dimensions, check the projectile acceleration, then solve each axis independently with the existing one-dimensional kinematics engine and cross-check the apex height by a second route
unsupported_near_neighbors general two-dimensional motion with a nonzero horizontal acceleration, oblique launch from a moving platform, air resistance, and more than two dimensions
solution_soundness_status verified by rank, frame, stage and dimension checks, the projectile acceleration precondition, and a second independent route to the apex height that must agree with the first
solution_completeness_status partial, one body under constant acceleration with the horizontal axis unaccelerated
corpus_case_ids planar_kinematics_projectile_mixed_units
device_performance_status not measured
direct_keypad_entry_status native Lua bridge implemented as nps.planar_kinematics, reachable from the Ki V4 menu's guided physics browser as of #382
isolated_runtime_status ARM module compiles and packages, calculator runtime not yet measured
release_status in development, unreleased
rule physics.planar-kinematics.projectile-plan fixture
rule physics.planar-kinematics.check-rank fixture
rule physics.planar-kinematics.check-frame-declared fixture
rule physics.planar-kinematics.check-frame-match fixture
rule physics.planar-kinematics.check-stages fixture
rule physics.planar-kinematics.check-input-dimensions fixture
rule physics.planar-kinematics.check-projectile fixture
rule physics.planar-kinematics.definition fixture
rule physics.planar-kinematics.substitute fixture
rule physics.planar-kinematics.convert-si fixture
rule physics.planar-kinematics.check-result-dimension fixture
rule physics.planar-kinematics.component-i fixture
rule physics.planar-kinematics.component-j fixture
rule physics.planar-kinematics.check-shared-time fixture
rule physics.planar-kinematics.significant-figures fixture

family id physics.kinematics.motion-graphs.piecewise-area
topic_and_level Reading signed area under a piecewise a(t) or v(t) curve, chapter 2 motion graphs
accepted_expression_grammar existing algebraic expression grammar for each segment's curve, evaluated in the time variable t
accepted_input_forms one typed GraphIntegrationProblem naming the reading direction, the curve's dimension, a query interval and an ordered list of per-segment curve expressions with their own sub-intervals
domains_and_parameter_assumptions each segment's curve is defined and continuous on its own sub-interval, and the segments together cover the requested query interval with no gap
supported_branches_and_degenerate_cases a reversed query interval, a single segment, multiple segments whose own sub-intervals only partially overlap the query interval, and a signed area where part of the curve lies below the axis
exact_special_function_and_numerical_result_policy exact rational SI conversion of interval bounds and exact rational evaluation of each segment's antiderivative at its bounds, summed as exact rationals
parser_module_ids src/units/units.cc, src/physics/graph_integration.cc, src/core/parser.cc, src/steps/integrate.cc
required_assumptions none, no golden fixture yet exercises this family so no engine string is joined against one
test_group_ids graph integration
proof_obligation_ids none, no golden fixture yet exercises this family so no obligation is joined against one
supported_methods stitch each segment's overlap with the query interval, integrate it with the existing indefinite-integral engine, evaluate the antiderivative at both ends and sum the signed differences
unsupported_near_neighbors a discontinuity inside one segment, more than one independent variable, and word-language input
solution_soundness_status each segment's antiderivative is differentiated by rule and checked, by the composed indefinite-integral family this one owns no rule of; its own interval and dimension checks are refused as InvalidInput, DimensionMismatch or IncompleteCoverage outcomes rather than recorded derivation steps
solution_completeness_status partial, limited to curves the composed indefinite-integral engine supports on each segment and to piecewise coverage with no gap
corpus_case_ids not yet filed
device_performance_status not measured
direct_keypad_entry_status not yet implemented
isolated_runtime_status host core validated, ARM packaging and calculator runtime not yet measured
release_status in development, unreleased
rule calculus.integrate.rules device
rule calculus.integrate.check-by-differentiation device
rule i.power device
rule i.reciprocal device
rule i.sum device
rule i.constant-multiple device
rule i.linear-substitution device
rule i.constant-of-integration device
rule i.logarithm-parts device
rule i.constant device
rule i.function device
# This family composes with the existing indefinite-integral engine for each segment's closed
# form rather than carrying a second integrator, so every rule id above is that family's own.

family id physics.forces.newton-second-law
topic_and_level One body under Newton's second law on axes along and across its supporting surface, PRD section 9 PHYS-008 and chapter 4 of the PHYS 2410 scope
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities rather than as a typed equation, and the along-axis and across-axis sums are built by the engine
accepted_input_forms one typed ForcesProblem naming the body and its support, a mass, a gravitational field strength, a surface that is horizontal or an incline declared by the exact sine and cosine of its angle, an optional applied force and tension, a friction model with its coefficient, a declared sense of motion and the unknown to solve for
domains_and_parameter_assumptions the body is a particle in contact with the surface, the across-axis acceleration is zero, and an incline's sine and cosine are given as exact rationals, an inexact pair being refused rather than approximated
supported_branches_and_degenerate_cases a horizontal surface and an incline, no friction, kinetic friction opposing a declared sense of motion, and static friction compared against its own maximum, with the four unknowns acceleration, applied force, normal force and friction force
exact_special_function_and_numerical_result_policy exact rational arithmetic throughout, with the incline entered as an exact sine and cosine so no trigonometric evaluation is needed and no value is approximated
parser_module_ids src/units/units.cc, src/physics/forces.cc
required_assumptions the particle model and the axis choice are stated rather than inferred, carried as "the body is a particle, so every force acts at one point", "the axes run along the supporting surface and across it" and "the surface stays in contact, so the across-axis acceleration is zero"
test_group_ids forces
proof_obligation_ids obl.plan.preconditions-hold, obl.forces.input-dimensions, obl.forces.exact-angle, obl.forces.weight-components, obl.forces.normal-from-balance, obl.forces.pairs-separate, obl.forces.kinetic-friction, obl.forces.unknown-isolated, obl.forces.residual-zero, obl.forces.result-dimension, obl.forces.static-within-limit
supported_methods inventory the forces on the one body, resolve each onto the two surface axes, sum each axis under Newton's second law, isolate the unknown and check the along-axis residual and the result's dimension
unsupported_near_neighbors more than one body sharing an unknown, an incline whose sine and cosine are not exact rationals, drag or any velocity-dependent force, circular motion, and word-language input
solution_soundness_status verified, the input dimensions and the incline angle are checked before solving, the along-axis residual is rebuilt from the published inventory and checked against zero, and the result's dimension is checked against the unknown
solution_completeness_status partial, one body on one surface with the four named unknowns
corpus_case_ids forces_horizontal_kinetic_friction, forces_incline_static_friction, forces_incline_angle_refused
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
# Newton's third law acts between bodies, so the pair step is recorded and its obligation keeps the
# pairs out of the one body's inventory rather than adding a force to it.
rule physics.forces.plan fixture
rule physics.forces.check-input-dimensions fixture
rule physics.forces.check-angle fixture
rule physics.forces.weight fixture
rule physics.forces.normal-force fixture
rule physics.forces.third-law-pairs fixture
rule physics.forces.kinetic-friction fixture
rule physics.forces.static-friction-limit fixture
rule physics.forces.solve-unknown fixture
rule physics.forces.check-residual fixture
rule physics.forces.check-result-dimension fixture

family id physics.circular-motion.uniform
topic_and_level Uniform circular motion relating speed, radius and period, with the centripetal acceleration reported alongside, PRD section 9 PHYS-010
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds the relation it needs
accepted_input_forms one typed CircularMotionProblem naming which of speed, radius and period is unknown and giving the other two as quantities
domains_and_parameter_assumptions the speed is constant around the circle and the radius is positive, a zero or negative radius being refused rather than answered
supported_branches_and_degenerate_cases each of speed, radius and period as the unknown, with a different relation selected for each, and the centripetal acceleration computed for all three once speed and radius are both known
exact_special_function_and_numerical_result_policy exact rational SI conversion and exact rational isolation, with measured precision applied only to the reported value
parser_module_ids src/units/units.cc, src/physics/circular_motion.cc, src/physics/relation.cc, src/steps/linear.cc
required_assumptions the constant-speed model and what the reported acceleration means are stated rather than inferred, carried as "the speed is constant, so the motion traces a circle at constant rate" and "the reported acceleration is the magnitude directed toward the centre of the circle"
test_group_ids circular motion
proof_obligation_ids obl.plan.preconditions-hold, physics.circular-motion.period.dimensions-agree, physics.circular-motion.period.candidate-satisfies, physics.circular-motion.speed.dimensions-agree, physics.circular-motion.speed.candidate-satisfies, physics.circular-motion.radius.dimensions-agree, physics.circular-motion.radius.candidate-satisfies, physics.circular-motion.acceleration.dimensions-agree, physics.circular-motion.acceleration.candidate-satisfies, obl.physics.lookup-preserves-solutions, obl.physics.reported-within-half-place, obl.eq.same-solutions, obl.linear.candidate-satisfies
supported_methods select the relation that isolates the requested unknown, check its dimensions, substitute the knowns exactly in SI, isolate with the existing linear engine, check the candidate in the original relation, then run the same sequence again for the centripetal acceleration
unsupported_near_neighbors non-uniform circular motion, angular velocity and angular acceleration as their own quantities, the force that causes the acceleration, and orbital mechanics
solution_soundness_status verified, each relation's dimensions are checked before substitution and each candidate is put back into the relation it came from
solution_completeness_status partial, the three positions of the uniform relation with the centripetal acceleration beside them
corpus_case_ids circular_motion_period_and_acceleration, circular_motion_speed_from_period, circular_motion_radius_from_period
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
# One rule prefix per unknown, because a different relation is selected for each, and the
# acceleration's own four rules run for every one of them.
rule physics.circular-motion.period.definition fixture
rule physics.circular-motion.period.check-dimensions fixture
rule physics.circular-motion.period.substitute fixture
rule physics.circular-motion.period.check-candidate fixture
rule physics.circular-motion.speed.definition fixture
rule physics.circular-motion.speed.check-dimensions fixture
rule physics.circular-motion.speed.substitute fixture
rule physics.circular-motion.speed.check-candidate fixture
rule physics.circular-motion.speed.significant-figures fixture
rule physics.circular-motion.radius.definition fixture
rule physics.circular-motion.radius.check-dimensions fixture
rule physics.circular-motion.radius.substitute fixture
rule physics.circular-motion.radius.check-candidate fixture
rule physics.circular-motion.radius.significant-figures fixture
rule physics.circular-motion.acceleration.definition fixture
rule physics.circular-motion.acceleration.check-dimensions fixture
rule physics.circular-motion.acceleration.substitute fixture
rule physics.circular-motion.acceleration.check-candidate fixture
rule physics.circular-motion.acceleration.significant-figures fixture
# The isolation is the existing linear engine's, run with no backend, so its moves land in the same
# derivation and a reader of one of these records sees them.
rule eq.linear.inverse-operations fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture
rule eq.linear.check-by-substitution fixture

family id physics.gravitation.point-masses
topic_and_level Newton's law of gravitation between two point masses, PRD section 9 PHYS-012
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds F = G*m1*m2*r^-2
accepted_input_forms one typed RelationProblem naming which of the force, the two masses and the separation is unknown and giving the other three as quantities
domains_and_parameter_assumptions each body is a point mass or a spherically symmetric sphere, the separation is between centres, and the tabulated gravitational constant is used exactly
supported_branches_and_degenerate_cases any one of the four positions as the unknown, since the relation is linear in each after exact SI substitution
exact_special_function_and_numerical_result_policy exact rational SI conversion and exact rational isolation, with the gravitational constant carried as an exact tabulated rational rather than a decimal
parser_module_ids src/units/units.cc, src/physics/gravitation.cc, src/physics/relation.cc, src/steps/linear.cc
required_assumptions the point-mass model, the two-body restriction and the exact constant are stated rather than inferred, carried as "each body is a point mass, or a sphere whose mass distribution is spherically symmetric so its field outside equals that of a point mass at its centre", "the separation is measured between the two centres and no third body contributes" and "the tabulated gravitational constant 6.674e-11 m^3 kg^-1 s^-2 is used exactly and its measurement uncertainty is not propagated"
test_group_ids gravitation
proof_obligation_ids obl.plan.preconditions-hold, physics.gravitation.dimensions-agree, physics.gravitation.candidate-satisfies, obl.physics.lookup-preserves-solutions, obl.eq.same-solutions, obl.linear.candidate-satisfies
supported_methods check the relation's dimensions, substitute the knowns exactly in SI, isolate the unknown with the existing linear engine and check the candidate in the original relation
unsupported_near_neighbors more than two bodies, gravitational fields and potentials as their own quantities, orbital motion, and the uncertainty carried by the measured constant
solution_soundness_status verified, the dimensions are checked before substitution and the candidate is put back into the original relation
solution_completeness_status partial, the four positions of the two-body law
corpus_case_ids gravitation_two_point_masses
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.gravitation.definition fixture
rule physics.gravitation.check-dimensions fixture
rule physics.gravitation.substitute fixture
rule physics.gravitation.check-candidate fixture
rule eq.linear.inverse-operations fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture
rule eq.linear.check-by-substitution fixture

family id physics.oscillation.restoring-force
topic_and_level The linear restoring force of simple harmonic motion, PRD section 9 PHYS-011
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds F = k*x
accepted_input_forms one typed RelationProblem naming which of the restoring force, the stiffness and the displacement is unknown and giving the other two as quantities
domains_and_parameter_assumptions the restoring force is linear in the displacement from equilibrium, the motion is undamped and undriven, and the relation is between magnitudes
supported_branches_and_degenerate_cases any one of the three positions as the unknown, since the relation is linear in each
exact_special_function_and_numerical_result_policy exact rational SI conversion and exact rational isolation, with measured precision applied only to the reported value
parser_module_ids src/units/units.cc, src/physics/oscillation.cc, src/physics/relation.cc, src/steps/linear.cc
required_assumptions the linearity, the direction convention and the absence of damping are stated rather than inferred, carried as "the restoring force is linear in the displacement from equilibrium, which for a pendulum holds only in the small-angle approximation", "the magnitudes are related here, with the restoring force directed opposite to the displacement" and "the motion is undamped and no driving force acts"
test_group_ids oscillation
proof_obligation_ids obl.plan.preconditions-hold, physics.oscillation.dimensions-agree, physics.oscillation.candidate-satisfies, obl.physics.lookup-preserves-solutions, obl.eq.same-solutions, obl.linear.candidate-satisfies
supported_methods check the relation's dimensions, substitute the knowns exactly in SI, isolate the unknown with the existing linear engine and check the candidate in the original relation
unsupported_near_neighbors the period and frequency of the oscillation, the energy stored, damped or driven motion, and the pendulum outside the small-angle approximation
solution_soundness_status verified, the dimensions are checked before substitution and the candidate is put back into the original relation
solution_completeness_status partial, the three positions of the restoring-force relation only
corpus_case_ids oscillation_restoring_force
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.oscillation.definition fixture
rule physics.oscillation.check-dimensions fixture
rule physics.oscillation.substitute fixture
rule physics.oscillation.check-candidate fixture
rule eq.linear.inverse-operations fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture
rule eq.linear.check-by-substitution fixture

family id physics.wave.speed-frequency-wavelength
topic_and_level The basic traveling-wave relation v = f*lambda, PRD section 9 PHYS-011
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds v = f*lambda
accepted_input_forms one typed RelationProblem naming which of the speed, the frequency and the wavelength is unknown and giving the other two as quantities
domains_and_parameter_assumptions the medium is uniform and non-dispersive over the band in question, and the wave is periodic and travelling rather than standing
supported_branches_and_degenerate_cases any one of the three positions as the unknown, and a wavelength given with a unit prefix, which reaches the conversion step an all-SI problem never runs
exact_special_function_and_numerical_result_policy exact rational SI conversion and exact rational isolation, with measured precision applied only to the reported value
parser_module_ids src/units/units.cc, src/physics/oscillation.cc, src/physics/relation.cc, src/steps/linear.cc
required_assumptions the medium and the kind of wave are stated rather than inferred, carried as "the medium is uniform and non-dispersive over the band in question, so one speed describes the wave" and "the wave is periodic and travelling rather than standing, and the frequency is the source frequency the medium carries unchanged"
test_group_ids oscillation
proof_obligation_ids obl.plan.preconditions-hold, physics.wave.dimensions-agree, physics.wave.candidate-satisfies, obl.physics.scale-preserves-solutions, obl.physics.lookup-preserves-solutions, obl.eq.same-solutions, obl.linear.candidate-satisfies
supported_methods check the relation's dimensions, convert any prefixed quantity exactly to SI, substitute the knowns, isolate the unknown with the existing linear engine and check the candidate in the original relation
unsupported_near_neighbors standing waves and their harmonics, dispersion, wave amplitude and energy, interference and the Doppler shift
solution_soundness_status verified, the dimensions are checked before substitution, each conversion is checked against its table scale and the candidate is put back into the original relation
solution_completeness_status partial, the three positions of the traveling-wave relation only
corpus_case_ids wave_speed_mixed_units
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.wave.definition fixture
rule physics.wave.check-dimensions fixture
rule physics.wave.convert-units fixture
rule physics.wave.substitute fixture
rule physics.wave.check-candidate fixture
rule eq.linear.inverse-operations fixture
rule eq.collect-like-terms fixture
rule eq.divide-both-sides fixture
rule eq.linear.check-by-substitution fixture

# The five relativity families share one engine and one rule set, so their blocks differ in the
# relation each applies rather than in the steps each records. Velocity addition is the one that
# needs no Lorentz factor, so it names neither that rule nor its obligation.

family id physics.relativity.time-dilation
topic_and_level Time dilation between two inertial frames in relative motion along a shared x axis, PRD section 9 PHYS-023
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds dt = gamma*dt0
accepted_input_forms one typed RelativityProblem naming both frames, the boost as a fraction of c and the proper time
domains_and_parameter_assumptions both frames are inertial, the boost is below the speed of light, and 1 - beta^2 is the square of an exact fraction so gamma is an exact rational
supported_branches_and_degenerate_cases a boost along either direction of the shared axis, since gamma reads beta squared, and a refusal when the speed is at or above c or when gamma has no exact rational value
exact_special_function_and_numerical_result_policy exact rational throughout, reporting nothing rather than a decimal when the Lorentz factor is not an exact rational
parser_module_ids src/units/units.cc, src/physics/relativity.cc
required_assumptions the frames, the boost direction and the meaning of the proper time are stated rather than inferred, carried as "both frames are inertial, so no acceleration enters the transformation" and "the two events happen at the same place in the moving frame, so its reading is the proper time"
test_group_ids relativity
proof_obligation_ids obl.plan.preconditions-hold, obl.relativity.frames-distinct, obl.relativity.speed-below-light, obl.relativity.factor-identity, obl.physics.lookup-preserves-solutions, obl.relativity.invariant-holds
supported_methods check the two frames are declared and distinct, check the boost is below c, form the exact Lorentz factor, apply the dilation relation and check the result against a quantity both frames must agree on
unsupported_near_neighbors accelerating frames, general relativity, a boost whose Lorentz factor is not an exact rational, and motion off the shared axis
solution_soundness_status verified, the frames, the speed and the factor identity are checked before the relation is applied and the answer is checked against an invariant rather than against the relation that produced it
solution_completeness_status partial, one boost along one shared axis with an exact rational Lorentz factor
corpus_case_ids relativity_time_dilation_exact_gamma
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.relativity.plan fixture
rule physics.relativity.check-frames fixture
rule physics.relativity.check-boost fixture
rule physics.relativity.lorentz-factor fixture
rule physics.relativity.apply fixture
rule physics.relativity.check-invariant fixture

family id physics.relativity.length-contraction
topic_and_level Length contraction between two inertial frames in relative motion along a shared x axis, PRD section 9 PHYS-023
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds L = L0/gamma
accepted_input_forms one typed RelativityProblem naming both frames, the boost as a fraction of c and the proper length
domains_and_parameter_assumptions both frames are inertial, the boost is below the speed of light, 1 - beta^2 is the square of an exact fraction, and the proper length is positive
supported_branches_and_degenerate_cases a boost along either direction of the shared axis, and a refusal for a non-positive proper length, a speed at or above c, or a gamma with no exact rational value
exact_special_function_and_numerical_result_policy exact rational throughout, reporting nothing rather than a decimal when the Lorentz factor is not an exact rational
parser_module_ids src/units/units.cc, src/physics/relativity.cc
required_assumptions the frames and the rod's orientation and rest frame are stated rather than inferred, carried as "both frames are inertial, so no acceleration enters the transformation" and "the rod lies along the direction of motion and is at rest in the moving frame, so its length there is the proper length"
test_group_ids relativity
proof_obligation_ids obl.plan.preconditions-hold, obl.relativity.frames-distinct, obl.relativity.speed-below-light, obl.relativity.factor-identity, obl.physics.lookup-preserves-solutions, obl.relativity.invariant-holds
supported_methods check the two frames are declared and distinct, check the boost is below c, form the exact Lorentz factor, apply the contraction relation and check the result against a quantity both frames must agree on
unsupported_near_neighbors a rod across the direction of motion, accelerating frames, general relativity, and a boost whose Lorentz factor is not an exact rational
solution_soundness_status verified, the frames, the speed and the factor identity are checked before the relation is applied and the answer is checked against an invariant rather than against the relation that produced it
solution_completeness_status partial, one rod along one shared axis with an exact rational Lorentz factor
corpus_case_ids relativity_length_contraction_exact_gamma
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.relativity.plan fixture
rule physics.relativity.check-frames fixture
rule physics.relativity.check-boost fixture
rule physics.relativity.lorentz-factor fixture
rule physics.relativity.apply fixture
rule physics.relativity.check-invariant fixture

family id physics.relativity.lorentz-transformation
topic_and_level One event's coordinates in two inertial frames, PRD section 9 PHYS-023
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds the transformation pair
accepted_input_forms one typed RelativityProblem naming both frames, the boost as a fraction of c, and the event's position and time in the rest frame
domains_and_parameter_assumptions both frames are inertial, the axes are parallel and the origins coincide at t = 0, the boost is below the speed of light, and gamma is an exact rational
supported_branches_and_degenerate_cases an event ahead of or behind the origin, since a negative position is an ordinary coordinate rather than an unphysical value, and a refusal for a speed at or above c
exact_special_function_and_numerical_result_policy exact rational throughout, reporting nothing rather than a decimal when the Lorentz factor is not an exact rational
parser_module_ids src/units/units.cc, src/physics/relativity.cc
required_assumptions the frames and the coincident origins are stated rather than inferred, carried as "both frames are inertial, so no acceleration enters the transformation" and "the axes are parallel and the origins coincide at t = t' = 0, so the transformation carries no offset"
test_group_ids relativity
proof_obligation_ids obl.plan.preconditions-hold, obl.relativity.frames-distinct, obl.relativity.speed-below-light, obl.relativity.factor-identity, obl.physics.lookup-preserves-solutions, obl.relativity.invariant-holds
supported_methods check the two frames are declared and distinct, check the boost is below c, form the exact Lorentz factor, transform both coordinates and check the spacetime interval, which both frames must agree on
unsupported_near_neighbors a boost off the shared axis, a transformation carrying an origin offset, accelerating frames, and general relativity
solution_soundness_status verified, the transformed coordinates are checked against the invariant spacetime interval rather than against the transformation that produced them
solution_completeness_status partial, one event and one boost along one shared axis with an exact rational Lorentz factor
corpus_case_ids relativity_lorentz_transformation_event
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.relativity.plan fixture
rule physics.relativity.check-frames fixture
rule physics.relativity.check-boost fixture
rule physics.relativity.lorentz-factor fixture
rule physics.relativity.apply fixture
rule physics.relativity.check-invariant fixture

family id physics.relativity.velocity-addition
topic_and_level Relativistic velocity addition along a shared x axis, PRD section 9 PHYS-023
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds the addition relation
accepted_input_forms one typed RelativityProblem naming both frames, the boost as a fraction of c and the object's velocity in the moving frame
domains_and_parameter_assumptions both frames are inertial, the boost and the object's velocity are both below the speed of light, and the object moves along the shared axis
supported_branches_and_degenerate_cases velocities that would exceed c under a Galilean sum and do not under this one, and a refusal for an object given at or above the speed of light
exact_special_function_and_numerical_result_policy exact rational throughout, and this is the one relation here that needs no Lorentz factor, so an inexact gamma does not refuse it
parser_module_ids src/units/units.cc, src/physics/relativity.cc
required_assumptions the frames and the object's direction are stated rather than inferred, carried as "both frames are inertial, so no acceleration enters the transformation" and "the object moves along the same shared x axis the boost points along"
test_group_ids relativity
proof_obligation_ids obl.plan.preconditions-hold, obl.relativity.frames-distinct, obl.relativity.speed-below-light, obl.physics.lookup-preserves-solutions, obl.relativity.invariant-holds
supported_methods check the two frames are declared and distinct, check both speeds are below c, apply the addition relation and check the result against a quantity both frames must agree on
unsupported_near_neighbors an object off the shared axis, a massless object at c, accelerating frames, and general relativity
solution_soundness_status verified, both speeds are checked before the relation is applied and the answer is checked against an invariant rather than against the relation that produced it
solution_completeness_status partial, one object along one shared axis
corpus_case_ids relativity_velocity_addition_half_c
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
# No lorentz-factor rule here, because this relation does not read gamma, which is also why no
# factor-identity obligation appears above.
rule physics.relativity.plan fixture
rule physics.relativity.check-frames fixture
rule physics.relativity.check-boost fixture
rule physics.relativity.apply fixture
rule physics.relativity.check-invariant fixture

family id physics.relativity.energy-momentum
topic_and_level The total energy, momentum and kinetic energy of a moving particle, PRD section 9 PHYS-023
accepted_expression_grammar no expression grammar of its own, because the problem arrives as typed quantities and the engine builds E = gamma*E0 with pc = gamma*beta*E0
accepted_input_forms one typed RelativityProblem naming both frames, the boost as a fraction of c and the particle's rest energy in megaelectronvolts
domains_and_parameter_assumptions both frames are inertial, the boost is below the speed of light, gamma is an exact rational, and the rest energy is positive
supported_branches_and_degenerate_cases the total energy, the momentum energy and the kinetic energy reported together from one rest energy, and a refusal for a non-positive rest energy or a speed at or above c
exact_special_function_and_numerical_result_policy exact rational throughout in megaelectronvolts, because the SI value of an energy at this scale does not fit the exact rationals every step here compares with
parser_module_ids src/units/units.cc, src/physics/relativity.cc
required_assumptions the frames and the particle's rest frame are stated rather than inferred, carried as "both frames are inertial, so no acceleration enters the transformation" and "the particle is at rest in the moving frame, so it travels at the boost velocity in the rest frame"
test_group_ids relativity
proof_obligation_ids obl.plan.preconditions-hold, obl.relativity.frames-distinct, obl.relativity.speed-below-light, obl.relativity.factor-identity, obl.physics.lookup-preserves-solutions, obl.relativity.invariant-holds
supported_methods check the two frames are declared and distinct, check the boost is below c, form the exact Lorentz factor, report the total energy, the momentum energy and the kinetic energy, and check them against a quantity both frames must agree on
unsupported_near_neighbors a massless particle, a particle given by its mass rather than its rest energy, collisions and decays, and accelerating frames
solution_soundness_status verified, every reported energy has its own recorded equation on exact values and the answers are checked against an invariant
solution_completeness_status partial, one particle at one boost with an exact rational Lorentz factor
corpus_case_ids relativity_energy_momentum_proton
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.relativity.plan fixture
rule physics.relativity.check-frames fixture
rule physics.relativity.check-boost fixture
rule physics.relativity.lorentz-factor fixture
rule physics.relativity.apply fixture
rule physics.relativity.check-invariant fixture

family id physics.motion.position-vector
topic_and_level Velocity and acceleration read off a supplied position vector r(t), chapter 4 of the PHYS 2410 scope
family_envelope_version 1
accepted_expression_grammar the existing algebraic expression grammar for each component, evaluated in the time variable t
accepted_input_forms one typed PositionMotionProblem giving each component of r(t) as an expression in t, the declared rank, the interval bounds and the event time as time quantities
domains_and_parameter_assumptions each component is a function of t alone, the axes are independent and share one clock, and the interval has a non-zero duration
supported_branches_and_degenerate_cases rank two and rank three position vectors, a component that never moves, and the magnitude and direction of each reported vector when a backend is supplied to convert it
exact_special_function_and_numerical_result_policy exact rational evaluation of the secant over the interval and of both derivatives at the event, with directions left unreported when no backend can compute them
parser_module_ids src/core/parser.cc, src/physics/position_motion.cc, src/steps/differentiate.cc, src/physics/vector_components.cc
required_assumptions the one-variable component model and the shared clock are stated rather than inferred, carried as "each component of the position vector is a function of t alone" and "the axes are independent and share one clock"
test_group_ids position motion
proof_obligation_ids obl.plan.preconditions-hold, obl.calculus.rule-preserves-value
supported_methods differentiate each component twice in t with the existing differentiation engine, take the secant over the declared interval for the average velocity, evaluate both derivatives at the event time, and convert each reported vector to a magnitude and direction when a backend is supplied
unsupported_near_neighbors a component that is not differentiable by rule, motion given as a constant-acceleration equation table, more than one independent variable, and word-language input
solution_soundness_status verified by the composed differentiation engine, which checks each rule it applies; this family owns no rule of its own and its interval and dimension checks are refusals rather than recorded steps
solution_completeness_status partial, limited to the component expressions the composed differentiation engine supports
corpus_case_ids position_motion_vector_derivatives
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
# This family composes the differentiation engine for each component rather than carrying a second
# differentiator, so every rule id below is that family's own, recorded in this derivation.
rule calculus.differentiate.rules fixture
rule d.power fixture
rule d.constant-multiple fixture
rule d.variable fixture
rule d.constant fixture

family id physics.ranking.comparative-order
topic_and_level Ranking several situations by a quantity through ordered comparison criteria, chapter 2 of the PHYS 2410 scope
family_envelope_version 1
accepted_expression_grammar no expression grammar of its own, because a situation's standing on each criterion arrives as an exact rational rather than as an expression
accepted_input_forms one typed RankingModel naming the quantity and its criteria in priority order, and one RankingProblem giving each named situation's value for every criterion
domains_and_parameter_assumptions the criteria are given in priority order, every situation carries a value for each of them, and a value may be declared unknown
supported_branches_and_degenerate_cases a strict order, ties grouped into one tier, a later criterion deciding a pair the first one ties, and a refusal when the given facts do not decide a pair at all
exact_special_function_and_numerical_result_policy exact rational comparison only, with no arithmetic performed on the criterion values and nothing approximated
parser_module_ids src/physics/ranking.cc
required_assumptions the priority order and the completeness of the criterion values are stated rather than inferred, carried as "the criteria are given in priority order and every situation is measured on each of them"
test_group_ids ranking
proof_obligation_ids none, the two rules this family records raise no obligation of their own, because a comparison of exact rationals has nothing to prove beyond the comparison itself
supported_methods compare each pair on the criteria in priority order, the first untied criterion deciding the pair, record a justification step per criterion, then order the situations and group the ties into tiers
unsupported_near_neighbors combining criteria into a computed quantity, ranking by a value the problem does not give, and inferring a missing value from the others
solution_soundness_status partial, the order follows from exact rational comparisons and a pair the criteria cannot decide is refused rather than guessed, but nothing independent re-derives the published order
solution_completeness_status partial, the order is complete for situations whose criterion values are all known and is refused otherwise
corpus_case_ids ranking_average_speed_with_a_tie
device_performance_status not measured
direct_keypad_entry_status not implemented, typed API only
isolated_runtime_status not yet measured on the calculator
release_status unreleased
rule physics.ranking.criterion fixture
rule physics.ranking.order fixture

family id algebra.formula-rearrangement.single-occurrence
topic_and_level Rearranging a formula for one of its symbols, PRD section 9.4 ALG-007
accepted_input_forms an equation in the project's own grammar, and the symbol to isolate
accepted_expression_grammar sums, products, negations, first powers and reciprocals of the symbol
domains_and_parameter_assumptions a divisor that is not a literal number is carried as a stated restriction rather than assumed non-zero, and so is the other side of a reciprocal
supported_branches_and_degenerate_cases none, because every supported inverse has one real value
exact_special_function_and_numerical_result_policy exact throughout, no arithmetic is folded and no value is approximated
parser_module_ids src/core/parser.cc, src/steps/rearrange.cc, src/core/evaluate.cc
required_assumptions whatever the inverse operations introduce, recorded as it is met, as in "R is not zero" when the rearrangement divides by R
test_group_ids rearrange
proof_obligation_ids obl.rearrange.substitution-identity, obl.rearrange.same-solutions, obl.plan.preconditions-hold
supported_methods peel one operation at a time off the side holding the symbol, applying its inverse to both sides
unsupported_near_neighbors a symbol appearing more than once, an even power (two roots), any other root, a symbol inside a function, a symbol in an exponent, and inequalities
solution_soundness_status verified by rule-local equality invariants on every move, then checked by substituting the rearranged side back and evaluating both sides exactly, with Giac asked for a symbolic second opinion when a backend is supplied
solution_completeness_status partial, one occurrence of the symbol and the inverses listed above
corpus_case_ids the golden fixtures naming this family
explanation_review_status solved and refusal derivations covered by golden fixtures, independent explanation review not yet recorded
device_performance_status not measured
direct_keypad_entry_status native rearrange command and Ki V4 routing implemented, manual keypad qualification pending
isolated_runtime_status host core and native Lua bridge validated, calculator runtime not yet measured
release_status unreleased
rule alg.rearrange.inverse-operations fixture
rule alg.rearrange.swap-sides fixture
rule alg.rearrange.subtract-both-sides fixture
rule alg.rearrange.divide-both-sides fixture
rule alg.rearrange.negate-both-sides fixture
rule alg.rearrange.reciprocal-both-sides fixture
rule alg.rearrange.drop-unit-factor fixture
rule alg.rearrange.first-power fixture
rule alg.rearrange.check-by-substitution fixture

family id algebra.polynomial-rewrite.single-expression
topic_and_level Simplifying, expanding and factoring an expression, PRD section 9.4 ALG-001 and ALG-002
accepted_input_forms one expression in the project's own grammar, with the form to rewrite it into
accepted_expression_grammar sums, products, negations and whole-number powers over exact integers and symbols
domains_and_parameter_assumptions none, every rule here holds wherever the expression is defined
supported_branches_and_degenerate_cases an expression already in the asked-for form is reported as such rather than given an invented step, and a quadratic with no whole-number pair is left as it is
exact_special_function_and_numerical_result_policy exact integers and fractions only, and a decimal literal is refused rather than converted
parser_module_ids src/core/parser.cc, src/steps/rewrite.cc, src/core/evaluate.cc
required_assumptions none, the rules here are identities over exact integers and symbols, so no condition has to be carried
test_group_ids rewrite
proof_obligation_ids obl.rewrite.same-value, obl.alg.rule-preserves-value, obl.alg.fold-preserves-value, obl.alg.factor-multiplies-back, obl.plan.preconditions-hold
supported_methods work out one arithmetic operation at a time, drop a term of zero, write repeated factors as powers, gather terms differing only by a coefficient, distribute a product over a sum, take out a common factor, and factor a monic quadratic by product and sum
unsupported_near_neighbors decimals, a quadratic this rule cannot make monic, cubics and higher, rational expressions, radicals, and arithmetic wider than exact int64 rationals
solution_soundness_status verified, each factoring step multiplies its own answer back out and compares it term by term, and the finished expression is worked out against the original at six exact assignments with Giac asked as well when a backend is supplied
solution_completeness_status partial, a common factor and a monic quadratic
corpus_case_ids the golden fixtures naming this family
explanation_review_status rewritten and refusal derivations covered by golden fixtures, independent explanation review not yet recorded
device_performance_status not measured
direct_keypad_entry_status native simplify, expand and factor commands and Ki V4 routing implemented, manual keypad qualification pending
isolated_runtime_status host core and native Lua bridge validated, calculator runtime not yet measured
release_status unreleased
rule alg.simplify.fold-and-collect fixture
rule alg.expand.distribute-and-collect fixture
rule alg.factor.common-then-quadratic fixture
rule alg.fold-arithmetic fixture
rule alg.drop-zero-term fixture
rule alg.collect-like-terms fixture
rule alg.gather-powers fixture
rule alg.distribute fixture
rule alg.power-as-product fixture
rule alg.factor.common-factor fixture
rule alg.factor.product-and-sum fixture
rule alg.factor.difference-of-squares fixture
rule alg.rewrite.check-by-evaluation fixture

family id number.integer-method.literal
reference_curriculum_set_ids none, this family extends the menu walkthrough request beyond the MVP corpus minimums
curriculum_source_locations docs/menu-walkthrough-plan.md, Number and Probability menu inventory
topic_and_level Integer division, greatest common divisors, counting, primality, prime factorization and modular exponentiation
family_envelope_version 2
accepted_expression_grammar one top-level iquo, irem, factorial, perm, comb, is_prime, nextprime, ifactor or powmod call with nonnegative integer literal arguments, or gcd with signed integer literal arguments
accepted_input_forms ordinary menu commands in Exact mode, with every argument supplied
domains_and_parameter_assumptions iquo and irem accept 0 <= dividend <= 1000000000 and 1 <= divisor <= 1000000000. gcd accepts both arguments from -1000000000 through 1000000000. factorial accepts 0 through 100. perm and comb require 0 <= k <= n <= 100. is_prime accepts 0 through 1000000. ifactor accepts 2 through 1000000. nextprime accepts 0 through 999999 and searches at most 1000000. powmod requires base from 0 through 1000000000, exponent from 1 through 1000000 and modulus from 2 through 1000000000
supported_branches_and_degenerate_cases zero dividends, signed gcd with a nonnegative result and gcd(0,0)=0, empty counting products, composite and prime inputs, repeated prime factors and bounded next-prime searches. A search without a prime within the bound refuses a result
exact_special_function_and_numerical_result_policy exact GMP integer products including factorial(100). is_prime returns 2 for a proven prime and 0 otherwise. Decimal mode and approximate literals are refused
word_language_profile_ids none, mathematical command entry only
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/integer.cc
required_assumptions none, every argument and method bound is checked before recording a plan
test_group_ids integer, command
proof_obligation_ids obl.int.division-identity, obl.int.gcd-sign, obl.int.gcd-remainder, obl.int.gcd-certificate, obl.int.factorial-product, obl.int.permutation-product, obl.int.combination-product, obl.int.trial-division, obl.int.prime-decision, obl.int.next-prime, obl.int.modular-power, obl.int.factor-product, obl.plan.preconditions-hold
strategy_ids plan.integer-method
supported_methods quotient and remainder identity, GMP Euclidean division and checked Bezout certificate, consecutive products, exact binomial recurrence, trial division through the square root, consecutive candidate exclusion, prime factor reconstruction and binary modular exponentiation
unsupported_near_neighbors negative arguments except for gcd, symbolic or compound arguments, approximate literals, Decimal mode, inputs beyond the stated bounds, zero or negative modular exponents and modulus one. Tuple-valued iegcd and iabcuv and other integer menu commands remain pending
solution_soundness_status version 1 exact arithmetic checks and finite recurrence completion passed independent host review, including an external arithmetic oracle. Version 2 gcd passed independent host review and 934 signed, zero, boundary and randomized pairs against Python math.gcd. Physical qualification remains pending
solution_completeness_status bounded to the declared literal envelope and resource policy. The complete Number and Probability menus remain unfinished
corpus_case_ids integer_quotient, integer_remainder, integer_gcd, integer_gcd_zero, integer_gcd_refused, integer_factorial, integer_permutation, integer_combination, integer_prime, integer_composite, integer_next_prime, integer_modular_power, integer_factorization, integer_refused, integer_invalid, integer_step_budget, integer_cancelled
explanation_review_status version 1 semantic fixtures and host invariants passed independent review. Version 2 review and learner testing remain pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native command dispatcher implemented, physical keypad qualification pending
isolated_runtime_status host core and Lua bridge checked, physical execution and Giac compatibility qualification pending
capability_manifest_ids number.integer-method.literal
release_status in development, unreleased
rule plan.integer-method fixture
rule int.division fixture
rule int.factorial-product fixture
rule int.permutation-product fixture
rule int.combination-product fixture
rule int.trial-division fixture
rule int.prime-conclusion fixture
rule int.next-prime fixture
rule int.modular-power fixture
rule int.factor-product fixture
rule int.gcd-sign fixture
rule int.gcd-remainder fixture
rule int.gcd-conclusion fixture

family id matrix.ref.rational
reference_curriculum_set_ids none, this family extends the menu walkthrough request beyond the MVP corpus minimums
curriculum_source_locations docs/menu-walkthrough-plan.md, Matrix and Vector menu inventory
topic_and_level Exact rational matrices in row echelon form
family_envelope_version 1
accepted_expression_grammar one top-level ref call with a rectangular list of row lists. Cells use exact arithmetic over integer literals with sums, products, negation and integer powers, including reciprocal powers for division
accepted_input_forms ordinary ref menu command in Exact mode with one matrix argument
domains_and_parameter_assumptions one through four rows and one through six columns. Every input cell, intermediate matrix cell and row-operation coefficient must fit the shared exact Rational representation and checked arithmetic bounds. In-range input cells do not guarantee that a reduction stays within that envelope
supported_branches_and_degenerate_cases zero matrices, zero and dependent rows, negative and fractional cells, rectangular matrices and single-cell matrices. A matrix already in the requested form can complete without a transformation
exact_special_function_and_numerical_result_policy exact rational row operations and final matrix only. Decimal mode, decimal syntax, approximate containers or cells and symbolic or complex entries are refused
word_language_profile_ids none, mathematical command entry only
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/matrix.cc
required_assumptions none, matrix shape, exact provenance and arithmetic bounds are checked before recording a plan
test_group_ids matrix, matrix row, matrix form, command
proof_obligation_ids obl.plan.preconditions-hold, obl.matrix.row-equivalent, obl.matrix.trace-complete, obl.matrix.ref-form
strategy_ids plan.matrix-method
supported_methods actual Giac row swaps, nonzero row scaling and addition of a multiple of a distinct row. Exact certificates validate every changed and unchanged cell. A separate final check requires that leading nonzero entries move strictly right, zero rows are last and entries below each pivot are zero. Unit pivots are not required
unsupported_near_neighbors empty, flat, ragged or deeper lists, symbolic and approximate arithmetic, unsupported options or extra arguments, matrices beyond the stated dimensions and arithmetic outside checked bounds. Other matrix commands remain outside this family
solution_soundness_status exact row certificates, trace continuity and final-form checks pass native tests and the actual host Giac Lua bridge. Physical qualification remains pending
solution_completeness_status successful results require a complete verified trace from the input to the requested matrix form. This family does not solve a linear system or claim solution-set completeness. Cancellation and Meter limits retain the verified prefix without a final result. Arena failure discards unusable records
corpus_case_ids matrix_ref_scaled, matrix_ref_swap, matrix_ref_dependent, matrix_ref_zero
explanation_review_status semantic fixtures and Do, Write and Why bridge checks pass. Learner testing remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native command dispatch implemented, physical keypad qualification pending
isolated_runtime_status actual host Giac callback and native Lua bridge execution checked with calculator OS shims. Emulator and handheld qualification remain pending
capability_manifest_ids matrix.ref.rational
release_status in development, unreleased
rule plan.matrix-method fixture
rule matrix.row-swap fixture
rule matrix.row-scale fixture
rule matrix.row-add-multiple fixture
rule matrix.ref-conclusion fixture

family id matrix.rref.rational
reference_curriculum_set_ids none, this family extends the menu walkthrough request beyond the MVP corpus minimums
curriculum_source_locations docs/menu-walkthrough-plan.md, Matrix and Vector menu inventory
topic_and_level Exact rational matrices in reduced row echelon form
family_envelope_version 1
accepted_expression_grammar one top-level rref call with a rectangular list of row lists. Cells use exact arithmetic over integer literals with sums, products, negation and integer powers, including reciprocal powers for division
accepted_input_forms ordinary rref menu command in Exact mode with one matrix argument
domains_and_parameter_assumptions one through four rows and one through six columns. Every input cell, intermediate matrix cell and row-operation coefficient must fit the shared exact Rational representation and checked arithmetic bounds. In-range input cells do not guarantee that a reduction stays within that envelope
supported_branches_and_degenerate_cases zero matrices, zero and dependent rows, negative and fractional cells, rectangular matrices and single-cell matrices. A matrix already in the requested form can complete without a transformation
exact_special_function_and_numerical_result_policy exact rational row operations and final matrix only. Decimal mode, decimal syntax, approximate containers or cells and symbolic or complex entries are refused
word_language_profile_ids none, mathematical command entry only
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/matrix.cc
required_assumptions none, matrix shape, exact provenance and arithmetic bounds are checked before recording a plan
test_group_ids matrix, matrix row, matrix form, command
proof_obligation_ids obl.plan.preconditions-hold, obl.matrix.row-equivalent, obl.matrix.trace-complete, obl.matrix.rref-form
strategy_ids plan.matrix-method
supported_methods actual Giac row swaps, nonzero row scaling and addition of a multiple of a distinct row. Exact certificates validate every changed and unchanged cell. A separate final check requires that leading entries move strictly right, zero rows are last, every pivot equals one and all other entries in each pivot column are zero
unsupported_near_neighbors empty, flat, ragged or deeper lists, symbolic and approximate arithmetic, unsupported options or extra arguments, matrices beyond the stated dimensions and arithmetic outside checked bounds. Other matrix commands remain outside this family
solution_soundness_status exact row certificates, trace continuity and final-form checks pass native tests and the actual host Giac Lua bridge. Physical qualification remains pending
solution_completeness_status successful results require a complete verified trace from the input to the requested matrix form. This family does not solve a linear system or claim solution-set completeness. Cancellation and Meter limits retain the verified prefix without a final result. Arena failure discards unusable records
corpus_case_ids matrix_rref_scaled, matrix_rref_swap, matrix_rref_fraction, matrix_rref_identity, matrix_rref_form_refused
explanation_review_status semantic fixtures and Do, Write and Why bridge checks pass. Learner testing remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native command dispatch implemented, physical keypad qualification pending
isolated_runtime_status actual host Giac callback and native Lua bridge execution checked with calculator OS shims. Emulator and handheld qualification remain pending
capability_manifest_ids matrix.rref.rational
release_status in development, unreleased
rule plan.matrix-method fixture
rule matrix.row-swap fixture
rule matrix.row-scale fixture
rule matrix.row-add-multiple fixture
rule matrix.rref-conclusion fixture

family id matrix.det.rational
reference_curriculum_set_ids none, this family extends the menu walkthrough request beyond the MVP corpus minimums
curriculum_source_locations docs/menu-walkthrough-plan.md, Matrix and Vector menu inventory
topic_and_level Exact rational determinants by recorded row reduction
family_envelope_version 1
accepted_expression_grammar one top-level det call with a square list of row lists. Cells use exact arithmetic over integer literals with sums, products, negation and integer powers, including reciprocal powers for division
accepted_input_forms ordinary det menu command in Exact mode with one matrix argument
domains_and_parameter_assumptions square matrices of order one through four. Every input cell, intermediate matrix cell and row-operation coefficient must fit the shared exact Rational representation and checked arithmetic bounds. The GMP determinant factor, diagonal product and scalar answer have a separate 4096-bit numerator and denominator limit
supported_branches_and_degenerate_cases zero and singular matrices, dependent and duplicate rows, negative and fractional cells, single-cell matrices, row swaps and exact scalar answers beyond int64. Unchanged matrix events do not alter the retained determinant factor
exact_special_function_and_numerical_result_policy exact rational scalar result only. Decimal mode, decimal syntax, approximate containers or cells and symbolic or complex entries are refused
word_language_profile_ids none, mathematical command entry only
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/matrix.cc
required_assumptions none, square shape, exact provenance and arithmetic bounds are checked before recording a plan
test_group_ids matrix, matrix row, matrix form, command
proof_obligation_ids obl.plan.preconditions-hold, obl.matrix.row-equivalent, obl.matrix.det-factor, obl.matrix.trace-complete, obl.matrix.ref-form, obl.matrix.det-diagonal-product, obl.matrix.det-correction
strategy_ids plan.matrix-determinant
supported_methods actual Giac row swaps, nonzero row scaling and row addition with independently checked matrix transitions. Each changing event records its determinant effect. Exact echelon verification precedes the diagonal product. Dividing by the accumulated nonzero factor recovers the original determinant
unsupported_near_neighbors empty, flat, ragged, rectangular or deeper lists, symbolic and approximate arithmetic, unsupported options or extra arguments, order above four, intermediate cells outside checked Rational bounds and determinant arithmetic beyond its bit limit. Other matrix commands remain outside this family
solution_soundness_status exact row, determinant factor, diagonal and correction certificates pass native and actual host Giac tests. A bounded GMP permutation oracle supplies independent expected values. Physical qualification remains pending
solution_completeness_status successful results require a complete verified matrix trace and exact scalar correction. Cancellation and Meter limits preserve the verified prefix without a final determinant. Arena failure discards unusable records
corpus_case_ids matrix_det_scalar, matrix_det_swap, matrix_det_fraction, matrix_det_singular, matrix_det_identity, matrix_det_large
explanation_review_status semantic fixtures and actual native Do, Write and Why UI checks cover determinant effects and the final correction. Learner testing remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native menu dispatch implemented, physical keypad qualification pending
isolated_runtime_status actual host Giac callback and native Lua bridge execution checked with calculator OS shims. Emulator and handheld determinant qualification remain pending
capability_manifest_ids matrix.det.rational
release_status in development, unreleased
rule plan.matrix-determinant fixture
rule matrix.det-row-swap fixture
rule matrix.det-row-scale fixture
rule matrix.det-row-add-multiple fixture
rule matrix.det-diagonal-product fixture
rule matrix.det-correction fixture

family id calculus.tangent-line.single-variable
topic_and_level Tangent lines to a supported expression at a rational point, PRD section 9 CALC-010
family_envelope_version 1
accepted_expression_grammar the expressions the native differentiation engine supports in one variable, over numbers and that variable, with a typed decimal read as the fraction it names and refused in an exponent
accepted_input_forms tangent(expression,variable,point), with the point an exact rational
domains_and_parameter_assumptions the expression and its derivative both have an exact rational value at the point. Domain restrictions the differentiation rules raise are recorded on the steps that introduce them
supported_branches_and_degenerate_cases a point where the function and the derivative are exact, including a constant expression whose tangent is the horizontal line through it
exact_special_function_and_numerical_result_policy exact rationals over int64, refusing rather than wrapping or approximating the point value or the slope
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/calculus.cc, src/steps/differentiate.cc
required_assumptions none carried for the family, a condition a form needs is recorded as a domain restriction on the step that introduces it
test_group_ids calculus, context
supported_methods evaluate the function at the point, differentiate by the registered rules, evaluate the derivative at the point, and assemble the point-slope line
unsupported_near_neighbors normal lines, tangents at a symbolic point, tangents at an irrational point, secant lines, implicit and higher-order tangency, and forms the differentiation engine has no rule for
proof_obligation_ids obl.calculus.rule-preserves-value, obl.calculus.tangent-agreement, obl.plan.preconditions-hold
solution_soundness_status the assembled line is read back exactly at the point and one unit away, which pins its value and its slope and withholds the result when either disagrees
solution_completeness_status partial, the expressions the differentiation engine covers at a rational point and no others
corpus_case_ids tangent_line
explanation_review_status semantic fixtures recorded. Independent final review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status command and menu template implemented. Handheld qualification pending
isolated_runtime_status unqualified, emulator and physical-device qualification pending
capability_manifest_ids calculus.tangent-line.single-variable
release_status in development, unreleased
rule tangent.point-value fixture
rule calculus.differentiate.rules fixture
rule d.power fixture
rule tangent.slope fixture
rule tangent.line fixture
rule tangent.check-line fixture

family id calculus.linearization.single-variable
topic_and_level Local linear approximation of a supported expression at a rational point, PRD section 9 CALC-010
family_envelope_version 1
accepted_expression_grammar the expressions the native differentiation engine supports in one variable, over numbers and that variable, with a typed decimal read as the fraction it names and refused in an exponent
accepted_input_forms linearize(expression,variable,point), with the point an exact rational
domains_and_parameter_assumptions the expression and its derivative both have an exact rational value at the point. Domain restrictions the differentiation rules raise are recorded on the steps that introduce them
supported_branches_and_degenerate_cases a point where the function and the derivative are exact, including a constant expression whose linearization is that constant
exact_special_function_and_numerical_result_policy exact rationals over int64, refusing rather than wrapping. The answer is stated as an approximation near the point rather than as an equality
parser_module_ids src/core/parser.cc, src/steps/command.cc, src/steps/calculus.cc, src/steps/differentiate.cc
required_assumptions none carried for the family, a condition a form needs is recorded as a domain restriction on the step that introduces it
test_group_ids calculus, context
supported_methods evaluate the function at the point, differentiate by the registered rules, evaluate the derivative at the point, and state the point-slope line as the local approximation
unsupported_near_neighbors an error bound on the approximation, higher-order Taylor approximations, differentials quoted as a change in the function, a symbolic or irrational point, and forms the differentiation engine has no rule for
proof_obligation_ids obl.calculus.rule-preserves-value, obl.calculus.tangent-agreement, obl.plan.preconditions-hold
solution_soundness_status the assembled line is read back exactly at the point and one unit away, which pins its value and its slope and withholds the result when either disagrees. The approximation claim itself is stated rather than proved away from the point
solution_completeness_status partial, the expressions the differentiation engine covers at a rational point and no others
corpus_case_ids tangent_linearization
explanation_review_status semantic fixtures recorded. Independent final review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status command and menu template implemented. Handheld qualification pending
isolated_runtime_status unqualified, emulator and physical-device qualification pending
capability_manifest_ids calculus.linearization.single-variable
release_status in development, unreleased
rule tangent.point-value fixture
rule calculus.differentiate.rules fixture
rule d.power fixture
rule tangent.slope fixture
rule tangent.linearization fixture
rule tangent.check-line fixture

family id physics.optics.refraction.snell
reference_curriculum_set_ids none, PHYS-022 names these five relations directly rather than through a curriculum set
curriculum_source_locations docs/StepCAS_Product_Requirements_Document.md, PRD section 9 PHYS-022
topic_and_level Refraction and total internal reflection through n1*sin(t1) = n2*sin(t2), PRD section 9 PHYS-022
family_envelope_version 1
accepted_expression_grammar existing quantity grammar for each numeric field, with the unit supplied by the variable rather than typed. An angle is supplied as its sine, because the exact engine has no transcendental arithmetic
accepted_input_forms one typed unknown chosen from n1, sin(t1), n2 and sin(t2), with the other three supplied as distinct typed knowns
domains_and_parameter_assumptions each refractive index is at least one and dimensionless, each sine lies in [-1, 1] and is dimensionless, and no quantity repeats
supported_branches_and_degenerate_cases any one of the four may be unknown. Entering a denser medium, leaving a denser one, and equal indices leaving the ray undeviated. An incident sine past the critical sine concludes total internal reflection with the critical sine reported, which is an answer rather than a refusal. Reflection is the same-medium case sin(t2) = sin(t1)
exact_special_function_and_numerical_result_policy exact rational arithmetic over the supplied sines with no trigonometric evaluation, and measured precision applied only after candidate verification
word_language_profile_ids none, typed variable and quantity entry only
parser_module_ids src/units/units.cc, src/physics/optics.cc, src/core/parser.cc
required_assumptions the light is monochromatic in a homogeneous medium and the answer is read under the relation's declared convention, carried as "the medium is homogeneous and the light is monochromatic" and "angles are measured from the normal, each sine is a non-negative ratio, and the reflected ray leaves at the incident angle"
test_group_ids optics, units, golden
proof_obligation_ids obl.plan.preconditions-hold, obl.optics.dimensions-agree, obl.optics.domain-holds, obl.optics.convention-declared, obl.physics.lookup-preserves-solutions, obl.optics.critical-angle, obl.optics.candidate-satisfies
strategy_ids physics.optics.refraction.snell
supported_methods check dimensions, check the stated domain, declare the sign convention, substitute exactly into Snell's law, compare the incident sine against the critical sine n2/n1, and verify the candidate by substitution
unsupported_near_neighbors angles in degrees or radians, Fresnel reflection and transmission coefficients, polarization and the Brewster angle, dispersion and wavelength-dependent indices, prism deviation, and the other four PHYS-022 relations
solution_soundness_status verified by dimensional analysis, the stated domain, and exact substitution of the candidate back into the law. Total internal reflection is proved from the critical sine rather than inferred from a failed solve
solution_completeness_status complete for one unknown and three compatible knowns when the unique exact rational answer fits, with total internal reflection reported in place of a transmitted sine
corpus_case_ids not yet filed, the two golden fixtures optics_refraction_transmitted_sine and optics_refraction_total_internal record this family
explanation_review_status semantic golden fixtures recorded for this relation. Independent learner review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status host and native Lua bridge execution checked. Emulator and handheld qualification remain pending
capability_manifest_ids physics.optics.refraction.snell
release_status in development, unreleased
rule physics.optics.refraction.snell fixture
rule physics.optics.check-dimensions fixture
rule physics.optics.check-domain fixture
rule physics.optics.sign-convention fixture
rule physics.optics.substitute fixture
rule physics.optics.total-internal-reflection fixture
rule physics.optics.check-candidate fixture

family id physics.optics.thin-lens.image
reference_curriculum_set_ids none, PHYS-022 names these five relations directly rather than through a curriculum set
curriculum_source_locations docs/StepCAS_Product_Requirements_Document.md, PRD section 9 PHYS-022
topic_and_level Thin lens imaging through 1/do + 1/di = 1/f with its lateral magnification, PRD section 9 PHYS-022
family_envelope_version 1
accepted_expression_grammar existing quantity grammar for each numeric field, with the unit supplied by the variable rather than typed. An angle is supplied as its sine, because the exact engine has no transcendental arithmetic
accepted_input_forms one typed unknown chosen from f, do and di, with the other two supplied as distinct typed knowns in any length unit
domains_and_parameter_assumptions every distance carries the dimension of length and is nonzero, and no quantity repeats. The two given reciprocals must not cancel, which is what leaves the third undetermined
supported_branches_and_degenerate_cases any one of the three may be unknown. Real and virtual images, converging and diverging lenses, and mixed length units converted exactly to SI. Two distances whose reciprocals sum to zero are reported as indeterminate rather than divided by zero
exact_special_function_and_numerical_result_policy exact rational SI conversion and reciprocal arithmetic, the answer reported in metres, and measured precision applied only after candidate verification
word_language_profile_ids none, typed variable and quantity entry only
parser_module_ids src/units/units.cc, src/physics/optics.cc, src/core/parser.cc
required_assumptions the light is monochromatic in a homogeneous medium and the answer is read under the relation's declared convention, carried as "the medium is homogeneous and the light is monochromatic" and "distances are positive on the real side, so a real object has do > 0, a real image has di > 0, a converging lens has f > 0, and a virtual image gives di < 0"
test_group_ids optics, units, golden
proof_obligation_ids obl.plan.preconditions-hold, obl.optics.dimensions-agree, obl.physics.scale-preserves-solutions, obl.optics.domain-holds, obl.optics.convention-declared, obl.physics.lookup-preserves-solutions, obl.optics.candidate-satisfies
strategy_ids physics.optics.thin-lens.image
supported_methods check dimensions, convert exactly to SI, check the stated domain, declare the sign convention, substitute exactly into the thin lens equation, verify the candidate, and report the lateral magnification -di/do
unsupported_near_neighbors thick lenses and lens makers formula, multiple-element systems and their combined focal length, aberration, ray-diagram construction, angular magnification of an eyepiece, the spherical mirror relation, and the other PHYS-022 relations
solution_soundness_status verified by dimensional analysis, the stated domain, and exact substitution of the candidate back into the lens equation. The magnification is computed from the verified distances rather than reported alongside an unverified one
solution_completeness_status complete for one unknown and two compatible knowns when the unique exact rational answer fits, refusing the indeterminate case rather than reporting it
corpus_case_ids not yet filed, the golden fixture optics_thin_lens_real_image records this family
explanation_review_status semantic golden fixtures recorded for this relation. Independent learner review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native Lua bridge and Ki V4 menu template implemented. Physical keypad qualification pending
isolated_runtime_status host and native Lua bridge execution checked. Emulator and handheld qualification remain pending
capability_manifest_ids physics.optics.thin-lens.image
release_status in development, unreleased
rule physics.optics.thin-lens.image fixture
rule physics.optics.check-dimensions fixture
rule physics.optics.convert-units fixture
rule physics.optics.check-domain fixture
rule physics.optics.sign-convention fixture
rule physics.optics.substitute fixture
rule physics.optics.check-candidate fixture

family id physics.optics.spherical-mirror.image
reference_curriculum_set_ids none, PHYS-022 names these five relations directly rather than through a curriculum set
curriculum_source_locations docs/StepCAS_Product_Requirements_Document.md, PRD section 9 PHYS-022
topic_and_level Spherical mirror imaging through 1/do + 1/di = 1/f with f = R/2, PRD section 9 PHYS-022
family_envelope_version 1
accepted_expression_grammar existing quantity grammar for each numeric field, with the unit supplied by the variable rather than typed. An angle is supplied as its sine, because the exact engine has no transcendental arithmetic
accepted_input_forms one typed unknown chosen from f, do and di, with the other two supplied as distinct typed knowns in any length unit
domains_and_parameter_assumptions every distance carries the dimension of length and is nonzero, and no quantity repeats. The two given reciprocals must not cancel, which is what leaves the third undetermined
supported_branches_and_degenerate_cases any one of the three may be unknown. Real and virtual images, concave and convex mirrors, and mixed length units converted exactly to SI. Two distances whose reciprocals sum to zero are reported as indeterminate rather than divided by zero
exact_special_function_and_numerical_result_policy exact rational SI conversion and reciprocal arithmetic, the answer reported in metres, and measured precision applied only after candidate verification
word_language_profile_ids none, typed variable and quantity entry only
parser_module_ids src/units/units.cc, src/physics/optics.cc, src/core/parser.cc
required_assumptions the light is monochromatic in a homogeneous medium and the answer is read under the relation's declared convention, carried as "the medium is homogeneous and the light is monochromatic" and "distances are positive in front of the mirror, so a real object has do > 0, a real image has di > 0, a concave mirror has f = R/2 > 0, and a virtual image gives di < 0"
test_group_ids optics, units, golden
proof_obligation_ids obl.plan.preconditions-hold, obl.optics.dimensions-agree, obl.physics.scale-preserves-solutions, obl.optics.domain-holds, obl.optics.convention-declared, obl.physics.lookup-preserves-solutions, obl.optics.candidate-satisfies
strategy_ids physics.optics.spherical-mirror.image
supported_methods check dimensions, convert exactly to SI, check the stated domain, declare the sign convention, substitute exactly into the mirror equation, verify the candidate, and report the lateral magnification -di/do
unsupported_near_neighbors the radius of curvature entered in place of the focal length, spherical aberration and the paraxial limit, plane mirrors, ray-diagram construction, the thin lens relation, and the other PHYS-022 relations
solution_soundness_status verified by dimensional analysis, the stated domain, and exact substitution of the candidate back into the mirror equation. The magnification is computed from the verified distances rather than reported alongside an unverified one
solution_completeness_status complete for one unknown and two compatible knowns when the unique exact rational answer fits, refusing the indeterminate case rather than reporting it
corpus_case_ids not yet filed, the golden fixture optics_spherical_mirror_image records this family
explanation_review_status semantic golden fixtures recorded for this relation. Independent learner review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status host and native Lua bridge execution checked. Emulator and handheld qualification remain pending
capability_manifest_ids physics.optics.spherical-mirror.image
release_status in development, unreleased
rule physics.optics.spherical-mirror.image fixture
rule physics.optics.check-dimensions fixture
rule physics.optics.convert-units fixture
rule physics.optics.check-domain fixture
rule physics.optics.sign-convention fixture
rule physics.optics.substitute fixture
rule physics.optics.check-candidate fixture

family id physics.optics.double-slit.maxima
reference_curriculum_set_ids none, PHYS-022 names these five relations directly rather than through a curriculum set
curriculum_source_locations docs/StepCAS_Product_Requirements_Document.md, PRD section 9 PHYS-022
topic_and_level Two-slit interference bright fringes through d*sin(t) = m*lambda, PRD section 9 PHYS-022
family_envelope_version 1
accepted_expression_grammar existing quantity grammar for each numeric field, with the unit supplied by the variable rather than typed. An angle is supplied as its sine, because the exact engine has no transcendental arithmetic
accepted_input_forms one typed unknown chosen from d, sin(t), m and lambda, with the other three supplied as distinct typed knowns
domains_and_parameter_assumptions the slit spacing and the wavelength carry the dimension of length and are strictly positive, the fringe sine lies in [-1, 1] and is dimensionless, and the fringe order is a dimensionless integer
supported_branches_and_degenerate_cases any one of the four may be unknown. The central maximum at m = 0, higher orders, and mixed length units converted exactly to SI. An isolated order between two integers is refused as unphysical rather than rounded
exact_special_function_and_numerical_result_policy exact rational SI conversion and arithmetic over the supplied sine with no trigonometric evaluation, and measured precision applied only after candidate verification
word_language_profile_ids none, typed variable and quantity entry only
parser_module_ids src/units/units.cc, src/physics/optics.cc, src/core/parser.cc
required_assumptions the light is monochromatic in a homogeneous medium and the answer is read under the relation's declared convention, carried as "the medium is homogeneous and the light is monochromatic" and "the order m counts bright fringes outward from the central maximum, so m is 0, 1, 2 and up"
test_group_ids optics, units, golden
proof_obligation_ids obl.plan.preconditions-hold, obl.optics.dimensions-agree, obl.physics.scale-preserves-solutions, obl.optics.domain-holds, obl.optics.convention-declared, obl.physics.lookup-preserves-solutions, obl.optics.candidate-satisfies, obl.physics.reported-within-half-place
strategy_ids physics.optics.double-slit.maxima
supported_methods check dimensions, convert exactly to SI, check the stated domain, declare the order convention, substitute exactly into d*sin(t) = m*lambda, and verify the candidate by substitution
unsupported_near_neighbors fringe spacing on a screen at a stated distance, intensity distribution and the cosine-squared profile, the small-angle approximation, dark fringes at half-integer orders, the single-slit envelope and missing orders, diffraction gratings, and the other PHYS-022 relations
solution_soundness_status verified by dimensional analysis, the stated domain including the integrality of the order, and exact substitution of the candidate back into the fringe condition
solution_completeness_status complete for one unknown and three compatible knowns when the unique exact rational answer fits
corpus_case_ids not yet filed, the golden fixture optics_double_slit_wavelength records this family
explanation_review_status semantic golden fixtures recorded for this relation. Independent learner review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status host and native Lua bridge execution checked. Emulator and handheld qualification remain pending
capability_manifest_ids physics.optics.double-slit.maxima
release_status in development, unreleased
rule physics.optics.double-slit.maxima fixture
rule physics.optics.check-dimensions fixture
rule physics.optics.convert-units fixture
rule physics.optics.check-domain fixture
rule physics.optics.sign-convention fixture
rule physics.optics.substitute fixture
rule physics.optics.check-candidate fixture
rule physics.optics.significant-figures fixture

family id physics.optics.single-slit.minima
reference_curriculum_set_ids none, PHYS-022 names these five relations directly rather than through a curriculum set
curriculum_source_locations docs/StepCAS_Product_Requirements_Document.md, PRD section 9 PHYS-022
topic_and_level Single-slit diffraction minima through a*sin(t) = m*lambda, PRD section 9 PHYS-022
family_envelope_version 1
accepted_expression_grammar existing quantity grammar for each numeric field, with the unit supplied by the variable rather than typed. An angle is supplied as its sine, because the exact engine has no transcendental arithmetic
accepted_input_forms one typed unknown chosen from a, sin(t), m and lambda, with the other three supplied as distinct typed knowns
domains_and_parameter_assumptions the slit width and the wavelength carry the dimension of length and are strictly positive, the fringe sine lies in [-1, 1] and is dimensionless, and the order is a dimensionless integer counted from one, since m = 0 is the central maximum rather than a minimum
supported_branches_and_degenerate_cases any one of the four may be unknown. The first and higher minima, and mixed length units converted exactly to SI. An isolated order between two integers is refused as unphysical rather than rounded
exact_special_function_and_numerical_result_policy exact rational SI conversion and arithmetic over the supplied sine with no trigonometric evaluation, and measured precision applied only after candidate verification
word_language_profile_ids none, typed variable and quantity entry only
parser_module_ids src/units/units.cc, src/physics/optics.cc, src/core/parser.cc
required_assumptions the light is monochromatic in a homogeneous medium and the answer is read under the relation's declared convention, carried as "the medium is homogeneous and the light is monochromatic" and "the order m counts diffraction minima outward, so m is 1, 2, 3 and up, and m = 0 is the central maximum rather than a minimum"
test_group_ids optics, units, golden
proof_obligation_ids obl.plan.preconditions-hold, obl.optics.dimensions-agree, obl.physics.scale-preserves-solutions, obl.optics.domain-holds, obl.optics.convention-declared, obl.physics.lookup-preserves-solutions, obl.optics.candidate-satisfies
strategy_ids physics.optics.single-slit.minima
supported_methods check dimensions, convert exactly to SI, check the stated domain, declare the order convention, substitute exactly into a*sin(t) = m*lambda, and verify the candidate by substitution
unsupported_near_neighbors the central maximum width on a screen at a stated distance, the intensity envelope and its sinc-squared profile, secondary maxima, circular apertures and the Rayleigh criterion, resolving power, the two-slit maxima relation, and the other PHYS-022 relations
solution_soundness_status verified by dimensional analysis, the stated domain including the integrality of the order, and exact substitution of the candidate back into the minimum condition
solution_completeness_status complete for one unknown and three compatible knowns when the unique exact rational answer fits
corpus_case_ids not yet filed, the golden fixture optics_single_slit_minimum records this family
explanation_review_status semantic golden fixtures recorded for this relation. Independent learner review remains pending
learner_transfer_status not measured
device_performance_status not measured
direct_keypad_entry_status native Lua bridge implemented, guided keypad entry not yet implemented
isolated_runtime_status host and native Lua bridge execution checked. Emulator and handheld qualification remain pending
capability_manifest_ids physics.optics.single-slit.minima
release_status in development, unreleased
rule physics.optics.single-slit.minima fixture
rule physics.optics.check-dimensions fixture
rule physics.optics.convert-units fixture
rule physics.optics.check-domain fixture
rule physics.optics.sign-convention fixture
rule physics.optics.substitute fixture
rule physics.optics.check-candidate fixture
