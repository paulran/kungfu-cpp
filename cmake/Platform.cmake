set(CMAKE_CXX_STANDARD 20)

if (UNIX)
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIC")
  set(CMAKE_CXX_FLAGS_DEBUG "-g")
  set(CMAKE_CXX_FLAGS_RELEASE "-g")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/${CMAKE_BUILD_TYPE})
  set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
  set(COMPILER_OPTIMIZE_ON_OPTIONS "-O0")
  set(COMPILER_OPTIMIZE_OFF_OPTIONS "-O0")
endif ()

if (UNIX AND NOT APPLE)
  set(KFC_INSTALL_RPATH
      "$ORIGIN"
      "$ORIGIN/../../"
      )
  set(CMAKE_INSTALL_RPATH "${KFC_INSTALL_RPATH}")
endif ()

if (APPLE)
  set(KFC_INSTALL_RPATH
      "@loader_path"
      "@loader_path/../../"
      "@executable_path/../../../../Resources/kfc"
      )
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations -Wno-unqualified-std-cast-call -Wno-unused-value")
  set(CMAKE_INSTALL_RPATH "${KFC_INSTALL_RPATH}")
  set(CMAKE_MACOSX_RPATH ON)
endif ()

if (MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP /utf-8 /permissive- /bigobj /W0 /Zc:__cplusplus")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /IGNORE:4199")
  set(CMAKE_MODULE_LINKER_FLAGS "${CMAKE_MODULE_LINKER_FLAGS} /IGNORE:4199")
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
  add_compile_definitions(HAVE_SNPRINTF)
  add_compile_definitions(_SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
  set(COMPILER_OPTIMIZE_ON_OPTIONS "/O2")
  set(COMPILER_OPTIMIZE_OFF_OPTIONS "/Od")
endif ()

if (${CMAKE_CXX_COMPILER_ID} MATCHES GNU)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftemplate-backtrace-limit=0 -Wno-address-of-packed-member -Wno-deprecated")
endif ()

macro(enable_windows_export_all_symbols)
  if (MSVC)
      set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
  endif ()
endmacro()

macro(add_library_object OBJ_NAME SRC_FILES COMPILER_OPTIMIZE_OPTIONS OUTPUT_DIR)
  add_library(${OBJ_NAME} OBJECT ${SRC_FILES})
  set_target_properties(${OBJ_NAME} PROPERTIES POSITION_INDEPENDENT_CODE ON)
  if (NOT ${OUTPUT_DIR} STREQUAL "")
    set_target_properties(${OBJ_NAME} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY ${OUTPUT_DIR})
  endif ()
  if (NOT ${COMPILER_OPTIMIZE_OPTIONS} STREQUAL "")
    target_compile_options(${OBJ_NAME} PRIVATE $<$<CONFIG:Release>:${COMPILER_OPTIMIZE_OPTIONS}>)
  endif ()
endmacro()

add_compile_definitions(HAVE_USLEEP=1)
add_compile_definitions(SPDLOG_ACTIVE_LEVEL=SPDLOG_LEVEL_INFO)
add_compile_definitions(SPDLOG_NO_NAME)
add_compile_definitions(SPDLOG_NO_ATOMIC_LEVELS)
add_compile_definitions(SPDLOG_FMT_EXTERNAL=ON)

include_directories(${CMAKE_SOURCE_DIR}/3rdparty/fmt/include)

set(KUNGFU_BUILD_DIR ${CMAKE_BINARY_DIR})
set(LIBKUNGFU_NAME kungfu)