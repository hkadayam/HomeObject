cmake_minimum_required (VERSION 3.11)

find_package(Threads QUIET REQUIRED)
find_package(sisl QUIET REQUIRED)
find_package(homestore QUIET REQUIRED)
find_package(nuraft_mesg QUIET REQUIRED)

find_package(GTest QUIET REQUIRED)

link_directories(${spdk_LIB_DIRS} ${dpdk_LIB_DIRS})

set (COMMON_DEPS
        homestore::homestore
        nuraft_mesg::nuraft_mesg
        sisl::sisl
    )

set(COMMON_TEST_DEPS
        home_replication_mock
        ${COMMON_DEPS}
        GTest::gmock
        ${spdk_LIBRARY_LIST}
        ${dpdk_LIBRARY_LIST}
    )
add_flags("-Wall -Wextra -Werror -Wpedantic")

include_directories (BEFORE .)
include_directories (BEFORE include/)
include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/include)
include_directories(BEFORE ${CMAKE_CURRENT_BINARY_DIR}/lib)

add_subdirectory(lib)
add_subdirectory(mocks)
