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
