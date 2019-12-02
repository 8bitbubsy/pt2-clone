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

target_link_libraries(pt2-clone 
    PRIVATE m asound pthread ${SDL2_LIBRARIES})
target_compile_definitions(pt2-clone PRIVATE __LINUX_ALSA__)


install(TARGETS pt2-clone
        RUNTIME DESTINATION bin )