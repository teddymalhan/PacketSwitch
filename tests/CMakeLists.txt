cmake_minimum_required(VERSION 3.15)





project(
  ${CMAKE_PROJECT_NAME}Tests
  LANGUAGES CXX
)

verbose_message("Adding tests under ${CMAKE_PROJECT_NAME}Tests...")

if(${CMAKE_PROJECT_NAME}_USE_GTEST)
  find_package(GTest QUIET)

  if(NOT GTest_FOUND)
    include(FetchContent)
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_Declare(
      googletest
      URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
      URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    FetchContent_MakeAvailable(googletest)
  endif()

  if(TARGET GTest::gtest)
    set(PROJECT_GTEST_LIBRARIES GTest::gtest GTest::gtest_main)
  else()
    set(PROJECT_GTEST_LIBRARIES GTest::GTest GTest::Main)
  endif()

  if(${CMAKE_PROJECT_NAME}_USE_GOOGLE_MOCK)
    list(APPEND PROJECT_GTEST_LIBRARIES GTest::gmock GTest::gmock_main)
  endif()
endif()

if(${CMAKE_PROJECT_NAME}_BUILD_EXECUTABLE)
  set(${CMAKE_PROJECT_NAME}_TEST_LIB ${CMAKE_PROJECT_NAME}_LIB)
else()
  set(${CMAKE_PROJECT_NAME}_TEST_LIB ${CMAKE_PROJECT_NAME})
endif()

if(${CMAKE_PROJECT_NAME}_ENABLE_CODE_COVERAGE)
  target_compile_options(
    ${${CMAKE_PROJECT_NAME}_TEST_LIB}
    PUBLIC -O0 -g --coverage -fprofile-update=atomic
  )
  target_link_options(${${CMAKE_PROJECT_NAME}_TEST_LIB} PUBLIC --coverage)
  verbose_message("Code coverage is enabled and provided with GCC.")
endif()

if(WIN32)
  list(REMOVE_ITEM test_sources src/sys_utils_test.cpp src/tap_device_test.cpp)
endif()

foreach(file ${test_sources})
  string(REGEX REPLACE "(.*/)([a-zA-Z0-9_ ]+)(\.cpp)" "\\2" test_name ${file}) 
  add_executable(${test_name}_Tests ${file})





  target_compile_features(${test_name}_Tests PUBLIC cxx_std_17)






  if(${CMAKE_PROJECT_NAME}_USE_GTEST)
    target_link_libraries(
      ${test_name}_Tests
      PUBLIC
        ${PROJECT_GTEST_LIBRARIES}
        ${${CMAKE_PROJECT_NAME}_TEST_LIB}
    )
  elseif(${CMAKE_PROJECT_NAME}_USE_CATCH2)
    find_package(Catch2 REQUIRED)
    target_link_libraries(
      ${test_name}_Tests
      PUBLIC
        Catch2::Catch2
        ${${CMAKE_PROJECT_NAME}_TEST_LIB}
    )
  else()
    message(FATAL_ERROR "Unknown testing library. Please setup your desired unit testing library by using `target_link_libraries`.")  
  endif()





  add_test(
    NAME
      ${test_name}
    COMMAND
      ${test_name}_Tests
  )
endforeach()

verbose_message("Finished adding unit tests for ${CMAKE_PROJECT_NAME}.")
