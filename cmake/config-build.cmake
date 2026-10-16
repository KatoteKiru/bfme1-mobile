# Do we want to build extra SDK stuff or just the game binary?
option(RTS_BUILD_CORE_TOOLS "Build core tools" ON)
option(RTS_BUILD_CORE_EXTRAS "Build core extra tools/tests" OFF)
option(RTS_BUILD_ZEROHOUR "Build Zero Hour code." ON)
option(RTS_BUILD_GENERALS "Build Generals code." ON)
option(RTS_BUILD_OPTION_PROFILE "Build code with the \"Profile\" configuration." OFF)
option(RTS_BUILD_OPTION_PROFILE_TRACY "Build code with Tracy profiling enabled." OFF)
option(RTS_BUILD_OPTION_DEBUG "Build code with the \"Debug\" configuration." OFF)
option(RTS_BUILD_OPTION_ASAN "Build code with Address Sanitizer." OFF)
option(RTS_BUILD_OPTION_VC6_FULL_DEBUG "Build VC6 with full debug info." OFF)
option(RTS_BUILD_OPTION_FFMPEG "Enable FFmpeg support" OFF)

# Linux/SDL3 and OpenAL options (Phase 1 Linux port)
option(SAGE_USE_SDL3 "Use SDL3 for windowing/input (Linux/macOS)" OFF)
option(SAGE_USE_OPENAL "Use OpenAL for audio backend (Linux/macOS)" OFF)

# GeneralsX @feature BenderAI 21/04/2026 In-game update checker via GitHub Releases API (SDL3+libcurl builds only)
# Default ON when SDL3 is enabled, but only if the user has not explicitly set SAGE_UPDATE_CHECK.
# An explicit -DSAGE_UPDATE_CHECK=OFF on the cmake command line is always respected.
if(NOT DEFINED CACHE{SAGE_UPDATE_CHECK})
    set(SAGE_UPDATE_CHECK "${SAGE_USE_SDL3}" CACHE BOOL "Enable in-game update check via GitHub Releases API")
endif()

# The GeneralsOnline PC client is built with GENERALS_ONLINE_HIGH_FPS_SERVER defined
# unconditionally, so its simulation runs at 60 Hz while ours runs at 30. That is part of
# the lockstep contract - the service even separates the two populations by client id
# (gen_online_60hz vs gen_online_30hz) - so cross-play with Windows requires this ON.
# It is defined at the top level rather than in a header because it changes the layout of
# GameLogic (m_frameLegacy) and the value of WWSyncPerSecond, which Core and GeneralsMD
# must agree on in every single translation unit.
option(SAGE_HIGH_FPS_SIM "Simulate at 60 Hz to match the GeneralsOnline PC client" OFF)

# macOS port option (Phase 5)
option(SAGE_USE_MOLTENVK "Use MoltenVK for Vulkan on macOS (Phase 5 macOS port)" OFF)

# BFME: E1-17b Native D3D8->GLES3 (d3d8gles). ON for Android, OFF on desktop
# unless a preset or -D asks for it, so linux64-deploy stays on DXVK.
# The runtime choice (DXVK vs this backend) is d3d8gles_ShouldUseVulkanBackend().
if(ANDROID)
    set(_SAGE_USE_D3D8GLES_DEFAULT ON)
else()
    set(_SAGE_USE_D3D8GLES_DEFAULT OFF)
endif()
option(SAGE_USE_D3D8GLES "Build the native D3D8 to GLES3 backend (d3d8gles)" ${_SAGE_USE_D3D8GLES_DEFAULT})
# BFME: E1-17b Setup writes render_backend.cfg before the process starts.
# Without this library that choice cannot be honored.
if(ANDROID AND NOT SAGE_USE_D3D8GLES)
    message(WARNING "SAGE_USE_D3D8GLES is OFF on Android. Setup's GLES choice (render_backend.cfg) will be ignored because d3d8gles is not linked.")
