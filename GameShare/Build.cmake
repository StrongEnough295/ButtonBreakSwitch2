# Keep the normal single-instance target available with this option OFF.
option(PPSSPP_SWITCH2_GAMESHARE "Experimental independent GameShare guest" OFF)
if(NOT PPSSPP_SWITCH2_GAMESHARE)
	return()
endif()

# Snapshot only PPSSPP's libraries, before adding the GameShare SDK.
function(switch2_collect_static_libraries directory output)
	get_property(libraries DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
	set(result)
	foreach(library IN LISTS libraries)
		get_target_property(kind "${library}" TYPE)
		if(kind STREQUAL "STATIC_LIBRARY")
			list(APPEND result "${library}")
		endif()
	endforeach()
	get_property(children DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
	foreach(child IN LISTS children)
		switch2_collect_static_libraries("${child}" nested)
		list(APPEND result ${nested})
	endforeach()
	set(${output} "${result}" PARENT_SCOPE)
endfunction()
switch2_collect_static_libraries("${CMAKE_SOURCE_DIR}" guest_libraries)
set(guest_archives)
foreach(library IN LISTS guest_libraries)
	list(APPEND guest_archives "$<TARGET_FILE:${library}>")
endforeach()

target_compile_definitions(${TargetBin} PRIVATE PPSSPP_SWITCH2_GAMESHARE=1)
target_compile_definitions(ppsspp_ui PRIVATE PPSSPP_SWITCH2_GAMESHARE=1)
set_property(SOURCE
	"${CMAKE_SOURCE_DIR}/Core/HLE/sceNetAdhoc.cpp"
	"${CMAKE_SOURCE_DIR}/Core/HLE/proAdhocServer.cpp"
	TARGET_DIRECTORY Core APPEND PROPERTY COMPILE_DEFINITIONS PPSSPP_SWITCH2_GAMESHARE=1)
add_library(PPSSPPSwitch2GuestFrontend OBJECT Switch/GameShare/GuestFrontend.cpp)
target_compile_definitions(PPSSPPSwitch2GuestFrontend PRIVATE PPSSPP_SWITCH2_GAMESHARE=1)
target_link_libraries(PPSSPPSwitch2GuestFrontend PRIVATE ppsspp_ui)
set(guest_object "${CMAKE_BINARY_DIR}/gameshare/guest-core.o")
add_custom_command(OUTPUT "${guest_object}"
	COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/Tools/isolate-switch2-guest.py"
		--ld "${CMAKE_LINKER}" --nm "${CMAKE_NM}" --objcopy "${CMAKE_OBJCOPY}" --readelf "${CMAKE_READELF}"
		--output "${guest_object}" --object $<TARGET_OBJECTS:PPSSPPSwitch2GuestFrontend> ${guest_archives}
	DEPENDS PPSSPPSwitch2GuestFrontend ${guest_libraries} "${CMAKE_SOURCE_DIR}/Tools/isolate-switch2-guest.py"
	COMMAND_EXPAND_LISTS VERBATIM)
set_source_files_properties("${guest_object}" PROPERTIES GENERATED TRUE EXTERNAL_OBJECT TRUE)
target_sources(${TargetBin} PRIVATE "${guest_object}")
# The Vulkan driver is linked statically and owns the process's GSV sessions.
target_link_options(${TargetBin} PRIVATE "-Wl,--wrap=gsvGpuCreateSlotEvent,--wrap=gsvGpuOpenWorker,--wrap=gsvGpuClose")
add_library(PPSSPPSwitch2GameShareBridge STATIC Switch/GameShare/Integration.cpp Switch/GameShare/CpuRenderer.cpp Switch/GameShare/AdhocServer.cpp)
target_compile_definitions(PPSSPPSwitch2GameShareBridge PRIVATE PPSSPP_SWITCH2_GAMESHARE=1)
set(GAMESHARE_ENABLE_CPU_STREAMER OFF CACHE BOOL "" FORCE)
add_subdirectory(Switch/GameShare/sdk)
target_link_libraries(PPSSPPSwitch2GameShareBridge PRIVATE gameshare::gameshare Core)
target_link_libraries(${TargetBin} PPSSPPSwitch2GameShareBridge)
