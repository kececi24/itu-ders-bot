cmake_minimum_required(VERSION 3.10)
project(ITUPickerProject LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(main
    src/main.cpp
    src/clock.cpp
    src/token.cpp
    src/clock.hpp
    src/token.hpp
    src/response.hpp
)

add_executable(setup setup/main.cpp)

foreach(target IN ITEMS main setup)
    target_include_directories(${target} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_target_properties(${target} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_CURRENT_SOURCE_DIR}>"
    )

    if(CMAKE_COMPILER_IS_GNUCXX)
        target_compile_options(${target} PRIVATE -O3 -Wall)
        target_link_options(${target} PRIVATE -static)
    endif()
endforeach()

target_link_libraries(main PRIVATE winhttp winmm)
