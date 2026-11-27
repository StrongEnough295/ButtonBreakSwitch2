cmake_minimum_required(VERSION 3.13)
project(GameShareMirrorExample LANGUAGES CXX)
add_subdirectory(../.. gameshare-sdk)
add_executable(gameshare-mirror main.cpp)
target_link_libraries(gameshare-mirror PRIVATE gameshare::gameshare)
set_target_properties(gameshare-mirror PROPERTIES SUFFIX ".elf")
add_custom_target(gameshare-mirror.nro ALL DEPENDS gameshare-mirror
    COMMAND ${NX_NACPTOOL_EXE} --create "GameShare Mirror Example" "XITRIX" "0.1.0" gameshare-mirror.nacp
    COMMAND ${NX_ELF2NRO_EXE} $<TARGET_FILE:gameshare-mirror> gameshare-mirror.nro
        --nacp=gameshare-mirror.nacp)
