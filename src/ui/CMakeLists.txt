add_library(ui STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/top_bar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/osc_tab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/draw_tab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/fx_tab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/mod_matrix_tab.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scope.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/piano.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/spectrum.cpp
)
target_include_directories(ui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(ui PUBLIC synth imgui_lib)
target_compile_definitions(ui PRIVATE NOMINMAX)
