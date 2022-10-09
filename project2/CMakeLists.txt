cmake_minimum_required(VERSION 3.21)

project(AtomicSnapshot)

include(CTest)

# C++ settings
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# Options
option(BUILD_TESTS "Build unit tests" ON)

# Sources
set(AS_SOURCES_DIR src)
set(AS_SOURCES
    ${AS_SOURCES_DIR}/wfa.cpp
    )

# Headers
set(AS_HEADERS_DIR include)
set(AS_HEADERS
    ${AS_HEADERS_DIR}/wfa.hpp
    )

include_directories(${AS_HEADERS_DIR})

add_library(wfa STATIC ${AS_SOURCES} ${AS_HEADERS})

add_executable(run ${AS_SOURCES_DIR}/main.cpp)
target_link_libraries(run wfa)

if(BUILD_TESTS)
    add_subdirectory(test)
endif()
