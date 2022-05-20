cmake_minimum_required(VERSION 3.2)

set(PATCH_VERSION "1" CACHE INTERNAL "Patch version")
set(PROJECT_VESRION 0.0.${PATCH_VERSION})

project(bulk_server VERSION ${PROJECT_VESRION})

find_package(Boost COMPONENTS program_options system thread REQUIRED)

configure_file(version.h.in version.h)

file(COPY ${CMAKE_BINARY_DIR}/version.h
        DESTINATION ${CMAKE_CURRENT_SOURCE_DIR})
		
add_library(async STATIC async.cpp async.h)
add_executable(bulk_server main.cpp version.cpp)

set_target_properties(bulk_server async PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

target_include_directories(bulk_server
    PRIVATE "${CMAKE_BINARY_DIR}" ${Boost_INCLUDE_DIR}
)

target_link_libraries(bulk_server async ${Boost_LIBRARIES})

if (MSVC)
    target_compile_options(bulk_server PRIVATE
        /W4
    )
    target_compile_definitions(bulk_server PRIVATE _WIN32_WINNT=0x0601)
else ()
    target_compile_options(bulk_server PRIVATE
        -Wall -Wextra -pedantic -Werror
    )
    target_link_libraries(async pthread)
endif()

install(TARGETS bulk_server RUNTIME DESTINATION bin)

set(CPACK_GENERATOR DEB)

if (WIN32)
    set(CPACK_GENERATOR ZIP)
endif()

set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_CONTACT example@example.com)
include(CPack)