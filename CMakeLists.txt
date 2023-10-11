cmake_minimum_required(VERSION 3.5)
# No idea what the true minimum version is. I use 3.27.4.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
project(osm-play)

find_package(Osmium REQUIRED COMPONENTS pbf)
include_directories(SYSTEM ${OSMIUM_INCLUDE_DIRS})
file(GLOB HEADERS *.hpp)

set(PROG extract_relevant)
add_executable(${PROG} extract_relevant.cpp ${SOURCES})
# FIXME: Why doesn't this work? --> target_compile_options(${PROG} PRIVATE ${OSMIUM_WARNING_OPTIONS})
target_compile_options(${PROG} PRIVATE -Wall -Wextra -pedantic -Wredundant-decls -Wdisabled-optimization -Wctor-dtor-privacy -Wnon-virtual-dtor -Woverloaded-virtual -Wsign-promo -Wold-style-cast)
target_link_libraries(${PROG} ${Boost_LIBRARIES} ${OSMIUM_LIBRARIES})
set_pthread_on_target(${PROG})

# TODO: Also compile convert_to_svg.cpp
