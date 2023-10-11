cmake_minimum_required(VERSION 2.8.12)
# No idea what the true minimum version is. I use 3.27.4.

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

#include(add_dev_configuration) ???
project(osm-play)

#find_package(Boost REQUIRED COMPONENTS program_options)
#include_directories(SYSTEM ${Boost_INCLUDE_DIRS})

#if(NOT DEFINED CMAKE_PREFIX_PATH)
#    set(CMAKE_PREFIX_PATH "../../libosmium;../../protozero")
#endif()
find_package(Osmium REQUIRED COMPONENTS pbf)
include_directories(SYSTEM ${OSMIUM_INCLUDE_DIRS})
#include(common)

# OSMIUM_DRACONIC_CLANG_OPTIONS
# OSMIUM_WARNING_OPTIONS

set(PROG osm-play)
file(GLOB SOURCES *.cpp *.hpp)
add_executable(${PROG} ${SOURCES})
# FIXME: Why doesn't this work? --> target_compile_options(${PROG} PRIVATE ${OSMIUM_WARNING_OPTIONS})
target_compile_options(${PROG} PRIVATE -Wall -Wextra -pedantic -Wredundant-decls -Wdisabled-optimization -Wctor-dtor-privacy -Wnon-virtual-dtor -Woverloaded-virtual -Wsign-promo -Wold-style-cast)
target_link_libraries(${PROG} ${Boost_LIBRARIES} ${OSMIUM_LIBRARIES})
set_pthread_on_target(${PROG})

#add_test(node_density_help node_density --help)

#set_tests_properties(node_density_help PROPERTIES
#                     PASS_REGULAR_EXPRESSION "Usage: node_density"
#)
