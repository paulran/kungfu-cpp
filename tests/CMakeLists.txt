add_executable(kf_tests
    test_hash.cpp
    test_mmap.cpp
    test_frame.cpp
    test_page.cpp
    test_journal_rw.cpp
    test_locator.cpp
    test_longfist.cpp
    test_practice.cpp
    test_integration.cpp
)
target_link_libraries(kf_tests PRIVATE kf_yijinjing kf_longfist GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(kf_tests)

# Phase 2 tests
add_executable(kf_tests_phase2
    test_longfist_phase2.cpp
    test_config.cpp
    test_store.cpp
    test_ledger.cpp
    test_supervisor.cpp
    ${PROJECT_SOURCE_DIR}/src/services/cached/store.cpp
    ${PROJECT_SOURCE_DIR}/src/services/ledger/book.cpp
    ${PROJECT_SOURCE_DIR}/src/services/master/supervisor.cpp
)
target_include_directories(kf_tests_phase2 PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_tests_phase2 PRIVATE
    kf_yijinjing kf_common kf_longfist
    sqlite_orm spdlog::spdlog
    GTest::gtest_main
)
if(WIN32)
    target_link_libraries(kf_tests_phase2 PRIVATE kernel32)
endif()
gtest_discover_tests(kf_tests_phase2)
