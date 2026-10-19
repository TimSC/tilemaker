cmake_minimum_required(VERSION 3.18)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" tilemaker_version LIMIT_COUNT 1)
string(STRIP "${tilemaker_version}" tilemaker_version)

project(tilemaker VERSION ${tilemaker_version})

OPTION(TILEMAKER_BUILD_STATIC "Attempt to link dependencies static" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
  message(FATAL_ERROR "In-source builds are not allowed, please use a separate build directory.")
endif()

if (POLICY CMP0074)
  cmake_policy(SET CMP0074 NEW)
endif()

IF (TILEMAKER_BUILD_STATIC)
    MESSAGE (STATUS "Staticly linking with Boost")
    SET (Boost_USE_STATIC_LIBS TRUE)
	SET (CMAKE_FIND_LIBRARY_SUFFIXES ${CMAKE_STATIC_LIBRARY_SUFFIX})
ELSE ()
    MESSAGE (STATUS "Dynamically linking with Boost")
    SET (Boost_USE_STATIC_LIBS FALSE)
ENDIF ()

IF ("${CMAKE_CXX_COMPILER_ID}" STREQUAL "MSVC")
    LINK_DIRECTORIES(${Boost_LIBRARY_DIRS})
ENDIF ()

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
	set(Boost_USE_DEBUG_RUNTIME true)
else()
	set(Boost_USE_DEBUG_RUNTIME false)
endif()

set(BOOST_HAS_SYSTEM false)
set(BOOST_HAS_THROW_EXCEPTION false)

find_package(Boost 1.89 QUIET COMPONENTS filesystem program_options)

if(Boost_FOUND)
	find_package(Boost 1.89 QUIET COMPONENTS throw_exception)
	if(TARGET Boost::throw_exception)
		set(BOOST_HAS_THROW_EXCEPTION true)
	endif()
else()
	find_package(Boost 1.66 REQUIRED COMPONENTS system filesystem program_options)
	set(BOOST_HAS_SYSTEM true)
endif()

find_package(libshp REQUIRED)

find_package(PostgreSQL REQUIRED)

# cppo5m reads the OSM input. By default it is built from the submodule; set
# CPPO5M_SOURCE_DIR to another tree, or to empty to use an installed one.
set(CPPO5M_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cppo5m" CACHE PATH "cppo5m source tree to build with")
if(CPPO5M_SOURCE_DIR AND EXISTS "${CPPO5M_SOURCE_DIR}/CMakeLists.txt")
	add_subdirectory("${CPPO5M_SOURCE_DIR}" cppo5m EXCLUDE_FROM_ALL)
else()
	find_package(cppo5m REQUIRED)
endif()

find_package(Rapidjson REQUIRED)

find_package(Lua)

if(LUA_FOUND)
	include_directories(${LUA_INCLUDE_DIR})
else()
	find_package(LuaJIT REQUIRED)
	add_definitions(-DLUAJIT)
	include_directories(${LUAJIT_INCLUDE_DIR})
endif()

set(CMAKE_CXX_STANDARD 17)

if(NOT TM_VERSION)
	set(TM_VERSION v${PROJECT_VERSION})
	find_package(Git QUIET)
	if(NOT GIT_FOUND OR NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/.git")
		set(TM_VERSION ${TM_VERSION}+nogit)
	else()
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" rev-parse --short=12 HEAD
			OUTPUT_VARIABLE tm_git_sha
			RESULT_VARIABLE tm_git_sha_result
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		execute_process(
			COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" describe --exact-match --tags HEAD
			OUTPUT_VARIABLE tm_git_tag
			RESULT_VARIABLE tm_git_tag_result
			ERROR_QUIET
			OUTPUT_STRIP_TRAILING_WHITESPACE)
		if(tm_git_sha_result EQUAL 0 AND NOT tm_git_sha STREQUAL "")
			set(tm_git_dirty_suffix)
			execute_process(
				COMMAND "${GIT_EXECUTABLE}" -C "${CMAKE_CURRENT_SOURCE_DIR}" diff-index --quiet HEAD --
				RESULT_VARIABLE tm_git_dirty_result
				ERROR_QUIET
				OUTPUT_QUIET)
			if(tm_git_dirty_result EQUAL 1)
				set(tm_git_dirty_suffix .dirty)
			endif()
			if(NOT (tm_git_tag_result EQUAL 0 AND tm_git_tag STREQUAL TM_VERSION AND tm_git_dirty_suffix STREQUAL ""))
				set(TM_VERSION ${TM_VERSION}+g${tm_git_sha}${tm_git_dirty_suffix})
			endif()
		else()
			set(TM_VERSION ${TM_VERSION}+nogit)
		endif()
	endif()
endif()
add_compile_definitions(TM_VERSION=${TM_VERSION})

if(MSVC)
 	find_package(unofficial-sqlite3 CONFIG REQUIRED)
	add_library(SQLite::SQLite3 ALIAS unofficial::sqlite3::sqlite3)
	add_definitions(-D_USE_MATH_DEFINES -DWIN32_LEAN_AND_MEAN -DNOGDI -D__restrict__=__restrict)
	set(THREAD_LIB "")
else()
	find_package(SQLite3 REQUIRED)
	if(NOT TARGET SQLite::SQLite3)
	  add_library(SQLite::SQLite3 IMPORTED UNKNOWN)
	  set_target_properties(SQLite::SQLite3 PROPERTIES
	    IMPORTED_LOCATION "${SQLite3_LIBRARIES}"
	    INTERFACE_INCLUDE_DIRECTORIES "${SQLite3_INCLUDE_DIRS}")
	endif()
	set(THREAD_LIB pthread)
endif()

file(READ "${CMAKE_CURRENT_SOURCE_DIR}/resources/config-schema.json" TILEMAKER_CONFIG_SCHEMA_JSON)
configure_file(
	cmake/config_schema.h.in
	"${CMAKE_BINARY_DIR}/config_schema.h"
	@ONLY)

file(GLOB tilemaker_src_files
	src/attribute_store.cpp
	src/config_validator.cpp
	src/coordinates.cpp
	src/coordinates_geom.cpp
	src/declutter.cpp
	src/external/libdeflate/lib/adler32.c
	src/external/libdeflate/lib/arm/cpu_features.c
	src/external/libdeflate/lib/crc32.c
	src/external/libdeflate/lib/deflate_compress.c
	src/external/libdeflate/lib/deflate_decompress.c
	src/external/libdeflate/lib/gzip_compress.c
	src/external/libdeflate/lib/gzip_decompress.c
	src/external/libdeflate/lib/utils.c
	src/external/libdeflate/lib/x86/cpu_features.c
	src/external/libdeflate/lib/zlib_compress.c
	src/external/libdeflate/lib/zlib_decompress.c
	src/geojson_processor.cpp
	src/geom.cpp
	src/helpers.cpp
	src/mbtiles.cpp
	src/options_parser.cpp
	src/osm_lua_processing.cpp
	src/osm_area_processor.cpp
	src/osm_db_reader.cpp
	src/osm_mem_tiles.cpp
	src/osm_reader.cpp
	src/osm_store.cpp
	src/output_object.cpp
	src/pmtiles.cpp
	src/pooled_string.cpp
	src/relation_roles.cpp
	src/shared_data.cpp
	src/shp_mem_tiles.cpp
	src/shp_processor.cpp
	src/significant_tags.cpp
	src/simplify_buildings.cpp
	src/tag_map.cpp
	src/tile_coordinates_set.cpp
	src/tile_data.cpp
	src/tile_sorting.cpp
	src/tilemaker.cpp
	src/tile_worker.cpp
	src/visvalingam.cpp
  )
add_executable(tilemaker ${tilemaker_src_files})
target_include_directories(tilemaker PRIVATE include)
target_include_directories(tilemaker PRIVATE ${CMAKE_BINARY_DIR}) # for generated files
target_link_libraries(tilemaker
		${THREAD_LIB} ${CMAKE_DL_LIBS}
		${LUA_LIBRARIES}
		shapelib::shp
		SQLite::SQLite3
		Rapidjson::rapidjson
		cppo5m::cppo5m
		PostgreSQL::PostgreSQL
		Boost::filesystem Boost::program_options)
if(BOOST_HAS_SYSTEM)
	target_link_libraries(tilemaker Boost::system)
endif()
if(BOOST_HAS_THROW_EXCEPTION)
	target_link_libraries(tilemaker Boost::throw_exception)
endif()

include(CheckCxxAtomic)
if(NOT HAVE_CXX11_ATOMIC)
	string(APPEND CMAKE_CXX_STANDARD_LIBRARIES
	" ${LIBATOMIC_LINK_FLAGS}")
endif()

install(FILES docs/man/tilemaker.1 DESTINATION share/man/man1)

# A demonstration server for the tiles, with a map page (server/static)
add_executable(tilemaker-server server/server.cpp)
target_include_directories(tilemaker-server PRIVATE include)
target_link_libraries(tilemaker-server ${THREAD_LIB} ${CMAKE_DL_LIBS} SQLite::SQLite3 Boost::filesystem Boost::program_options)
if(BOOST_HAS_THROW_EXCEPTION)
	target_link_libraries(tilemaker-server Boost::throw_exception)
endif()

install(TARGETS tilemaker tilemaker-server RUNTIME DESTINATION bin)

# ******* Tests *******
# Each test builds with just the sources it needs; run them with ctest.

option(TILEMAKER_BUILD_TESTS "Build the tests" ON)
if(TILEMAKER_BUILD_TESTS)
	enable_testing()
	set(libdeflate_src_files ${tilemaker_src_files})
	list(FILTER libdeflate_src_files INCLUDE REGEX "libdeflate")

	function(tilemaker_test name)
		add_executable(test_${name} test/${name}.test.cpp ${ARGN})
		target_include_directories(test_${name} PRIVATE include ${CMAKE_BINARY_DIR})
		target_link_libraries(test_${name} ${THREAD_LIB} Rapidjson::rapidjson Boost::filesystem Boost::program_options)
		if(BOOST_HAS_SYSTEM)
			target_link_libraries(test_${name} Boost::system)
		endif()
		# Some tests read files relative to the source tree, such as config.json
		add_test(NAME ${name} COMMAND test_${name} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
	endfunction()

	tilemaker_test(append_vector)
	tilemaker_test(attribute_store src/attribute_store.cpp src/pooled_string.cpp)
	tilemaker_test(config_validator src/config_validator.cpp)
	tilemaker_test(deque_map)
	tilemaker_test(helpers src/helpers.cpp ${libdeflate_src_files})
	tilemaker_test(options_parser src/options_parser.cpp)
	tilemaker_test(osm_reader src/osm_reader.cpp)
	target_link_libraries(test_osm_reader cppo5m::cppo5m)
	tilemaker_test(osm_store src/osm_store.cpp src/coordinates.cpp src/relation_roles.cpp)
	tilemaker_test(pooled_string src/pooled_string.cpp)
	tilemaker_test(relation_roles src/relation_roles.cpp)
	tilemaker_test(significant_tags src/significant_tags.cpp src/tag_map.cpp)
	tilemaker_test(tile_coordinates_set src/tile_coordinates_set.cpp)
endif()