endif()
# BFME: E1-17b Mesa llvmpipe advertises S3TC and would skip the CPU decode
# Mali always takes. OFF leaves the extension check. ON forces the CPU path
# for the whole build; GENERALSX_D3D8GLES_FORCE_DXT_CPU=1 does it per run.
option(SAGE_D3D8GLES_FORCE_DXT_CPU "Force d3d8gles to decompress DXT on the CPU" OFF)

# SagePatch — optional QoL features for casual play (screenshot, cursor lock,
# brightness, camera/scroll INI overrides). Compiles to a separate shared lib
# that is loaded via DYLD_INSERT_LIBRARIES (macOS) / LD_PRELOAD (Linux) at runtime.
option(RTS_BUILD_OPTION_SAGE_PATCH "Build SagePatch QoL extras (macOS/Linux, requires SDL3)" ON)

if(NOT RTS_BUILD_ZEROHOUR AND NOT RTS_BUILD_GENERALS)
    set(RTS_BUILD_ZEROHOUR TRUE)
    message("You must select one project to build, building Zero Hour by default.")
endif()

add_feature_info(CoreTools RTS_BUILD_CORE_TOOLS "Build Core Mod Tools")
add_feature_info(CoreExtras RTS_BUILD_CORE_EXTRAS "Build Core Extra Tools/Tests")
add_feature_info(ZeroHourStuff RTS_BUILD_ZEROHOUR "Build Zero Hour code")
add_feature_info(GeneralsStuff RTS_BUILD_GENERALS "Build Generals code")
add_feature_info(ProfileBuild RTS_BUILD_OPTION_PROFILE "Building as a \"Profile\" build")
add_feature_info(DebugBuild RTS_BUILD_OPTION_DEBUG "Building as a \"Debug\" build")
add_feature_info(AddressSanitizer RTS_BUILD_OPTION_ASAN "Building with address sanitizer")
add_feature_info(Vc6FullDebug RTS_BUILD_OPTION_VC6_FULL_DEBUG "Building VC6 with full debug info")
add_feature_info(FFmpegSupport RTS_BUILD_OPTION_FFMPEG "Building with FFmpeg support")
add_feature_info(SDL3Windowing SAGE_USE_SDL3 "Using SDL3 for windowing (Linux)")
add_feature_info(OpenALAudio SAGE_USE_OPENAL "Using OpenAL for audio (Linux)")
add_feature_info(D3D8GLES SAGE_USE_D3D8GLES "Native D3D8 to GLES3 backend")
add_feature_info(D3D8GLESForceDxtCpu SAGE_D3D8GLES_FORCE_DXT_CPU "Force CPU DXT decompress in d3d8gles")
add_feature_info(UpdateCheck SAGE_UPDATE_CHECK "In-game update check via GitHub Releases API")
add_feature_info(SagePatch RTS_BUILD_OPTION_SAGE_PATCH "Build SagePatch QoL extras (macOS)")

set(RTS_BUILD_OUTPUT_SUFFIX "" CACHE STRING "Suffix appended to output names of installable targets")

if(RTS_BUILD_ZEROHOUR)
    option(RTS_BUILD_ZEROHOUR_TOOLS "Build tools for Zero Hour" ON)
    option(RTS_BUILD_ZEROHOUR_EXTRAS "Build extra tools/tests for Zero Hour" OFF)
    option(RTS_BUILD_ZEROHOUR_DOCS "Build documentation for Zero Hour" OFF)

    add_feature_info(ZeroHourTools RTS_BUILD_ZEROHOUR_TOOLS "Build Zero Hour Mod Tools")
    add_feature_info(ZeroHourExtras RTS_BUILD_ZEROHOUR_EXTRAS "Build Zero Hour Extra Tools/Tests")
    add_feature_info(ZeroHourDocs RTS_BUILD_ZEROHOUR_DOCS "Build Zero Hour Documentation")
endif()

