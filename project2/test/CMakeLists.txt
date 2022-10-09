# GoogleTest
include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/e4717df71a4f45bf9f0ac88c6cd9846a0bc248dd.zip)

set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

set(AS_TESTS
    wfa_test.cpp
    )

add_executable(wfa_test ${AS_TESTS})
target_link_libraries(
    wfa_test
    wfa
    gtest_main
    )

include(GoogleTest)
gtest_discover_tests(wfa_test)
