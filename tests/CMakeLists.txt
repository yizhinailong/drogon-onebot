file(GLOB TEST_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp
)

foreach(TEST_SOURCE IN LISTS TEST_SOURCES)
    cmake_path(GET TEST_SOURCE STEM TEST_NAME)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE ${PROJECT_NAME}::${PROJECT_NAME})
    target_compile_features(${TEST_NAME} PRIVATE cxx_std_23)
endforeach()