if(RTS_BUILD_GENERALS)
    option(RTS_BUILD_GENERALS_TOOLS "Build tools for Generals" ON)
    option(RTS_BUILD_GENERALS_EXTRAS "Build extra tools/tests for Generals" OFF)
    option(RTS_BUILD_GENERALS_DOCS "Build documentation for Generals" OFF)

    add_feature_info(GeneralsTools RTS_BUILD_GENERALS_TOOLS "Build Generals Mod Tools")
    add_feature_info(GeneralsExtras RTS_BUILD_GENERALS_EXTRAS "Build Generals Extra Tools/Tests")
    add_feature_info(GeneralsDocs RTS_BUILD_GENERALS_DOCS "Build Generals Documentation")
endif()

if(NOT IS_VS6_BUILD)
    # Because we set CMAKE_CXX_STANDARD_REQUIRED and CMAKE_CXX_EXTENSIONS in the compilers.cmake this should be enforced.
    target_compile_features(core_config INTERFACE cxx_std_20)
endif()

# GeneralsX @feature Android port 23/09/2026 Report every float-to-integer conversion of a NaN
# or an out-of-range value, with its file and line. That is where x86 (0x80000000) and ARM
# (saturation) produce different integers from the same source -- the cause of the debris
# desync -- and the fp "invalid" flag cannot tell it apart from a harmless NaN comparison.
# The check is clang's; the handler that logs it lives in GeneralsMD/Code/Main/
# ReferenceFloatMath.cpp, so this is limited to the engine's own targets (core_config) and
# never reaches third-party libraries that would not link it. Recoverable: the conversion
# still happens as before, it is only reported.
if(ANDROID)
    target_compile_options(core_config INTERFACE -fsanitize=float-cast-overflow -fsanitize-recover=float-cast-overflow)
endif()

if(IS_VS6_BUILD AND RTS_BUILD_OPTION_VC6_FULL_DEBUG)
    target_compile_options(core_config INTERFACE ${RTS_FLAGS} /Zi)
else()
    target_compile_options(core_config INTERFACE ${RTS_FLAGS})
endif()

# This disables a lot of warnings steering developers to use windows only functions/function names.
if(MSVC)
    target_compile_definitions(core_config INTERFACE _CRT_NONSTDC_NO_WARNINGS _CRT_SECURE_NO_WARNINGS $<$<CONFIG:DEBUG>:_DEBUG_CRT>)
endif()

if(UNIX)
    target_compile_definitions(core_config INTERFACE _UNIX)
    # Ubuntu 24.04+ and macOS have strlcpy/strlcat/wcslcpy/wcslcat in libc
    # GeneralsX @TheSuperHackers @build BenderAI 11/02/2026 Added guards for glibc 2.38+
    target_compile_definitions(core_config INTERFACE 
        HAVE_STRLCPY HAVE_STRLCAT HAVE_WCSLCPY HAVE_WCSLCAT)
endif()

if(RTS_BUILD_OPTION_DEBUG)
    target_compile_definitions(core_config INTERFACE RTS_DEBUG WWDEBUG DEBUG)
else()
    target_compile_definitions(core_config INTERFACE RTS_RELEASE NDEBUG)
endif()

if(RTS_BUILD_OPTION_PROFILE)
    target_compile_definitions(core_config INTERFACE RTS_PROFILE_LEGACY)
endif()

# Define a dummy Tracy target when the build option is disabled.
if(RTS_BUILD_OPTION_PROFILE_TRACY)
    include(cmake/tracy.cmake)
else()
    add_library(core_profile_tracy INTERFACE)
endif()
# Linux port options (Phase 1)
if(SAGE_USE_SDL3)
    target_compile_definitions(core_config INTERFACE SAGE_USE_SDL3)
    message(STATUS "SDL3 windowing backend enabled")
endif()

if(SAGE_USE_OPENAL)
    target_compile_definitions(core_config INTERFACE SAGE_USE_OPENAL)
    message(STATUS "OpenAL audio backend enabled")
