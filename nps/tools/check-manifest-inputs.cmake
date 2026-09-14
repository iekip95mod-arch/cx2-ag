# Every file a host configure hashes into a capability manifest has to be one the repository tracks.
# A build product hashed there makes the manifest id depend on whether somebody had already built
# the tree, so a fresh checkout configures differently from a warm one, and the fresh one loses:
# configure refuses an input that does not exist yet. Reads the list the configure recorded, so
# adding an input to a hash brings it under this check without a second edit here.

if(NOT DEFINED INPUTS)
    message(FATAL_ERROR "INPUTS was not supplied")
endif()
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR was not supplied")
endif()
if(NOT EXISTS "${INPUTS}")
    message(FATAL_ERROR "no recorded manifest inputs at ${INPUTS}")
endif()

file(STRINGS "${INPUTS}" inputs)
list(REMOVE_DUPLICATES inputs)
list(LENGTH inputs count)
if(count LESS 2)
    message(FATAL_ERROR "only ${count} manifest inputs were recorded, so this check saw nothing")
endif()

find_program(GIT_EXECUTABLE git)
if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git is what tells a tracked source from a build product, and it is missing")
endif()

set(missing "")
set(untracked "")
foreach(input IN LISTS inputs)
    if(NOT EXISTS "${input}")
        list(APPEND missing "${input}")
    endif()
endforeach()

# One call for the ordinary pass, and a call per input only once something has already failed.
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ls-files --error-unmatch -- ${inputs}
                RESULT_VARIABLE tracked OUTPUT_QUIET ERROR_QUIET)
if(NOT tracked EQUAL 0)
    foreach(input IN LISTS inputs)
        execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ls-files --error-unmatch -- "${input}"
                        RESULT_VARIABLE one OUTPUT_QUIET ERROR_QUIET)
        if(NOT one EQUAL 0)
            list(APPEND untracked "${input}")
        endif()
    endforeach()
endif()

if(missing OR untracked)
    string(REPLACE ";" "\n  " missing_text "${missing}")
    string(REPLACE ";" "\n  " untracked_text "${untracked}")
    set(report "")
    if(missing)
        string(APPEND report "manifest inputs that do not exist:\n  ${missing_text}\n")
    endif()
    if(untracked)
        string(APPEND report "manifest inputs the repository does not track:\n  ${untracked_text}\n")
    endif()
    message(FATAL_ERROR "${report}a manifest input is a tracked source, not a build product")
endif()

message(STATUS "${count} manifest inputs, all tracked")
