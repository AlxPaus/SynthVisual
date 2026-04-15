cmake_minimum_required(VERSION 3.14)
project(PhysicsSynthVisual VERSION 0.4 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 99)

if(MSVC)
    add_compile_options(/Z7 /MP1 /bigobj /Zm500)
    add_compile_options(/wd4005 /wd4244 /wd4305)
endif()

include(FetchContent)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw GIT_TAG 3.3.8)
FetchContent_MakeAvailable(glfw)

FetchContent_Declare(imgui GIT_REPOSITORY https://github.com/ocornut/imgui GIT_TAG v1.89.9)
FetchContent_MakeAvailable(imgui)

add_library(imgui_lib STATIC
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)
target_include_directories(imgui_lib PUBLIC
    "${imgui_SOURCE_DIR}"
    "${imgui_SOURCE_DIR}/backends"
)
target_link_libraries(imgui_lib PUBLIC glfw opengl32)

add_subdirectory(src/dsp)
add_subdirectory(src/synth)
add_subdirectory(src/audio)
add_subdirectory(src/ui)

add_executable(PhysicsSynthVisual src/main.cpp)
target_include_directories(PhysicsSynthVisual PRIVATE src)
target_link_libraries(PhysicsSynthVisual PRIVATE synth audio_driver ui imgui_lib)

if(WIN32)
    target_link_libraries(PhysicsSynthVisual PRIVATE winmm comdlg32 ole32)
endif()
