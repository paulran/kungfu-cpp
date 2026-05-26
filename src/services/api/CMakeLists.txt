add_executable(kf_api
    main.cpp
    api_gateway.cpp
    http_handler.cpp
    ws_handler.cpp
    ws_nng.cpp
    jwt.cpp
)
target_include_directories(kf_api PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(kf_api PRIVATE
    kf_yijinjing
    kf_common
    kf_longfist
    spdlog::spdlog
)
