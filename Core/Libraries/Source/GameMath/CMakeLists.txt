# BFME: ADR-0003 §2.3. Logic-thread FP gate. Register access is Platform's;
# fp_platform_stub.cpp exists so the game links until that lands.
if(NOT IS_VS6_BUILD)
    add_library(core_gamemath STATIC
        fp/fp_thread.cpp
        fp/fp_platform_stub.cpp
    )
    target_include_directories(core_gamemath PUBLIC include)
    target_compile_features(core_gamemath PUBLIC cxx_std_20)
    target_link_libraries(core_gamemath PRIVATE core_config)
    find_package(Threads REQUIRED)
    target_link_libraries(core_gamemath PUBLIC Threads::Threads)

    # CI release builds are not debug builds, but a thread mismatch is still fatal.
    # GitHub Actions sets CI=true; a local configure does not.
    if(DEFINED ENV{CI} AND NOT "$ENV{CI}" STREQUAL "" AND NOT "$ENV{CI}" STREQUAL "0" AND NOT "$ENV{CI}" STREQUAL "false")
        set(BFME_FP_CI_DEFAULT ON)
    else()
        set(BFME_FP_CI_DEFAULT OFF)
    endif()
    # Verification builds need an explicit -DBFME_FP_CI=ON: GitHub Actions are off, so ENV{CI} does not enable it.
    option(BFME_FP_CI "ADR-0003: FP logic-thread mismatch is fatal (CI). Verification builds need an explicit -DBFME_FP_CI=ON." ${BFME_FP_CI_DEFAULT})
    if(BFME_FP_CI)
        target_compile_definitions(core_gamemath PRIVATE BFME_FP_CI=1)
    endif()
endif()
