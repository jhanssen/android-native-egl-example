project(nativeegl)
cmake_minimum_required(VERSION 3.10.1)

add_subdirectory(glm)

add_library(nativeegl SHARED renderer.cpp jniapi.cpp)

set_property(TARGET nativeegl PROPERTY CXX_STANDARD 17)
target_include_directories(nativeegl PRIVATE ${CMAKE_CURRENT_LIST_DIR})
target_link_libraries(nativeegl log android EGL GLESv3 glm::glm)
