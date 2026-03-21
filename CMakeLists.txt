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

# === СПИСОК ФАЙЛОВ ===
add_executable(PhysicsSynthVisual
    "src/main.cpp"
    "src/osc.h"
    "src/wasapi_audio.h"  # Добавлен наш кастомный драйвер
    # src/audio_impl.c    # Удалено: мы больше не используем miniaudio
    "${imgui_SOURCE_DIR}/imgui.cpp"
    "${imgui_SOURCE_DIR}/imgui_draw.cpp"
    "${imgui_SOURCE_DIR}/imgui_tables.cpp"
    "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp"
    "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(PhysicsSynthVisual PRIVATE 
    "src" 
    "${imgui_SOURCE_DIR}" 
    "${imgui_SOURCE_DIR}/backends"
)

target_link_libraries(PhysicsSynthVisual PRIVATE glfw)

if(WIN32)
    # Добавлены системные библиотеки Windows: 
    # winmm (MIDI), comdlg32 (Диалоги сохранения), ole32 (WASAPI COM)
    target_link_libraries(PhysicsSynthVisual PRIVATE opengl32 winmm comdlg32 ole32)
endif()