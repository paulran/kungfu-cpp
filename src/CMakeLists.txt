add_subdirectory(common)
add_subdirectory(yijinjing)

add_library(kf_longfist INTERFACE)
target_include_directories(kf_longfist INTERFACE ${PROJECT_SOURCE_DIR}/include)
target_link_libraries(kf_longfist INTERFACE boost_hana nlohmann_json::nlohmann_json)

add_subdirectory(wingchun)
add_subdirectory(services)
