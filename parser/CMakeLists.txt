# CodeXray 解析引擎：C++17 + Clang libtooling + SQLite3
# 参考：doc/01-解析引擎 解析引擎详细功能与架构设计 §2.2

cmake_minimum_required(VERSION 3.16)
project(codexray-parser VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SQLite3（必需）
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(SQLite3 QUIET sqlite3)
endif()
if(NOT SQLite3_FOUND)
  find_path(SQLite3_INCLUDE_DIR sqlite3.h)
  find_library(SQLite3_LIBRARY sqlite3)
  if(SQLite3_INCLUDE_DIR AND SQLite3_LIBRARY)
    set(SQLite3_FOUND TRUE)
    set(SQLite3_INCLUDE_DIRS ${SQLite3_INCLUDE_DIR})
    set(SQLite3_LIBRARIES ${SQLite3_LIBRARY})
  endif()
endif()
if(NOT SQLite3_FOUND)
  message(FATAL_ERROR "SQLite3 not found. Install libsqlite3-dev or sqlite3.")
endif()

# LLVM/Clang：优先使用 parser/third_party/llvm，否则使用环境 LLVM_DIR
set(LLVM_FOUND FALSE)
set(CODEXRAY_LLVM_THIRD_PARTY "${CMAKE_CURRENT_SOURCE_DIR}/third_party/llvm")
if(EXISTS "${CODEXRAY_LLVM_THIRD_PARTY}/lib/cmake/llvm/LLVMConfig.cmake")
  set(LLVM_DIR "${CODEXRAY_LLVM_THIRD_PARTY}/lib/cmake/llvm" CACHE PATH "LLVM config (third_party)" FORCE)
  if(EXISTS "${CODEXRAY_LLVM_THIRD_PARTY}/lib/cmake/clang/ClangConfig.cmake")
    set(Clang_DIR "${CODEXRAY_LLVM_THIRD_PARTY}/lib/cmake/clang" CACHE PATH "Clang config (third_party)" FORCE)
  endif()
  message(STATUS "Using LLVM from third_party: ${LLVM_DIR}")
endif()
if(NOT LLVM_DIR AND DEFINED ENV{LLVM_DIR})
  set(LLVM_DIR "$ENV{LLVM_DIR}" CACHE PATH "LLVM config path")
endif()
find_package(LLVM QUIET CONFIG)
if(LLVM_FOUND)
  find_package(Clang QUIET CONFIG)
  if(Clang_FOUND)
    message(STATUS "Clang found: ${CLANG_INCLUDE_DIRS}")
  endif()
endif()

# nlohmann/json（可选，compile_commands 解析用）
find_path(JSON_INCLUDE_DIR nlohmann/json.hpp
  HINTS /usr/include /usr/local/include
)
if(JSON_INCLUDE_DIR)
  set(JSON_FOUND TRUE)
  set(JSON_INCLUDE_DIRS ${JSON_INCLUDE_DIR})
endif()

# 可执行体
add_executable(codexray-parser
  src/main.cc
  src/common/logger.cc
  src/common/error_code.cc
  src/common/path_util.cc
  src/db/connection.cc
  src/db/schema.cc
  src/compile_commands/load.cc
  src/db/writer/writer.cc
  src/db/reader/reader.cc
  src/history/history.cc
  src/incremental/incremental.cc
  src/scheduler/pool.cc
  src/ast/call_graph/action.cc
  src/ast/function_pointer/analyzer.cc
  src/ast/class_relation/action.cc
  src/ast/data_flow/action.cc
  src/ast/control_flow/action.cc
  src/driver/orchestrator.cc
  src/query/json_output.cc
  src/cli/parse_args.cc
  # src/db/schema.cc
  # src/compile_commands/load.cc
  # src/db/writer/...
  # src/db/reader/...
  # src/history/...
  # src/incremental/...
  # src/scheduler/pool.cc
  # src/ast/call_graph/...
  # src/ast/function_pointer/...
  # src/ast/class_relation/...
  # src/ast/data_flow/...
  # src/ast/control_flow/...
  # src/driver/orchestrator.cc
  # src/query/...
  # src/cli/parse_args.cc
)

target_include_directories(codexray-parser PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/src
  ${SQLite3_INCLUDE_DIRS}
)
target_link_libraries(codexray-parser PRIVATE ${SQLite3_LIBRARIES})

if(LLVM_FOUND AND Clang_FOUND)
  llvm_map_components_to_libnames(llvm_libs support core)
  target_include_directories(codexray-parser PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
  target_compile_definitions(codexray-parser PRIVATE CODEXRAY_HAVE_CLANG)
  target_link_libraries(codexray-parser PRIVATE ${LLVM_LIBS}
    clangAnalysis clangIndex clangTooling clangToolingCore clangFrontend clangDriver
    clangSerialization clangParse clangSema
    clangAST clangLex clangBasic)
endif()

if(JSON_FOUND)
  target_include_directories(codexray-parser PRIVATE ${JSON_INCLUDE_DIRS})
endif()

# 安装到 bin（供主仓库 getParserPath 使用）
install(TARGETS codexray-parser RUNTIME DESTINATION bin)
