cmake_minimum_required(VERSION 3.16)

if(${CMAKE_SOURCE_DIR} STREQUAL ${CMAKE_BINARY_DIR})
    message(FATAL_ERROR "Prevented in-tree built. Please create a build directory outside of the source code and call cmake from there")
endif()

project(extract-strings CXX)

set(CMAKE_CXX_STANDARD 17)
  

# Find CMake file for Clang
find_package(Clang REQUIRED CONFIG)
add_definitions(${CLANG_DEFINITIONS})
include_directories(${CLANG_INCLUDE_DIRS})


find_package(LLVM REQUIRED CONFIG)
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})
 
  
# include(AddLLVM)
#include(AddClang)

add_executable(extract-strings main.cpp)

target_include_directories(extract-strings PRIVATE Clang)


target_link_libraries(extract-strings
  PRIVATE
  clangParse
  clangAST
  clangDriver
  clangFrontend
  clangBasic
  clangTooling
  )

# testing
include(FetchContent)
FetchContent_Declare(test SOURCE_DIR ${CMAKE_CURRENT_LIST_DIR}/test CMAKE_ARGS -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
FetchContent_MakeAvailable(test)

enable_testing() # Enable CTest
add_test(NAME MyTest COMMAND extract-strings -p ${CMAKE_CURRENT_BINARY_DIR}/test-build --func _T ${CMAKE_CURRENT_LIST_DIR}/test/test1.cpp)