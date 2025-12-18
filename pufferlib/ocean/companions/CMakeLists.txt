cmake_minimum_required(VERSION 3.12)
project(companions VERSION 0.2.0 LANGUAGES CXX)

# C++17 required
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
if(MSVC)
  add_compile_options(/W4)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

# =============================================================================
# Version tracking for downstream consumers
# =============================================================================
string(TIMESTAMP COMPANIONS_BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")
configure_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/companions_version.hpp.in"
  "${CMAKE_CURRENT_SOURCE_DIR}/companions_version.hpp"
  @ONLY
)

# Put all executables in bin/ directory (ensures DLLs are found by tests)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# =============================================================================
# Source files
# =============================================================================

# Core module
set(CORE_SOURCES
  src/core/types.cc
  src/core/cell.cc
  src/core/grid.cc
  src/core/object.cc
  src/core/object_manager.cc
  src/core/level_builder.cc
  src/core/pathfinder.cc
  src/core/agent_config.cc
  src/core/effect_config.cc
  src/core/game_logger.cc
  src/core/map_generator.cc
  src/core/d4_transform.cc
  src/core/snapshot.cc
  src/core/level_generator.cc
  src/core/fsm/fsm_states.cc
  src/core/fsm/enemies.cc
)

set(CORE_HEADERS
  src/core/types.h
  src/core/cell.h
  src/core/grid.h
  src/core/object.h
  src/core/object_manager.h
  src/core/level_builder.h
  src/core/pathfinder.h
  src/core/agent_config.h
  src/core/effect_config.h
  src/core/game_logger.h
  src/core/map_generator.h
  src/core/d4_transform.h
  src/core/snapshot.h
  src/core/level_config.h
  src/core/level_generator.h
  src/core/fsm/fsm_state.h
  src/core/fsm/fsm_states.h
  src/core/fsm/enemies.h
)

# Env module
set(ENV_SOURCES
  src/env/base_env.cc
  src/env/effect_system.cc
  src/env/synchro_env.cc
  src/env/aggro_env.cc
  src/env/dodge_env.cc
)

set(ENV_HEADERS
  src/env/base_env.h
  src/env/effect_system.h
  src/env/synchro_env.h
  src/env/aggro_env.h
  src/env/dodge_env.h
)

# Viz module
set(VIZ_SOURCES
  src/viz/renderer.cc
)

set(VIZ_HEADERS
  src/viz/renderer.h
)

# =============================================================================
# Libraries
# =============================================================================

# Core library
add_library(companions_core STATIC ${CORE_SOURCES} ${CORE_HEADERS})
target_include_directories(companions_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)

# Env library (depends on core)
add_library(companions_env STATIC ${ENV_SOURCES} ${ENV_HEADERS})
target_include_directories(companions_env PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/env)
target_link_libraries(companions_env PUBLIC companions_core)

# Viz library (depends on env and core)
add_library(companions_viz STATIC ${VIZ_SOURCES} ${VIZ_HEADERS})
target_include_directories(companions_viz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/viz)
target_link_libraries(companions_viz PUBLIC companions_env companions_core)

# Combined library for convenience
add_library(companions INTERFACE)
target_link_libraries(companions INTERFACE companions_viz companions_env companions_core)

# =============================================================================
# Distribution directory (for companions-loop sync)
# =============================================================================
set(COMPANIONS_DIST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dist" CACHE PATH
    "Distribution directory for compiled libraries")

# POST_BUILD: Sync libraries to dist directory
foreach(lib companions_core companions_env companions_viz)
  add_custom_command(TARGET ${lib} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${COMPANIONS_DIST_DIR}/$<CONFIG>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:${lib}>"
      "${COMPANIONS_DIST_DIR}/$<CONFIG>/"
    COMMENT "Syncing ${lib} to dist/$<CONFIG>/"
  )
endforeach()

# Sync version header and data files
add_custom_command(TARGET companions_core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
    "${CMAKE_CURRENT_SOURCE_DIR}/companions_version.hpp"
    "${COMPANIONS_DIST_DIR}/"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
    "${CMAKE_CURRENT_SOURCE_DIR}/data"
    "${COMPANIONS_DIST_DIR}/data"
  COMMENT "Syncing version header and data to dist/"
)

