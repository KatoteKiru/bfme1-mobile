# Runs blend_zh_dump built against the upstream tree (OLD) and against this tree (NEW) on the same
# input and requires byte-identical output (see src/BlendZhDump.cpp).
cmake_policy(SET CMP0054 NEW)  # "OLD" below is a string, not the -DOLD variable
foreach(side OLD NEW)
    set(args --out ${WORK}/blend-${side}.bin)
    if(MAPS_DIR)
        list(APPEND args --maps-dir ${MAPS_DIR})
    endif()
    # The upstream ParseBlendTileData leaks the v7 tile buffer (fixed in this tree); in ASan builds
    # leak detection is turned off for the OLD binary only, NEW keeps it.
    set(env "")
    if(side STREQUAL "OLD")
        set(env ${CMAKE_COMMAND} -E env "ASAN_OPTIONS=detect_leaks=0")
    endif()
    execute_process(COMMAND ${env} ${${side}} ${args} RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    message(STATUS "${side}: ${out}${err}")
    if(NOT rc EQUAL 0)
        message(FATAL_ERROR "${${side}} failed: ${rc}")
    endif()
endforeach()
execute_process(COMMAND ${CMAKE_COMMAND} -E compare_files ${WORK}/blend-OLD.bin ${WORK}/blend-NEW.bin RESULT_VARIABLE diff)
if(NOT diff EQUAL 0)
    message(FATAL_ERROR "ZH BlendTileData differs between the upstream and the E1-25 build")
endif()
file(SIZE ${WORK}/blend-NEW.bin size)
message(STATUS "identical: ${size} bytes")
file(REMOVE ${WORK}/blend-OLD.bin ${WORK}/blend-NEW.bin)  # ~1.7 GB together
