add_executable(kf_producer producer.cpp)
target_include_directories(kf_producer PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_producer PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)

add_executable(kf_consumer consumer.cpp)
target_include_directories(kf_consumer PRIVATE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_consumer PRIVATE ${LIBKUNGFU_NAME} spdlog::spdlog)
