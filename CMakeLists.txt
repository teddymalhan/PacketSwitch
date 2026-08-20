cmake_minimum_required(VERSION 3.21)





project(
  WireLab
  VERSION 0.1.0
  LANGUAGES CXX
)





include(cmake/StandardSettings.cmake)
include(cmake/StaticAnalyzers.cmake)
include(cmake/Utils.cmake)
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()
message(STATUS "Started CMake for ${PROJECT_NAME} v${PROJECT_VERSION}...\n")

if (UNIX)
    add_compile_options("$<$<CONFIG:DEBUG>:-D_DEBUG>")
endif (UNIX)






if(${PROJECT_NAME}_USE_ALT_NAMES)
  string(TOLOWER ${PROJECT_NAME} PROJECT_NAME_LOWERCASE)
  string(TOUPPER ${PROJECT_NAME} PROJECT_NAME_UPPERCASE)
else()
  set(PROJECT_NAME_LOWERCASE ${PROJECT_NAME})
  set(PROJECT_NAME_UPPERCASE ${PROJECT_NAME})
endif()





if(PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  message(FATAL_ERROR "In-source builds not allowed. Please make a new directory (called a build directory) and run CMake from there.\n")
endif()





include(cmake/Conan.cmake)
include(cmake/Vcpkg.cmake)






include(cmake/SourcesAndHeaders.cmake)


add_library(
  ${PROJECT_NAME}
  ${headers}
  ${sources}
)

option(WIRELAB_ENABLE_CUDA "Build the optional WireLab CUDA packet parser" OFF)
if(WIRELAB_ENABLE_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  add_library(wirelab_cuda src/cuda_packet_parser.cu include/wirelab/cuda_packet_parser.hpp)
  target_compile_features(wirelab_cuda PUBLIC cxx_std_17)
  target_include_directories(wirelab_cuda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(wirelab_cuda PUBLIC ${PROJECT_NAME} CUDA::cudart)
  target_compile_definitions(wirelab_cuda PUBLIC WIRELAB_HAS_CUDA=1)
  set_target_properties(wirelab_cuda PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_SEPARABLE_COMPILATION ON)
endif()

option(WIRELAB_ENABLE_METAL "Build the optional WireLab Metal packet parser and traffic generator for Apple GPUs" OFF)
if(WIRELAB_ENABLE_METAL AND APPLE)
  enable_language(OBJCXX)
  find_package(Python3 COMPONENTS Interpreter REQUIRED)
  set(METAL_KERNEL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/metal_kernel.metal")
  set(METAL_KERNEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/metal_kernel_source.h")
  add_custom_command(
    OUTPUT "${METAL_KERNEL_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_metal.py"
            "${METAL_KERNEL_SOURCE}" "${METAL_KERNEL_HEADER}"
    DEPENDS "${METAL_KERNEL_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_metal.py"
    COMMENT "Embedding the WireLab Metal kernel source"
    VERBATIM)
  set(METAL_TRAFFIC_KERNEL_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/src/metal_traffic_kernel.metal")
  set(METAL_TRAFFIC_KERNEL_HEADER "${CMAKE_CURRENT_BINARY_DIR}/metal_traffic_kernel_source.h")
  add_custom_command(
    OUTPUT "${METAL_TRAFFIC_KERNEL_HEADER}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_metal.py"
            "${METAL_TRAFFIC_KERNEL_SOURCE}" "${METAL_TRAFFIC_KERNEL_HEADER}"
    DEPENDS "${METAL_TRAFFIC_KERNEL_SOURCE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/embed_metal.py"
    COMMENT "Embedding the WireLab Metal traffic kernel source"
    VERBATIM)
  add_library(wirelab_metal
    src/metal_packet_parser.mm
    src/metal_traffic_generator.mm
    include/wirelab/metal_packet_parser.hpp
    include/wirelab/metal_traffic_generator.hpp
    "${METAL_KERNEL_HEADER}"
    "${METAL_TRAFFIC_KERNEL_HEADER}")
  target_compile_features(wirelab_metal PUBLIC cxx_std_17)
  set_target_properties(wirelab_metal PROPERTIES
    OBJCXX_STANDARD 17
    OBJCXX_STANDARD_REQUIRED ON)
  target_compile_options(wirelab_metal PRIVATE -fobjc-arc -fblocks)
  target_include_directories(wirelab_metal
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(wirelab_metal
    PUBLIC ${PROJECT_NAME}
    PRIVATE "-framework Metal" "-framework Foundation")
  target_compile_definitions(wirelab_metal PUBLIC WIRELAB_HAS_METAL=1)
endif()

if(${PROJECT_NAME}_VERBOSE_OUTPUT)
  verbose_message("Found the following sources:")
  foreach(source IN LISTS sources)
    verbose_message("* ${source}")
  endforeach()
  verbose_message("Found the following headers:")
  foreach(header IN LISTS headers)
    verbose_message("* ${header}")
  endforeach()
endif()

set_target_properties(
  ${PROJECT_NAME}
  PROPERTIES
  ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib/${CMAKE_BUILD_TYPE}"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/${CMAKE_BUILD_TYPE}"
)
 

message(STATUS "Added all header and implementation files.\n")





target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_17)

if(WIN32)
  target_compile_definitions(${PROJECT_NAME} PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
  target_link_libraries(${PROJECT_NAME} PRIVATE Ws2_32 Advapi32)
 
endif()
include(cmake/CompilerWarnings.cmake)
set_wirelab_warnings(${PROJECT_NAME})

verbose_message("Applied compiler warnings. Using standard ${CMAKE_CXX_STANDARD}.\n")





include(cmake/Doxygen.cmake)


































set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

verbose_message("Successfully added all dependencies and linked against them.")






target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<INSTALL_INTERFACE:include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  # The version header is generated into the build tree; consumers include it as
  # <wirelab/version.hpp> exactly like a checked-in header.
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

message(STATUS "Finished setting up include directories.")





add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

verbose_message("Project is now aliased as ${PROJECT_NAME}::${PROJECT_NAME}.\n")





# Backend selection is compiled once, with whichever accelerators this build
# has, and linked by every host that runs benchmarks: the CLI and the switch
# serving the control API.
add_library(wirelab_backends STATIC src/accelerated_backends.cpp include/wirelab/accelerated_backends.hpp)
target_link_libraries(wirelab_backends PUBLIC ${PROJECT_NAME})
target_compile_features(wirelab_backends PUBLIC cxx_std_17)
if(TARGET wirelab_cuda)
  set_source_files_properties(src/accelerated_backends.cpp PROPERTIES LANGUAGE CUDA)
  target_link_libraries(wirelab_backends PUBLIC wirelab_cuda)
  set_target_properties(wirelab_backends PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON)
endif()
if(TARGET wirelab_metal)
  target_link_libraries(wirelab_backends PUBLIC wirelab_metal)
endif()

add_executable(vport src/vport_main.cpp)
target_link_libraries(vport PRIVATE ${PROJECT_NAME})
target_compile_features(vport PRIVATE cxx_std_17)

add_executable(vswitch src/vswitch_main.cpp)
target_link_libraries(vswitch PRIVATE wirelab_backends)
target_compile_features(vswitch PRIVATE cxx_std_17)

add_executable(wirelab_pcap src/wirelab_pcap_main.cpp)
target_link_libraries(wirelab_pcap PRIVATE ${PROJECT_NAME})
target_compile_features(wirelab_pcap PRIVATE cxx_std_17)

add_executable(wirelab_bench src/wirelab_bench.cpp)
target_link_libraries(wirelab_bench PRIVATE wirelab_backends)
target_compile_features(wirelab_bench PRIVATE cxx_std_17)
if(TARGET wirelab_cuda)
  # CMake needs a CUDA translation unit in this executable to schedule the
  # device-link step required by the separably compiled static CUDA backend.
  set_source_files_properties(src/wirelab_bench.cpp PROPERTIES LANGUAGE CUDA)
  target_link_libraries(wirelab_bench PRIVATE wirelab_cuda)
  set_target_properties(wirelab_bench PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON)
endif()
if(TARGET wirelab_metal)
  target_link_libraries(wirelab_bench PRIVATE wirelab_metal)
endif()

# The Qt-free orchestration layer every desktop frontend drives. It sits above
# wirelab_backends because the Reports workspace runs the same accelerated
# benchmark backends the CLI does.
add_library(wirelab_session STATIC
  src/session.cpp
  src/session_topology.cpp
  src/session_traffic.cpp
  src/session_policy.cpp
  src/session_report.cpp
  src/session_detail.hpp
  include/wirelab/session.hpp)
target_link_libraries(wirelab_session PUBLIC wirelab_backends)
target_compile_features(wirelab_session PUBLIC cxx_std_17)
# The Reports workspace names the configuration its numbers were measured in.
target_compile_definitions(wirelab_session PRIVATE WIRELAB_BUILD_TYPE="$<CONFIG>")
if(TARGET wirelab_cuda)
  target_link_libraries(wirelab_session PUBLIC wirelab_cuda)
endif()
if(TARGET wirelab_metal)
  target_link_libraries(wirelab_session PUBLIC wirelab_metal)
endif()

# The C ABI a native frontend binds to. Kept in its own target so a binding
# generator or a foreign linker sees exactly one header and one archive.
add_library(wirelab_ffi STATIC
  src/wirelab_ffi.cpp
  include/wirelab/wirelab_ffi.h)
target_link_libraries(wirelab_ffi PUBLIC wirelab_session)
target_compile_features(wirelab_ffi PUBLIC cxx_std_17)

# The GPUI frontend. CMake stays the master build: it produces the archives and
# then hands cargo their directories, so `cmake --build --target wirelab-desktop`
# is the one command that builds the whole application.
#
# A custom target rather than Corrosion: cargo already tracks its own inputs,
# this needs no configure-time download, and target ordering is expressed just
# as precisely with add_dependencies.
option(PROJECT_BUILD_DESKTOP "Build the GPUI desktop frontend (requires a Rust toolchain)" OFF)
if(PROJECT_BUILD_DESKTOP)
  find_program(CARGO_EXECUTABLE cargo)
  if(NOT CARGO_EXECUTABLE)
    message(FATAL_ERROR "PROJECT_BUILD_DESKTOP is enabled but cargo was not found. Install a Rust toolchain or set CARGO_EXECUTABLE.")
  endif()

  set(WIRELAB_DESKTOP_LIB_DIRS
    "$<TARGET_FILE_DIR:wirelab_ffi>;$<TARGET_FILE_DIR:wirelab_session>;$<TARGET_FILE_DIR:wirelab_backends>;$<TARGET_FILE_DIR:${PROJECT_NAME}>")
  set(WIRELAB_DESKTOP_PROFILE_FLAG "")
  set(WIRELAB_DESKTOP_PROFILE_DIR "debug")
  if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(WIRELAB_DESKTOP_PROFILE_FLAG "--release")
    set(WIRELAB_DESKTOP_PROFILE_DIR "release")
  endif()
  if(TARGET wirelab_metal)
    list(APPEND WIRELAB_DESKTOP_LIB_DIRS "$<TARGET_FILE_DIR:wirelab_metal>")
    set(WIRELAB_DESKTOP_METAL 1)
  else()
    set(WIRELAB_DESKTOP_METAL 0)
  endif()
  if(TARGET wirelab_cuda)
    list(APPEND WIRELAB_DESKTOP_LIB_DIRS "$<TARGET_FILE_DIR:wirelab_cuda>")
    set(WIRELAB_DESKTOP_CUDA 1)
  else()
    set(WIRELAB_DESKTOP_CUDA 0)
  endif()
  string(REPLACE ";" "$<SEMICOLON>" WIRELAB_DESKTOP_LIB_DIRS_ARG "${WIRELAB_DESKTOP_LIB_DIRS}")

  set(WIRELAB_DESKTOP_CARGO_ENV
    "WIRELAB_LIB_DIRS=${WIRELAB_DESKTOP_LIB_DIRS_ARG}"
    "WIRELAB_LINK_METAL=${WIRELAB_DESKTOP_METAL}"
    "WIRELAB_LINK_CUDA=${WIRELAB_DESKTOP_CUDA}"
    "CARGO_TARGET_DIR=${CMAKE_BINARY_DIR}/cargo")

  add_custom_target(wirelab-desktop ALL
    COMMAND ${CMAKE_COMMAND} -E env ${WIRELAB_DESKTOP_CARGO_ENV}
      ${CARGO_EXECUTABLE} build ${WIRELAB_DESKTOP_PROFILE_FLAG}
      --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/gui/Cargo.toml"
    BYPRODUCTS "${CMAKE_BINARY_DIR}/cargo/${WIRELAB_DESKTOP_PROFILE_DIR}/wirelab-desktop"
    COMMENT "Building the GPUI desktop frontend"
    VERBATIM
    USES_TERMINAL)
  add_dependencies(wirelab-desktop wirelab_ffi)

  if(APPLE)
    # macOS reads the application name, the icon and the retina flag from the
    # bundle, not from the process, so an unbundled binary shows up as
    # "wirelab-desktop" with a generic icon.
    set(WIRELAB_DESKTOP_BUNDLE "${CMAKE_BINARY_DIR}/bin/WireLab.app")
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/apps/resources/Info.plist.in"
      "${CMAKE_BINARY_DIR}/desktop/Info.plist"
      @ONLY)
    add_custom_command(TARGET wirelab-desktop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E make_directory "${WIRELAB_DESKTOP_BUNDLE}/Contents/MacOS"
      COMMAND ${CMAKE_COMMAND} -E make_directory "${WIRELAB_DESKTOP_BUNDLE}/Contents/Resources"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/desktop/Info.plist"
        "${WIRELAB_DESKTOP_BUNDLE}/Contents/Info.plist"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/apps/resources/WireLab.icns"
        "${WIRELAB_DESKTOP_BUNDLE}/Contents/Resources/WireLab.icns"
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_BINARY_DIR}/cargo/${WIRELAB_DESKTOP_PROFILE_DIR}/wirelab-desktop"
        "${WIRELAB_DESKTOP_BUNDLE}/Contents/MacOS/wirelab-desktop"
      COMMENT "Assembling WireLab.app"
      VERBATIM)
  endif()

  # The binding is only trustworthy with the layout check run, so the test suite
  # runs cargo test rather than trusting a clean compile.
  add_test(NAME wirelab_desktop_bindings
    COMMAND ${CMAKE_COMMAND} -E env ${WIRELAB_DESKTOP_CARGO_ENV}
      ${CARGO_EXECUTABLE} test ${WIRELAB_DESKTOP_PROFILE_FLAG}
      --manifest-path "${CMAKE_CURRENT_SOURCE_DIR}/gui/Cargo.toml")
endif()

set_target_properties(
  vport vswitch wirelab_bench wirelab_pcap
  PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<CONFIG>"
)

verbose_message("Added vport and vswitch executable targets.")





add_clang_format_target()





include(GNUInstallDirs)
install(
  TARGETS
  ${PROJECT_NAME}
  vport
  vswitch
  wirelab_bench
  wirelab_pcap
  EXPORT
  ${PROJECT_NAME}Targets
  LIBRARY DESTINATION
  ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION
  ${CMAKE_INSTALL_BINDIR}
  ARCHIVE DESTINATION
  ${CMAKE_INSTALL_LIBDIR}
  INCLUDES DESTINATION
  include
  PUBLIC_HEADER DESTINATION
  include
)

# wirelab-desktop is a custom target driving cargo, so what is installed is the
# artifact it leaves behind: the assembled bundle on macOS, the bare executable
# everywhere else.
if(PROJECT_BUILD_DESKTOP)
  if(APPLE)
    install(
      DIRECTORY "${WIRELAB_DESKTOP_BUNDLE}"
      DESTINATION ${CMAKE_INSTALL_BINDIR}
      USE_SOURCE_PERMISSIONS
    )
  else()
    install(
      PROGRAMS "${CMAKE_BINARY_DIR}/cargo/${WIRELAB_DESKTOP_PROFILE_DIR}/wirelab-desktop"
      DESTINATION ${CMAKE_INSTALL_BINDIR}
    )
  endif()
endif()

install(
  EXPORT
  ${PROJECT_NAME}Targets
  FILE
  ${PROJECT_NAME}Targets.cmake
  NAMESPACE
  ${PROJECT_NAME}::
  DESTINATION
  ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)





configure_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/version.hpp.in
  include/${PROJECT_NAME_LOWERCASE}/version.hpp
  @ONLY
)

install(
  FILES
  ${CMAKE_CURRENT_BINARY_DIR}/include/${PROJECT_NAME_LOWERCASE}/version.hpp
  DESTINATION
  include/${PROJECT_NAME_LOWERCASE}
)





install(
  DIRECTORY
  include/${PROJECT_NAME_LOWERCASE}
  DESTINATION
  include
)

verbose_message("Install targets successfully built. Install with `cmake --build <build_directory> --target install --config <build_config>`.")





include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  ${PROJECT_NAME}ConfigVersion.cmake
  VERSION
  ${PROJECT_VERSION}
  COMPATIBILITY
  SameMajorVersion
)

configure_package_config_file(
  ${CMAKE_CURRENT_LIST_DIR}/cmake/${PROJECT_NAME}Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  INSTALL_DESTINATION
  ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)

install(
  FILES
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
  DESTINATION
  ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}
)





if(${PROJECT_NAME}_GENERATE_EXPORT_HEADER)
  include(GenerateExportHeader)
  generate_export_header(${PROJECT_NAME})
  install(
    FILES
    ${PROJECT_BINARY_DIR}/${PROJECT_NAME_LOWERCASE}_export.h
    DESTINATION
    include
  )

  message(STATUS "Generated the export header `${PROJECT_NAME_LOWERCASE}_export.h` and installed it.")
endif()

message(STATUS "Finished building requirements for installing the package.\n")





if(${PROJECT_NAME}_ENABLE_UNIT_TESTING)
  enable_testing()
  message(STATUS "Build unit tests for the project. Tests should always be found in the test folder\n")
  add_subdirectory(tests)
endif()
