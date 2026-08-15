cmake_minimum_required(VERSION 3.21)





project(
  "Project"
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

option(PROJECT_ENABLE_CUDA "Build the optional WireLab CUDA packet parser" OFF)
if(PROJECT_ENABLE_CUDA)
  enable_language(CUDA)
  find_package(CUDAToolkit REQUIRED)
  add_library(project_cuda src/cuda_packet_parser.cu include/project/cuda_packet_parser.hpp)
  target_compile_features(project_cuda PUBLIC cxx_std_17)
  target_include_directories(project_cuda PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(project_cuda PUBLIC ${PROJECT_NAME} CUDA::cudart)
  target_compile_definitions(project_cuda PUBLIC PROJECT_HAS_CUDA=1)
  set_target_properties(project_cuda PROPERTIES
    CUDA_STANDARD 17
    CUDA_STANDARD_REQUIRED ON
    CUDA_SEPARABLE_COMPILATION ON)
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
set_project_warnings(${PROJECT_NAME})

verbose_message("Applied compiler warnings. Using standard ${CMAKE_CXX_STANDARD}.\n")





include(cmake/Doxygen.cmake)


































set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")

verbose_message("Successfully added all dependencies and linked against them.")






target_include_directories(
  ${PROJECT_NAME}
  PUBLIC
  $<INSTALL_INTERFACE:include>
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
)

message(STATUS "Finished setting up include directories.")





add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

verbose_message("Project is now aliased as ${PROJECT_NAME}::${PROJECT_NAME}.\n")





add_executable(vport src/vport_main.cpp)
target_link_libraries(vport PRIVATE ${PROJECT_NAME})
target_compile_features(vport PRIVATE cxx_std_17)

add_executable(vswitch src/vswitch_main.cpp)
target_link_libraries(vswitch PRIVATE ${PROJECT_NAME})
target_compile_features(vswitch PRIVATE cxx_std_17)

add_executable(wirelab_bench src/wirelab_bench.cpp)
target_link_libraries(wirelab_bench PRIVATE ${PROJECT_NAME})
target_compile_features(wirelab_bench PRIVATE cxx_std_17)
if(TARGET project_cuda)
  # CMake needs a CUDA translation unit in this executable to schedule the
  # device-link step required by the separably compiled static CUDA backend.
  set_source_files_properties(src/wirelab_bench.cpp PROPERTIES LANGUAGE CUDA)
  target_link_libraries(wirelab_bench PRIVATE project_cuda)
  set_target_properties(wirelab_bench PROPERTIES
    CUDA_SEPARABLE_COMPILATION ON
    CUDA_RESOLVE_DEVICE_SYMBOLS ON)
endif()

option(PROJECT_BUILD_WIRELAB_GUI "Build the Qt 6 WireLab frontend" OFF)
if(PROJECT_BUILD_WIRELAB_GUI)
  find_package(Qt6 6.5 QUIET COMPONENTS Quick QuickControls2)
  if(Qt6_FOUND)
    qt_add_executable(wirelab_gui
      apps/wirelab_gui.cpp
      src/wirelab_view_model.cpp
      include/project/wirelab_view_model.hpp)
    set_target_properties(wirelab_gui PROPERTIES AUTOMOC ON AUTORCC ON)
    qt_add_qml_module(wirelab_gui
      RESOURCE_PREFIX "/qt/qml"
      URI WireLab
      VERSION 1.0
      QML_FILES src/gui/qml/Main.qml)
    target_link_libraries(wirelab_gui PRIVATE
      ${PROJECT_NAME}
      Qt6::Quick
      Qt6::QuickControls2)
    if(TARGET project_cuda)
      target_link_libraries(wirelab_gui PRIVATE project_cuda)
    endif()
    target_compile_features(wirelab_gui PRIVATE cxx_std_17)
  else()
    message(WARNING "PROJECT_BUILD_WIRELAB_GUI is enabled, but Qt 6.5 Quick and QuickControls2 were not found.")
  endif()
endif()
set_target_properties(
  vport vswitch wirelab_bench
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

if(TARGET wirelab_gui)
  install(
    TARGETS wirelab_gui
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
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
