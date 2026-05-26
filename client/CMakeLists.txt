cmake_minimum_required(VERSION 3.20)
project(kungfu_client VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

set(SOURCES
    src/main.cpp
    src/app/application.cpp
    src/api/rest_client.cpp
    src/api/ws_client.cpp
    src/models/order_model.cpp
    src/models/trade_model.cpp
    src/models/position_model.cpp
    src/models/quote_model.cpp
    src/models/process_model.cpp
    src/widgets/login_dialog.cpp
    src/widgets/main_window.cpp
    src/widgets/market_widget.cpp
    src/widgets/order_entry_widget.cpp
    src/widgets/order_widget.cpp
    src/widgets/trade_widget.cpp
    src/widgets/position_widget.cpp
    src/widgets/asset_widget.cpp
    src/widgets/strategy_widget.cpp
    src/widgets/system_widget.cpp
)

set(HEADERS
    src/app/application.h
    src/api/api_types.h
    src/api/rest_client.h
    src/api/ws_client.h
    src/models/order_model.h
    src/models/trade_model.h
    src/models/position_model.h
    src/models/quote_model.h
    src/models/process_model.h
    src/widgets/login_dialog.h
    src/widgets/main_window.h
    src/widgets/market_widget.h
    src/widgets/order_entry_widget.h
    src/widgets/order_widget.h
    src/widgets/trade_widget.h
    src/widgets/position_widget.h
    src/widgets/asset_widget.h
    src/widgets/strategy_widget.h
    src/widgets/system_widget.h
    src/utils/formatters.h
)

add_executable(kungfu_client ${SOURCES} ${HEADERS})

target_include_directories(kungfu_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(kungfu_client PRIVATE
    Qt6::Widgets
    Qt6::Network
)

if(WIN32)
    set_target_properties(kungfu_client PROPERTIES WIN32_EXECUTABLE TRUE)
endif()
