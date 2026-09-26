# Each tool selftest stages files in a scratch directory of its own. Run against an empty root, the
# root has to be empty again afterwards, because a selftest that leaves its directory behind fills
# the disk one run at a time and nothing else would notice.

foreach(required SCRATCH SELFTESTS)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} was not supplied")
    endif()
endforeach()

file(REMOVE_RECURSE "${SCRATCH}")
file(MAKE_DIRECTORY "${SCRATCH}")
set(ENV{NPS_SCRATCH_DIR} "${SCRATCH}")

set(failures "")
foreach(selftest IN LISTS SELFTESTS)
    execute_process(COMMAND "${selftest}" --selftest
                    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
    if(NOT status EQUAL 0)
        list(APPEND failures "${selftest} --selftest failed with ${status}")
    endif()
    file(GLOB left LIST_DIRECTORIES true "${SCRATCH}/*")
    if(left)
        list(APPEND failures "${selftest} --selftest left ${left}")
        file(REMOVE_RECURSE ${left})
    endif()
endforeach()

# An empty root proves nothing about a selftest that stages somewhere else, so each one is also run
# with the root pointing at a plain file, where no directory can be made, and has to fail there.
file(REMOVE_RECURSE "${SCRATCH}")
file(WRITE "${SCRATCH}" "a file where a directory is needed\n")
foreach(selftest IN LISTS SELFTESTS)
    execute_process(COMMAND "${selftest}" --selftest
                    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
    if(status EQUAL 0)
        list(APPEND failures "${selftest} --selftest passed without the scratch root it was given")
    endif()
endforeach()

file(REMOVE_RECURSE "${SCRATCH}")
if(failures)
    string(REPLACE ";" "\n" report "${failures}")
    message(FATAL_ERROR "tool selftests did not clean up:\n${report}")
endif()
message(STATUS "every tool selftest removed its scratch directories")