# PufferLib wrapper (extern "C" interface for Python bindings)
add_library(companions_wrapper STATIC synchro_wrapper.cc)
target_include_directories(companions_wrapper PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(companions_wrapper PUBLIC companions_env companions_core)

# =============================================================================
# Test executables
# =============================================================================
add_executable(companions_test tests/test_core.cc)
target_link_libraries(companions_test companions_viz companions_env companions_core)
target_include_directories(companions_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(companions_fsm_test tests/test_fsm.cc)
target_link_libraries(companions_fsm_test companions_viz companions_env companions_core)
target_include_directories(companions_fsm_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(companions_aggro_test tests/test_aggro_env.cc)
target_link_libraries(companions_aggro_test companions_viz companions_env companions_core)
target_include_directories(companions_aggro_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(companions_effects_test tests/test_effects.cc)
target_link_libraries(companions_effects_test companions_viz companions_env companions_core)
target_include_directories(companions_effects_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(companions_dodge_test tests/test_dodge_env.cc)
target_link_libraries(companions_dodge_test companions_viz companions_env companions_core)
target_include_directories(companions_dodge_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(companions_map_generator_test tests/test_map_generator.cc)
target_link_libraries(companions_map_generator_test companions_viz companions_env companions_core)
target_include_directories(companions_map_generator_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(companions_d4_transform_test tests/test_d4_transform.cc)
target_link_libraries(companions_d4_transform_test companions_viz companions_env companions_core)
target_include_directories(companions_d4_transform_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(companions_snapshot_test tests/test_snapshot.cc)
target_link_libraries(companions_snapshot_test companions_viz companions_env companions_core)
target_include_directories(companions_snapshot_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# =============================================================================
# Demo executable (cross-platform)
# =============================================================================
add_executable(companions_demo src/demo/play_demo.cc)
target_link_libraries(companions_demo companions_viz companions_env companions_core)
target_include_directories(companions_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# =============================================================================
# Benchmark executable
# =============================================================================
add_executable(companions_fsm_benchmark benchmarks/cpp/fsm_benchmark.cc)
target_link_libraries(companions_fsm_benchmark companions_viz companions_env companions_core)
target_include_directories(companions_fsm_benchmark PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# =============================================================================
# Parity test generator (for Python wrapper validation)
# =============================================================================
add_executable(parity_generator tests/parity_generator.cc)
target_link_libraries(parity_generator companions_env companions_core)
target_include_directories(parity_generator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

# =============================================================================
# Testing
# =============================================================================
enable_testing()
add_test(NAME companions_test COMMAND companions_test)
add_test(NAME companions_fsm_test COMMAND companions_fsm_test)
add_test(NAME companions_aggro_test COMMAND companions_aggro_test)
add_test(NAME companions_effects_test COMMAND companions_effects_test)
add_test(NAME companions_dodge_test COMMAND companions_dodge_test)
add_test(NAME companions_map_generator_test COMMAND companions_map_generator_test)
add_test(NAME companions_d4_transform_test COMMAND companions_d4_transform_test)
add_test(NAME companions_snapshot_test COMMAND companions_snapshot_test)

# =============================================================================
# UE5 DLL (Dynamic library for Unreal Engine 5)
# =============================================================================
option(BUILD_UE5_DLL "Build dynamic library for Unreal Engine 5" OFF)

if(BUILD_UE5_DLL)
  # UE5 API sources
  set(UE_API_SOURCES
    src/ue_api/companions_ue.cc
  )

  set(UE_API_HEADERS
    src/ue_api/companions_ue.h
  )

  # Create shared library
  add_library(companions_ue SHARED ${UE_API_SOURCES} ${UE_API_HEADERS})

  # Link against static libraries (they get compiled into the DLL)
  target_link_libraries(companions_ue PRIVATE companions_env companions_core)

  # Include directories
  target_include_directories(companions_ue
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/ue_api
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
  )

  # Define export macro
  target_compile_definitions(companions_ue PRIVATE COMPANIONS_UE_EXPORTS)

  # Windows-specific settings
  if(WIN32)
    # Generate import library (.lib) alongside DLL
    set_target_properties(companions_ue PROPERTIES
      WINDOWS_EXPORT_ALL_SYMBOLS OFF
      RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
      LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
      ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    )
  endif()

  # Copy data files next to DLL
  add_custom_command(TARGET companions_ue POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/data"
          "$<TARGET_FILE_DIR:companions_ue>/data"
      COMMENT "Copying data files to output directory"
  )

  # UE5 API test executable
  add_executable(companions_ue_test tests/test_ue_api.cc)
  target_link_libraries(companions_ue_test companions_ue)
  target_include_directories(companions_ue_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/ue_api)
  add_test(NAME companions_ue_test COMMAND companions_ue_test)

  # UE5 API parity test (verifies UE API matches direct C++ API)
  add_executable(companions_ue_parity_test tests/test_ue_api_parity.cc)
  target_link_libraries(companions_ue_parity_test companions_ue companions_env companions_core)
  target_include_directories(companions_ue_parity_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ue_api
    ${CMAKE_CURRENT_SOURCE_DIR}
  )
  add_test(NAME companions_ue_parity_test COMMAND companions_ue_parity_test)

  # Install DLL and headers
  install(TARGETS companions_ue
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
  )
  install(FILES ${UE_API_HEADERS} DESTINATION include/companions/ue_api)
endif()

# =============================================================================
# Installation (optional)
# =============================================================================
install(TARGETS companions_core companions_env companions_viz
  ARCHIVE DESTINATION lib
  LIBRARY DESTINATION lib
)
install(FILES ${CORE_HEADERS} DESTINATION include/companions/core)
install(FILES ${ENV_HEADERS} DESTINATION include/companions/env)
install(FILES ${VIZ_HEADERS} DESTINATION include/companions/viz)
