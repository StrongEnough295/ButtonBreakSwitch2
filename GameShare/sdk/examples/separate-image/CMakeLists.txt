cmake_minimum_required(VERSION 3.13)
project(GameShareSeparateImageExample LANGUAGES C CXX)
add_subdirectory(../.. gameshare-sdk)
add_executable(gameshare-separate-image main.cpp)
target_link_libraries(gameshare-separate-image PRIVATE gameshare::gameshare)
set_target_properties(gameshare-separate-image PROPERTIES SUFFIX ".elf")
add_custom_target(gameshare-separate-image.nro ALL DEPENDS gameshare-separate-image
    COMMAND ${NX_NACPTOOL_EXE} --create "GameShare Separate Image" "XITRIX" "0.1.0" gameshare-separate-image.nacp
    COMMAND ${NX_ELF2NRO_EXE} $<TARGET_FILE:gameshare-separate-image> gameshare-separate-image.nro
        --nacp=gameshare-separate-image.nacp)
