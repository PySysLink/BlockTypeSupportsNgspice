cmake_minimum_required(VERSION 3.11)

set(PROJECT_VERSION "0.1.0")
set(PROJECT_VERSION_UNDERSCORE "0_1_0")
set(PYSYSLINK_BASE_VERSION "0.1.0")

project(BlockTypeSupportsNgspiceSupport VERSION ${PROJECT_VERSION})

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    if(WIN32)
        set(CMAKE_INSTALL_PREFIX "C:/Program Files" CACHE PATH "Install path" FORCE)
    elseif(APPLE)
        set(CMAKE_INSTALL_PREFIX "/Applications" CACHE PATH "Install path" FORCE)
    else()
        set(CMAKE_INSTALL_PREFIX "/usr/local" CACHE PATH "Install path" FORCE)
    endif()
endif()

set(INSTALLED_DYNAMIC_LIB_NAME "BlockTypeSupportsNgspiceSupport-${PROJECT_VERSION}")
set(PLUGIN_INSTALL_DIR
    "lib/pysyslink_plugins/block_type_supports/ngspice_support_${PROJECT_VERSION_UNDERSCORE}")

find_package(PySysLinkBase ${PYSYSLINK_BASE_VERSION} REQUIRED)

option(
    NGSPICE_PREFER_STATIC
    "Prefer a static ngspice library when libngspice.a is installed"
    ON)

if(NGSPICE_PREFER_STATIC)
    set(_NGSPICE_SAVED_LIBRARY_SUFFIXES ${CMAKE_FIND_LIBRARY_SUFFIXES})
    set(CMAKE_FIND_LIBRARY_SUFFIXES .a)
    find_library(NGSPICE_STATIC_LIBRARY NAMES ngspice)
    set(CMAKE_FIND_LIBRARY_SUFFIXES ${_NGSPICE_SAVED_LIBRARY_SUFFIXES})
    unset(_NGSPICE_SAVED_LIBRARY_SUFFIXES)
endif()

if(NGSPICE_PREFER_STATIC AND NGSPICE_STATIC_LIBRARY)
    set(NGSPICE_LIBRARY "${NGSPICE_STATIC_LIBRARY}")
    message(STATUS "Using static ngspice library: ${NGSPICE_LIBRARY}")
else()
    find_library(NGSPICE_LIBRARY NAMES ngspice libngspice.so.0)
    if(NGSPICE_PREFER_STATIC)
        message(STATUS
            "libngspice.a was not found; falling back to the ngspice shared library")
    endif()
endif()

if(NOT NGSPICE_LIBRARY)
    message(FATAL_ERROR "Could not find an ngspice library")
endif()

add_subdirectory(src)

include(CTest)
if(BUILD_TESTING)
    add_executable(ngspice_block_test tests/ngspice_block_test.cpp)
    target_include_directories(ngspice_block_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(ngspice_block_test
        PRIVATE
            PySysLinkBase::PySysLinkBase
            ${NGSPICE_LIBRARY})
    add_test(
        NAME ngspice_block_test
        COMMAND ngspice_block_test
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/rc_step.cir)
    set_tests_properties(ngspice_block_test PROPERTIES TIMEOUT 10)
endif()

install(DIRECTORY src/
    DESTINATION include/BlockTypeSupports/NgspiceSupport
    FILES_MATCHING PATTERN "*.h")

install(TARGETS BlockTypeSupportsNgspiceSupport
    EXPORT BlockTypeSupportsNgspiceSupportTargets
    ARCHIVE DESTINATION ${PLUGIN_INSTALL_DIR}
    LIBRARY DESTINATION ${PLUGIN_INSTALL_DIR}
    RUNTIME DESTINATION bin)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ngspice_support.pslkbtsp.yaml.in
    ${CMAKE_CURRENT_BINARY_DIR}/ngspice_support.${PROJECT_VERSION_UNDERSCORE}.pslkbtsp.yaml
    @ONLY)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/ngspice_blocks.pslkblp.yaml.in
    ${CMAKE_CURRENT_BINARY_DIR}/ngspice_blocks.${PROJECT_VERSION_UNDERSCORE}.pslkblp.yaml
    @ONLY)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/ngspice_support.${PROJECT_VERSION_UNDERSCORE}.pslkbtsp.yaml
    ${CMAKE_CURRENT_BINARY_DIR}/ngspice_blocks.${PROJECT_VERSION_UNDERSCORE}.pslkblp.yaml
    DESTINATION ${PLUGIN_INSTALL_DIR})

include(CMakePackageConfigHelpers)

install(EXPORT BlockTypeSupportsNgspiceSupportTargets
    FILE BlockTypeSupportsNgspiceSupportTargets.cmake
    NAMESPACE BlockTypeSupports::NgspiceSupport::
    DESTINATION lib/cmake/BlockTypeSupportsNgspiceSupport)

set(PACKAGE_INCLUDE_DIRS "${CMAKE_INSTALL_PREFIX}/include")
set(PACKAGE_LIBRARIES "${CMAKE_INSTALL_PREFIX}/${PLUGIN_INSTALL_DIR}")

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/BlockTypeSupportsNgspiceSupportConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/BlockTypeSupportsNgspiceSupportConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/BlockTypeSupportsNgspiceSupportConfig.cmake
    INSTALL_DESTINATION lib/cmake/BlockTypeSupportsNgspiceSupport)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/BlockTypeSupportsNgspiceSupportConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/BlockTypeSupportsNgspiceSupportConfigVersion.cmake
    DESTINATION lib/cmake/BlockTypeSupportsNgspiceSupport)
