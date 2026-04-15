add_library(synth STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/audio_engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/presets.cpp
)
target_include_directories(synth PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(synth PUBLIC dsp)
target_compile_definitions(synth PRIVATE NOMINMAX)
