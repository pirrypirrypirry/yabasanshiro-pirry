project(yabause-retro-arena)

add_definitions( -D__RETORO_ARENA__ )

yab_port_start()


find_package(SDL2 COMPONENTS sdl)
find_package(SDL2 REQUIRED)
find_package(OpenGLES)
find_package(PkgConfig REQUIRED)


if (NOT OPENGLES2_FOUND)
	return()
endif (NOT OPENGLES2_FOUND)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

add_subdirectory(pugixml)
add_subdirectory(nanogui-sdl)

#include( ../glfw/external_zlib.cmake )
include( ${CMAKE_SOURCE_DIR}/CMake/Packages/external_libpng.cmake )

#------------------------------------------------------------------------------
# nlohmann/json
#  * sets EXTERNAL_JSON_INCLUDE_DIR 

ExternalProject_Add(
     Json
     PREFIX ${CMAKE_CURRENT_BINARY_DIR}/Json
     GIT_REPOSITORY https://github.com/nlohmann/json.git
     # For shallow git clone (without downloading whole history)
     #GIT_SHALLOW 1
     # For point at certain tag
     GIT_TAG v3.2.0
     #disables auto update on every build
     UPDATE_DISCONNECTED 1
     #disable following
     CONFIGURE_COMMAND "" BUILD_COMMAND "" INSTALL_DIR "" INSTALL_COMMAND ""
     )
 # Update json target
 add_custom_target(external-Json-update
     COMMENT "Updated Nlohmann/Json"
     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/Json/src/Json
     COMMAND ${GIT_EXECUTABLE} pull
     DEPENDS Json)
# set include directory
set(EXTERNAL_JSON_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/Json/src/Json/single_include")

set(PORT_INCLUDE_DIRS ${EXTERNAL_JSON_INCLUDE_DIR} ${SDL2_INCLUDE_DIR} ${png_INCLUDE_DIR} ${zlib_INCLUDE_DIR} )
set(PORT_LIBRARIES ${SDL2_LIBRARY} ${OPENGLES2_LIBRARIES} ${png_STATIC_LIBRARIES} ${zlib_STATIC_LIBRARIES} )

message("Nlohmann/json dir =" ${EXTERNAL_JSON_INCLUDE_DIR})

set(yabause_ra_SOURCES
	main.cpp
	InputManager.h
	InputManager.cpp
	InputConfig.h
	InputConfig.cpp
	MenuScreen.h
    MenuScreen.cpp
    Preference.h
    Preference.cpp
    RetroAchievements.cpp
    rcheevos/src/rc_client.c
    rcheevos/src/rc_compat.c
    rcheevos/src/rc_util.c
    rcheevos/src/rc_version.c
    rcheevos/src/rapi/rc_api_common.c
    rcheevos/src/rapi/rc_api_info.c
    rcheevos/src/rapi/rc_api_runtime.c
    rcheevos/src/rapi/rc_api_user.c
    rcheevos/src/rcheevos/alloc.c
    rcheevos/src/rcheevos/condition.c
    rcheevos/src/rcheevos/condset.c
    rcheevos/src/rcheevos/consoleinfo.c
    rcheevos/src/rcheevos/format.c
    rcheevos/src/rcheevos/lboard.c
    rcheevos/src/rcheevos/memref.c
    rcheevos/src/rcheevos/operand.c
    rcheevos/src/rcheevos/rc_validate.c
    rcheevos/src/rcheevos/richpresence.c
    rcheevos/src/rcheevos/runtime.c
    rcheevos/src/rcheevos/runtime_progress.c
    rcheevos/src/rcheevos/trigger.c
    rcheevos/src/rcheevos/value.c
    rcheevos/src/rhash/md5.c
    ../about.h
    ../about.c
) 
link_directories( ${link_directories} ${LIBCHDR_LIB_DIR} )
include_directories( ${PORT_INCLUDE_DIRS} "nanogui-sdl/include" "nanogui-sdl/ext/eigen" "rcheevos/include" "rcheevos/src" )
add_executable(yabause-retro-arena ${yabause_ra_SOURCES})
target_link_libraries(yabause-retro-arena yabause  pugixml ${YABAUSE_LIBRARIES} ${PORT_LIBRARIES} nanogui stdc++fs  ${LIBCHDR_LIBRARIES} )
target_link_libraries( yabause-retro-arena ${LIBCHDR_LIBRARIES} )
target_link_libraries( yabause-retro-arena ${zlib_STATIC_LIBRARIES} )
target_link_libraries( yabause-retro-arena dl pthread )   # RetroAchievements: libcurl via dlopen, login thread
add_dependencies(yabause-retro-arena Json png zlib libchdr )
yab_port_success(yabause-retro-arena)
set_target_properties(yabause-retro-arena PROPERTIES OUTPUT_NAME yabasanshiro)


install(TARGETS yabause-retro-arena DESTINATION ".")
install(FILES "README.md" DESTINATION "." )

#install(TARGETS yabause-retoro-arena DESTINATION "bin")
#install(FILES "README.md" DESTINATION "." )
