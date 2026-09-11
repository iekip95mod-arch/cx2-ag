file(READ "${SOURCE}/lua/nps_v4.lua" application)
file(READ "${SOURCE}/tests/target/retained_failure.lua" probe)
file(WRITE "${OUTPUT}" "${application}\n${probe}\n")
