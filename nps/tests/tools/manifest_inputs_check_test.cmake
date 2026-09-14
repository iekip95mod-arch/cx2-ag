# The live manifest_inputs check reads whatever this configure recorded, so its own branches are only
# exercised by whichever tree it runs in. This drives check-manifest-inputs.cmake over written lists
# instead, so the untracked failure and each of the two deferral guards is watched failing here.

foreach(required CHECKER SOURCE_DIR WORK_DIR)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} was not supplied")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK_DIR}")
file(MAKE_DIRECTORY "${WORK_DIR}")

set(tracked_input "${SOURCE_DIR}/CMakeLists.txt")
set(build_product "${SOURCE_DIR}/tools/no-such-build-product.a")
set(failures "")

# An input can live in a checkout other than the one being configured, which is what the committed
# baseline procedure does when it points NDL_SDK at the original tree. Trackedness is a property of
# the repository holding the input, so a second checkout standing in for that SDK is how this test
# tells a cross-checkout source from a build product without needing a real second clone.
find_program(GIT_EXECUTABLE git)
if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git is what tells a tracked source from a build product, and it is missing")
endif()
set(elsewhere "${WORK_DIR}/other-checkout")
file(MAKE_DIRECTORY "${elsewhere}")
execute_process(COMMAND "${GIT_EXECUTABLE}" init --quiet "${elsewhere}"
                RESULT_VARIABLE git_status OUTPUT_QUIET ERROR_QUIET)
if(NOT git_status EQUAL 0)
    message(FATAL_ERROR "could not make a second checkout at ${elsewhere}")
endif()
set(elsewhere_tracked "${elsewhere}/tracked-source.c")
set(elsewhere_product "${elsewhere}/built-product.a")
file(WRITE "${elsewhere_tracked}" "int main(void) { return 0; }\n")
file(WRITE "${elsewhere_product}" "not a source\n")
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${elsewhere}" add -- "${elsewhere_tracked}"
                RESULT_VARIABLE git_status OUTPUT_QUIET ERROR_QUIET)
if(NOT git_status EQUAL 0)
    message(FATAL_ERROR "could not track ${elsewhere_tracked}")
endif()

function(nps_expect name expect_pass expect_text inputs deferred)
    string(REPLACE ";" "\n" input_lines "${inputs}")
    string(REPLACE ";" "\n" deferred_lines "${deferred}")
    file(WRITE "${WORK_DIR}/${name}-inputs.txt" "${input_lines}\n")
    file(WRITE "${WORK_DIR}/${name}-deferred.txt" "${deferred_lines}\n")
    execute_process(COMMAND "${CMAKE_COMMAND}"
                    "-DINPUTS=${WORK_DIR}/${name}-inputs.txt"
                    "-DDEFERRED=${WORK_DIR}/${name}-deferred.txt"
                    "-DSOURCE_DIR=${SOURCE_DIR}"
                    -P "${CHECKER}"
                    RESULT_VARIABLE status ERROR_VARIABLE diagnostics OUTPUT_VARIABLE report)
    if(expect_pass AND NOT status EQUAL 0)
        list(APPEND failures "${name}: expected a pass, got ${status}\n${diagnostics}")
    elseif(NOT expect_pass AND status EQUAL 0)
        list(APPEND failures "${name}: expected a failure, got a pass")
    elseif(NOT expect_pass AND NOT "${diagnostics}" MATCHES "${expect_text}")
        list(APPEND failures "${name}: failure did not say ${expect_text}\n${diagnostics}")
    endif()
    set(failures "${failures}" PARENT_SCOPE)
endfunction()

# A build product hashed into a manifest fails, which is the rule the check exists for.
nps_expect(untracked OFF "does not track" "${tracked_input};${build_product}" "")

# The same input passes once the hash that recorded it declares the deferral issue 222 owns.
nps_expect(deferred ON "" "${tracked_input};${build_product}" "${build_product}")

# A deferral no hash recorded is stale, so the exemption cannot outlive its input.
nps_expect(unrecorded OFF "no manifest hash recorded"
           "${tracked_input};${build_product}" "${build_product};${SOURCE_DIR}/tools/tidy.sh")

# A deferral set that is tracked all through covers nothing, which is how landing 222 removes it.
nps_expect(obsolete OFF "drop the deferral"
           "${tracked_input};${SOURCE_DIR}/tools/tidy.sh" "${tracked_input}")

# A deferral beside a tracked input of the same hash stays legitimate while a build product remains.
nps_expect(mixed ON "" "${tracked_input};${build_product}" "${tracked_input};${build_product}")

# An input the other checkout tracks is a source, however the configured tree would answer for it.
nps_expect(elsewhere_tracked ON "" "${tracked_input};${elsewhere_tracked}" "")

# And the same reach does not excuse a build product sitting beside it in that other checkout.
nps_expect(elsewhere_untracked OFF "does not track" "${tracked_input};${elsewhere_product}" "")

if(failures)
    string(REPLACE ";" "\n" report "${failures}")
    message(FATAL_ERROR "manifest input check behaved wrongly:\n${report}")
endif()
message(STATUS "manifest input check rejects build products and retires its own deferrals")
