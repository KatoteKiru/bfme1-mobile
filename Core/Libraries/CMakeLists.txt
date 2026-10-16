# WW common libraries decended from earliest C&C games
add_subdirectory(Source/WWVegas)

# BFME: E1-17b d3d8gles no-ops unless SAGE_USE_D3D8GLES (ON for Android).
add_subdirectory(Source/d3d8gles)

# profiling library
add_subdirectory(Source/profile)

# debugging library
add_subdirectory(Source/debug)

add_subdirectory(Source/EABrowserDispatch)
add_subdirectory(Source/EABrowserEngine)
add_subdirectory(Source/Compression)

# BFME: ADR-0003 §2.3. Logic-thread FP gate (core_gamemath). Not built for VC6.
add_subdirectory(Source/GameMath)
