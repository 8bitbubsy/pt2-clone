cmake_minimum_required(VERSION 3.7)

project(pt2-clone)

option(EXTERNAL_LIBFLAC "use external(system) flac library" OFF)

find_package(SDL2 REQUIRED)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${pt2-clone_SOURCE_DIR}/release/other/")

file(GLOB pt2-clone_SRC
    "${pt2-clone_SOURCE_DIR}/src/*.c"
    "${pt2-clone_SOURCE_DIR}/src/modloaders/*.c"
    "${pt2-clone_SOURCE_DIR}/src/smploaders/*.c"
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
    
target_compile_definitions(pt2-clone
    PRIVATE HAS_LIBFLAC)
    
if(EXTERNAL_LIBFLAC)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FLAC REQUIRED IMPORTED_TARGET flac)
    target_compile_definitions(pt2-clone
        PRIVATE EXTERNAL_LIBFLAC)
    target_link_libraries(pt2-clone
        PRIVATE PkgConfig::FLAC)
else()
    file(GLOB flac_SRCS
        "${pt2-clone_SOURCE_DIR}/src/libflac/*.c")
    target_sources(pt2-clone PRIVATE ${flac_SRCS})
endif()

install(TARGETS pt2-clone
    RUNTIME DESTINATION bin)