endif()

# BFME: E1-17b The macro rides core_config (same path as SAGE_USE_SDL3),
# which every engine TU that used to key a GLES fix off __ANDROID__ already
# links. A directory-wide add_compile_definitions also reached gamespy
# (FetchContent after this file). d3d8gles does not link core_config; its
# own CMakeLists defines the macro, and SAGE_D3D8GLES_FORCE_DXT_CPU, on
# that target alone.
if(SAGE_USE_D3D8GLES AND (WIN32 OR APPLE))
    message(FATAL_ERROR "SAGE_USE_D3D8GLES is supported on Android and desktop Linux only (WIN32 uses D3D8.DLL; Apple stays on the dylib path)")
endif()
if(SAGE_USE_D3D8GLES AND NOT SAGE_USE_SDL3)
    message(FATAL_ERROR "SAGE_USE_D3D8GLES requires SAGE_USE_SDL3 (the GLES context is an SDL GL context)")
endif()
# BFME: E1-17b Generals/Code/Main/SDL3Main.cpp:305 always passes
# SDL_WINDOW_VULKAN. Zero Hour switches the window with the backend.
if(SAGE_USE_D3D8GLES AND RTS_BUILD_GENERALS)
    message(WARNING "SAGE_USE_D3D8GLES is ON for Generals (RTS_BUILD_GENERALS). Generals/Code/Main/SDL3Main.cpp:305 always creates an SDL_WINDOW_VULKAN window, so GENERALSX_RENDER_BACKEND=gles would load the GLES backend against a Vulkan window. Zero Hour's SDL3Main switches the window with the backend.")
endif()
if(SAGE_USE_D3D8GLES)
    target_compile_definitions(core_config INTERFACE SAGE_USE_D3D8GLES)
    message(STATUS "D3D8 GLES backend enabled (SAGE_USE_D3D8GLES)")
endif()
if(SAGE_USE_D3D8GLES AND SAGE_D3D8GLES_FORCE_DXT_CPU)
    message(STATUS "D3D8 GLES: DXT CPU decompress forced at compile time")
endif()

# GeneralsX @feature BenderAI 21/04/2026 Update check compile definition
if(SAGE_UPDATE_CHECK)
    target_compile_definitions(core_config INTERFACE SAGE_UPDATE_CHECK)
    message(STATUS "In-game update checker enabled")
endif()

if(SAGE_USE_GLM)
    target_compile_definitions(core_config INTERFACE SAGE_USE_GLM)
    message(STATUS "GLM math library enabled (DirectX 8 replacement)")
endif()

# macOS MoltenVK detection (Phase 5)
# GeneralsX @build BenderAI 24/02/2026 - Phase 5 macOS port
if(APPLE AND SAGE_USE_MOLTENVK)
    find_package(Vulkan REQUIRED COMPONENTS MoltenVK)
    if(NOT Vulkan_FOUND)
        message(FATAL_ERROR "MoltenVK not found. Install from https://vulkan.lunarg.com/sdk/home#mac (LunarG installer to ~/VulkanSDK/<version>/macOS)")
    endif()
    
    target_compile_definitions(core_config INTERFACE SAGE_USE_MOLTENVK)
    target_link_libraries(core_config INTERFACE Vulkan::Vulkan)
    if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        # On iOS, MoltenVK links as a STATIC library, so its framework dependencies
        # must be linked by the consumer (the macOS dylib resolved these itself).
        target_link_libraries(core_config INTERFACE
            "-framework Metal"
            "-framework IOSurface"
            "-framework CoreGraphics"
            "-framework QuartzCore"
            "-framework Foundation"
            "-framework UIKit"
        )
    endif()
    message(STATUS "MoltenVK (Vulkan on macOS) detected and enabled")
    message(STATUS "  Vulkan SDK: ${Vulkan_INCLUDE_DIRS}")
    message(STATUS "  MoltenVK: Will translate Vulkan to Metal")
endif()