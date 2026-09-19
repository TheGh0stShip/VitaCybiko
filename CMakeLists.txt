cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE AND DEFINED ENV{VITASDK})
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
endif()

project(VitaCybiko C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CYBIKO_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/cybiko-c-emulator")

add_library(cybiko_core STATIC
  ${CYBIKO_VENDOR_DIR}/src/core/machine.c
  ${CYBIKO_VENDOR_DIR}/src/core/dataflash.c
  ${CYBIKO_VENDOR_DIR}/src/core/memory.c
  ${CYBIKO_VENDOR_DIR}/src/core/hd66421.c
  ${CYBIKO_VENDOR_DIR}/src/core/timer8.c
  ${CYBIKO_VENDOR_DIR}/src/core/timer16.c
  ${CYBIKO_VENDOR_DIR}/src/core/rtc.c
  ${CYBIKO_VENDOR_DIR}/src/core/speaker.c
  ${CYBIKO_VENDOR_DIR}/src/core/keyboard.c
  ${CYBIKO_VENDOR_DIR}/src/core/h8s_cpu.c
  ${CYBIKO_VENDOR_DIR}/src/core/address_bus.c
  ${CYBIKO_VENDOR_DIR}/src/core/cfs.c
  ${CYBIKO_VENDOR_DIR}/src/core/emulator.c
)
target_include_directories(cybiko_core PUBLIC ${CYBIKO_VENDOR_DIR}/src)

add_library(vitacybiko_input STATIC src/frontend/input.c)
target_include_directories(vitacybiko_input PUBLIC src ${CYBIKO_VENDOR_DIR}/src)

option(CYBIKO_TRACE "Enable CPU instruction tracing in the emulation core" OFF)
if(CYBIKO_TRACE)
  target_compile_definitions(cybiko_core PUBLIC CYBIKO_TRACE=1)
endif()

if(VITA)
  if(NOT DEFINED VITASDK)
    if(DEFINED ENV{VITASDK})
      set(VITASDK "$ENV{VITASDK}")
    else()
      message(FATAL_ERROR "Please define VITASDK to point to your Vita SDK path.")
    endif()
  endif()
  set(ENV{VITASDK} "${VITASDK}")

  include("${VITASDK}/share/vita.cmake" REQUIRED)
  find_package(PkgConfig REQUIRED)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  pkg_check_modules(SDL2_GFX REQUIRED IMPORTED_TARGET SDL2_gfx)

  set(VITA_APP_NAME "VitaCybiko")
  set(VITA_TITLEID  "VCYB00001")
  set(VITA_VERSION  "01.05")

  add_executable(${PROJECT_NAME}
    src/vita/main.c
  )
  option(VITACYBIKO_LTO "Inline core memory/CPU helpers across translation units" ON)
  if(VITACYBIKO_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT ipo_supported OUTPUT ipo_error LANGUAGES C)
    if(ipo_supported)
      set_property(TARGET cybiko_core vitacybiko_input ${PROJECT_NAME}
                   PROPERTY INTERPROCEDURAL_OPTIMIZATION TRUE)
    else()
      message(WARNING "Link-time optimization unavailable: ${ipo_error}")
    endif()
  endif()
  target_link_libraries(${PROJECT_NAME}
    cybiko_core
    vitacybiko_input
    PkgConfig::SDL2_GFX
  )
  target_compile_definitions(${PROJECT_NAME} PRIVATE VITA=1)

  vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
  vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
    FILE LICENSE licenses/VitaCybiko.txt
    FILE THIRD_PARTY_NOTICES.md licenses/THIRD_PARTY_NOTICES.md
    FILE third_party/cybiko-c-emulator/LICENSE licenses/cybiko-core.txt
    FILE sce_sys/icon0.png sce_sys/icon0.png
    FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
    FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
    FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  )
  add_custom_command(TARGET ${PROJECT_NAME}.vpk-vpk POST_BUILD
    COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tools/repack_vpk_store.py
            ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}.vpk
    COMMENT "Repacking VitaCybiko.vpk with stored ZIP entries for VitaShell"
    VERBATIM
  )
else()
  option(VITACYBIKO_FRONTEND_TESTS "Test Vita frontend using host SDL2" OFF)
  add_executable(cybiko-smoke src/host/smoke.c)
  target_link_libraries(cybiko-smoke cybiko_core)

  include(CTest)
  if(BUILD_TESTING)
    if(VITACYBIKO_FRONTEND_TESTS)
      find_package(PkgConfig REQUIRED)
      pkg_check_modules(HOST_SDL REQUIRED IMPORTED_TARGET sdl2 SDL2_gfx)
      add_executable(test_frontend tests/test_frontend.c)
      target_link_libraries(test_frontend cybiko_core vitacybiko_input PkgConfig::HOST_SDL)
      target_include_directories(test_frontend PRIVATE ${CYBIKO_VENDOR_DIR}/tests)
      add_test(NAME frontend_events_and_storage COMMAND test_frontend)
      set_tests_properties(frontend_events_and_storage PROPERTIES
        ENVIRONMENT "SDL_VIDEODRIVER=dummy;SDL_AUDIODRIVER=dummy")
    endif()
    add_executable(test_input tests/test_input.c)
    target_link_libraries(test_input vitacybiko_input)
    target_include_directories(test_input PRIVATE ${CYBIKO_VENDOR_DIR}/tests)
    add_test(NAME frontend_input COMMAND test_input)
    add_executable(test_models tests/test_models.c)
    target_link_libraries(test_models cybiko_core)
    target_include_directories(test_models PRIVATE ${CYBIKO_VENDOR_DIR}/tests)
    add_test(NAME model_profiles_and_dataflash COMMAND test_models)
    set(TEST_MODULES memory cfs keyboard timer8 timer16 rtc speaker hd66421 h8s_cpu emulator)
    foreach(mod ${TEST_MODULES})
      add_executable(test_${mod} ${CYBIKO_VENDOR_DIR}/tests/test_${mod}.c)
      target_link_libraries(test_${mod} cybiko_core)
      target_include_directories(test_${mod} PRIVATE ${CYBIKO_VENDOR_DIR}/tests)
      add_test(NAME ${mod} COMMAND test_${mod})
    endforeach()
  endif()
endif()
