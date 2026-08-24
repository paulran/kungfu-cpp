add_executable(kf_producer producer.cpp)
target_include_directories(kf_producer PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_producer PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_consumer consumer.cpp)
target_include_directories(kf_consumer PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_consumer PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

# Interactive demo client for apps/api.cpp (kf_api). Connects over nng pair1,
# sends JSON requests and decodes the binary push frames.
add_executable(kf_api_demo api_demo.cpp)
target_link_libraries(kf_api_demo PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)
if(WIN32)
    target_link_libraries(kf_api_demo PRIVATE ws2_32)
endif()
