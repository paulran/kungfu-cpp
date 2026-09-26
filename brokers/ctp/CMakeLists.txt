# CTP broker extension, built as an independently loadable shared library
# (kf_ctp.dll on Windows / libkf_ctp.so on Linux). kf_md and kf_td load it at
# runtime through kungfu::wingchun::plugin::Library and resolve the
# kf_create_trader / kf_create_market_data factory symbols defined via the
# macros in kungfu/wingchun/extension.h.
set(CTP_EXTENSION_SOURCES
    src/ctp_ext_api.cpp
    src/ctp_common.cpp
    src/ctp_md_spi.cpp
    src/ctp_trader_spi.cpp
    src/market_data_ctp.cpp
    src/trader_ctp.cpp
)

add_library(kf_ctp SHARED ${CTP_EXTENSION_SOURCES})

target_include_directories(kf_ctp PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/api
)

set_target_properties(kf_ctp PROPERTIES LINKER_LANGUAGE CXX)

# kungfu transitively provides the include dirs of spdlog/fmt/nlohmann_json
# and the broker base classes the extension implements.
target_link_libraries(kf_ctp PRIVATE ${LIBKUNGFU_NAME})

# CTP ships prebuilt shared libraries without the usual "lib" prefix
# (thostmduserapi_se.so / thosttraderapi_se.so). Link them by exact filename
# and embed their directory in the runtime search path so libkf_ctp.so
# resolves the dependencies when dlopen()ed by kf_md / kf_td.
if(UNIX AND NOT APPLE)
  set(CTP_LIB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/lib/linux)
  # The CTP libraries have no "lib" prefix, so plain full paths get mangled
  # into -l flags the linker cannot resolve; -l:<file> links the exact file.
  target_link_directories(kf_ctp PRIVATE ${CTP_LIB_DIR})
  target_link_libraries(kf_ctp PRIVATE
      -l:thostmduserapi_se.so
      -l:thosttraderapi_se.so
  )
  set_target_properties(kf_ctp PROPERTIES
      BUILD_RPATH "${CTP_LIB_DIR}"
      INSTALL_RPATH "${CTP_LIB_DIR}"
  )
elseif(MSVC)
  # Requires thostmduserapi_se.lib / thosttraderapi_se.lib (and the matching
  # DLLs at runtime) dropped into lib/ alongside the headers.
  target_link_directories(kf_ctp PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib/win64)
  target_link_libraries(kf_ctp PRIVATE thostmduserapi_se thosttraderapi_se)
endif()
