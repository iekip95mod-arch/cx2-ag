# Every file a host configure hashes into a capability manifest has to be one the repository tracks.
# A build product hashed there makes the manifest id depend on whether somebody had already built
# the tree, so a fresh checkout configures differently from a warm one, and the fresh one loses:
# configure refuses an input that does not exist yet. Reads the list the configure recorded, so
# adding an input to a hash brings it under this check without a second edit here.
#
# The bootstrapped SDK and cross toolchain artifacts are hashed today and are not tracked, which is
# issue 222. Until that hash moves onto sources, the calls that hash them declare them through
# nps_defer_manifest_inputs and this check skips exactly those. It does not skip a path pattern, and
# a deferral no hash recorded fails here, and so does a deferral set the repository has come to track
# in full, so landing 222 is what removes the exemption rather than a later reader deciding to.

if(NOT DEFINED INPUTS)
    message(FATAL_ERROR "INPUTS was not supplied")
endif()
if(NOT DEFINED DEFERRED)
    message(FATAL_ERROR "DEFERRED was not supplied")
endif()
if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR was not supplied")
endif()
if(NOT EXISTS "${INPUTS}")
    message(FATAL_ERROR "no recorded manifest inputs at ${INPUTS}")
endif()
if(NOT EXISTS "${DEFERRED}")
    message(FATAL_ERROR "no recorded deferrals at ${DEFERRED}")
endif()

file(STRINGS "${INPUTS}" inputs)
list(REMOVE_DUPLICATES inputs)
list(LENGTH inputs count)
if(count LESS 2)
    message(FATAL_ERROR "only ${count} manifest inputs were recorded, so this check saw nothing")
endif()

file(STRINGS "${DEFERRED}" deferred)
if(deferred)
    list(REMOVE_DUPLICATES deferred)
endif()

find_program(GIT_EXECUTABLE git)
if(NOT GIT_EXECUTABLE)
    message(FATAL_ERROR "git is what tells a tracked source from a build product, and it is missing")
endif()

function(nps_tracked input result)
    execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ls-files --error-unmatch -- "${input}"
                    RESULT_VARIABLE status OUTPUT_QUIET ERROR_QUIET)
    if(status EQUAL 0)
        set(${result} ON PARENT_SCOPE)
    else()
        set(${result} OFF PARENT_SCOPE)
    endif()
endfunction()

# A deferral earns its exemption by naming an input some hash actually recorded. The declaring call
# names a whole hash, and part of that hash is tracked source already, so a tracked entry in here is
# ordinary. What is not ordinary is every entry being tracked, because then the exemption covers
# nothing and 222 is finished.
set(unrecorded "")
set(still_untracked "")
foreach(input IN LISTS deferred)
    list(FIND inputs "${input}" recorded)
    if(recorded LESS 0)
        list(APPEND unrecorded "${input}")
    else()
        nps_tracked("${input}" is_tracked)
        if(NOT is_tracked)
            list(APPEND still_untracked "${input}")
        endif()
    endif()
endforeach()
set(obsolete "")
if(deferred AND NOT unrecorded AND NOT still_untracked)
    set(obsolete "${deferred}")
endif()

set(checked "${inputs}")
if(deferred)
    list(REMOVE_ITEM checked ${deferred})
endif()

set(missing "")
set(untracked "")
foreach(input IN LISTS checked)
    if(NOT EXISTS "${input}")
        list(APPEND missing "${input}")
    endif()
endforeach()

# One call for the ordinary pass, and a call per input only once something has already failed.
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${SOURCE_DIR}" ls-files --error-unmatch -- ${checked}
                RESULT_VARIABLE tracked OUTPUT_QUIET ERROR_QUIET)
if(NOT tracked EQUAL 0)
    foreach(input IN LISTS checked)
        nps_tracked("${input}" is_tracked)
        if(NOT is_tracked)
            list(APPEND untracked "${input}")
        endif()
    endforeach()
endif()

if(missing OR untracked OR unrecorded OR obsolete)
    string(REPLACE ";" "\n  " missing_text "${missing}")
    string(REPLACE ";" "\n  " untracked_text "${untracked}")
    string(REPLACE ";" "\n  " unrecorded_text "${unrecorded}")
    string(REPLACE ";" "\n  " obsolete_text "${obsolete}")
    set(report "")
    if(missing)
        string(APPEND report "manifest inputs that do not exist:\n  ${missing_text}\n")
    endif()
    if(untracked)
        string(APPEND report "manifest inputs the repository does not track:\n  ${untracked_text}\n")
    endif()
    if(unrecorded)
        string(APPEND report "deferred inputs no manifest hash recorded:\n  ${unrecorded_text}\n")
    endif()
    if(obsolete)
        string(APPEND report "every deferred input is tracked now, so drop the deferral and close 222:\n  ${obsolete_text}\n")
    endif()
    message(FATAL_ERROR "${report}a manifest input is a tracked source, not a build product")
endif()

list(LENGTH checked checked_count)
list(LENGTH still_untracked deferred_count)
message(STATUS "${checked_count} manifest inputs, all tracked, and ${deferred_count} build products deferred to issue 222")
