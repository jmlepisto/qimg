cmake_minimum_required(VERSION 2.8)
project(qimg)

set(CMAKE_CXX_FLAGS_RELEASE "-O3")

include_directories(./lib/)

add_executable(
    ${PROJECT_NAME}
    qimg.c
    )

target_link_libraries(${PROJECT_NAME} m)



