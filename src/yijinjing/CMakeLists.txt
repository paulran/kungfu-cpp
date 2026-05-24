add_library(kf_yijinjing STATIC
    journal/page.cpp
    journal/writer.cpp
    journal/reader.cpp
    io/locator.cpp
    nanomsg/socket.cpp
    practice/hero.cpp
    practice/apprentice.cpp
)
target_include_directories(kf_yijinjing PUBLIC ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_yijinjing
    PUBLIC kf_common kf_longfist nng rxcpp spdlog::spdlog
)
