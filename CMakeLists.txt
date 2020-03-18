cmake_minimum_required(VERSION 3.7)

project(pt2-clone)

find_package(SDL2 REQUIRED)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${pt2-clone_SOURCE_DIR}/release/other/")

file(GLOB pt2-clone_SRC
    "${pt2-clone_SOURCE_DIR}/src/*.c"
    "${pt2-clone_SOURCE_DIR}/src/gfx/*.c"
)

add_executable(pt2-clone ${pt2-clone_SRC})

target_include_directories(pt2-clone SYSTEM
    PRIVATE ${SDL2_INCLUDE_DIRS})

if("${SDL2_LIBRARIES}" STREQUAL "")
    message(WARNING "SDL2_LIBRARIES wasn't set, manually setting to SDL2::SDL2")
    set(SDL2_LIBRARIES "SDL2::SDL2")
endif()

target_link_libraries(pt2-clone
    PRIVATE m ${SDL2_LIBRARIES})

install(TARGETS pt2-clone
    RUNTIME DESTINATION bin)
