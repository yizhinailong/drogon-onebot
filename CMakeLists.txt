cmake_minimum_required(VERSION 4.0.0)

project(
    drogon-onebot
    LANGUAGES CXX
    VERSION 0.1.4
)

option(DROGON_ONEBOT_BUILD_TESTS "Build the test executables" ${PROJECT_IS_TOP_LEVEL})
option(DROGON_ONEBOT_INSTALL "Generate install and package config rules" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

find_package(Drogon CONFIG REQUIRED)

# Drogon's FindFilesystem.cmake restores an unset CMAKE_CXX_STANDARD as "",
# which target_compile_features rejects as an invalid CXX_STANDARD.
if(CMAKE_CXX_STANDARD STREQUAL "")
    unset(CMAKE_CXX_STANDARD)
endif()

file(GLOB_RECURSE SOURCE CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp
)

file(GLOB_RECURSE PUBLIC_HEADERS CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp
)

add_library(${PROJECT_NAME} ${SOURCE})
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(
    ${PROJECT_NAME}
    PUBLIC
    FILE_SET HEADERS
    BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/include
    FILES ${PUBLIC_HEADERS}
)

target_include_directories(
    ${PROJECT_NAME}
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(
    ${PROJECT_NAME}
    PUBLIC
    Drogon::Drogon
)

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_23)

if(DROGON_ONEBOT_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

if(DROGON_ONEBOT_INSTALL)
    set(PACKAGE_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

    install(
        TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}-targets
        FILE_SET HEADERS
    )

    install(
        EXPORT ${PROJECT_NAME}-targets
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${PACKAGE_INSTALL_DIR}
    )

    configure_package_config_file(
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/${PROJECT_NAME}-config.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
        INSTALL_DESTINATION ${PACKAGE_INSTALL_DIR}
    )

    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        COMPATIBILITY SameMinorVersion
    )

    install(
        FILES
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}-config-version.cmake
        DESTINATION ${PACKAGE_INSTALL_DIR}
    )
endif()
