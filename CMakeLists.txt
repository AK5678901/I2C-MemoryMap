cmake_minimum_required(VERSION 3.20)
project(i2c_memorymap VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGuiのパス定義
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodule/imgui")
set(IMGUI_CLUB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodule/imgui_club")

# ソースファイルを明示的に定義
set(SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/application.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/control_panel.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/csv_processor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/device_config_loader.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/device_window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/gui_context.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/i2c_device.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/i2c_event_processor.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main_window.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/live_receiver.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/live_protocol.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/timeline_view.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/time_value.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/view_helpers.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/windows_helpers.cpp"
    
    # ImGui コアソース
    "${IMGUI_DIR}/imgui.cpp"
    "${IMGUI_DIR}/imgui_draw.cpp"
    "${IMGUI_DIR}/imgui_widgets.cpp"
    "${IMGUI_DIR}/imgui_tables.cpp"
    # バックエンドソース
    "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
    "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
)

# 実行ターゲットの作成
add_executable(${PROJECT_NAME} ${SOURCES})

# インクルードパスの設定
target_include_directories(${PROJECT_NAME} PRIVATE 
    ${CMAKE_SOURCE_DIR}/include
    "${IMGUI_DIR}"
    "${IMGUI_DIR}/backends"
    "${IMGUI_CLUB_DIR}/imgui_memory_editor"
)

# 依存ライブラリのリンク (GLFWやOpenGLが必要)
# find_packageを使用するか、環境に合わせてライブラリを指定してください
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(submodule/glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)

# OpenGLライブラリのリンク (Windows/Linux環境で一般的な処理)
if(WIN32)
    target_sources(${PROJECT_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/assets/app_icon.rc")
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32 ws2_32)
    # MSVC環境（Windows）のとき、コンソール画面を非表示にする
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:WINDOWS)
    endif()
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()

# Keep the distribution layout in one place for both install and CPack.
install(TARGETS ${PROJECT_NAME} RUNTIME DESTINATION . COMPONENT Runtime)
install(DIRECTORY resources/ DESTINATION . COMPONENT Runtime)
install(DIRECTORY saleae-live DESTINATION . COMPONENT Runtime
    PATTERN "__pycache__" EXCLUDE
)
install(FILES README.md LICENSE DESTINATION . COMPONENT Runtime)

set(CPACK_GENERATOR "ZIP")
set(CPACK_PACKAGE_FILE_NAME "I2C_MemoryMap")
set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY OFF)
set(CPACK_PACKAGE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/packages")
set(CPACK_COMPONENTS_ALL Runtime)
set(CPACK_VERBATIM_VARIABLES ON)
configure_file(cmake/PublishPackage.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/PublishPackage.cmake" @ONLY)
set(CPACK_POST_BUILD_SCRIPTS "${CMAKE_CURRENT_BINARY_DIR}/PublishPackage.cmake")
include(CPack)

if(WIN32)
    # An always-run target also picks up resource additions and deletions.
    add_custom_target(distribution ALL
        COMMAND "${CMAKE_CPACK_COMMAND}" --config "${CMAKE_CURRENT_BINARY_DIR}/CPackConfig.cmake"
            -C "$<CONFIG>"
        DEPENDS ${PROJECT_NAME}
        COMMENT "Generating distribution with CPack"
        VERBATIM
    )
endif()

if(BUILD_TESTING)
    function(add_i2c_memorymap_test test_name test_source)
        add_executable(${test_name}
            "${CMAKE_CURRENT_SOURCE_DIR}/${test_source}"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/i2c_device.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/time_value.cpp"
        )
        target_include_directories(${test_name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
        add_test(NAME ${test_name} COMMAND ${test_name})
    endfunction()

    add_i2c_memorymap_test(live_protocol_test tests/live_protocol_test.cpp)
    target_sources(live_protocol_test PRIVATE src/live_protocol.cpp src/i2c_event_processor.cpp)
    add_i2c_memorymap_test(register_history_test tests/register_history_test.cpp)
    add_i2c_memorymap_test(access_statistics_test tests/access_statistics_test.cpp)
    add_i2c_memorymap_test(i2c_event_processor_test tests/i2c_event_processor_test.cpp)
    target_sources(i2c_event_processor_test PRIVATE src/i2c_event_processor.cpp)
    add_i2c_memorymap_test(i2c_bus_manager_test tests/i2c_bus_manager_test.cpp)
    target_sources(i2c_bus_manager_test PRIVATE src/i2c_event_processor.cpp)
    add_executable(time_value_test tests/time_value_test.cpp src/time_value.cpp)
    target_include_directories(time_value_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME time_value_test COMMAND time_value_test)
    add_executable(csv_iso_test tests/csv_iso_test.cpp src/csv_processor.cpp src/i2c_event_processor.cpp src/i2c_device.cpp src/time_value.cpp)
    target_include_directories(csv_iso_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME csv_iso_test COMMAND csv_iso_test)
    add_executable(multi_bus_test tests/multi_bus_test.cpp src/csv_processor.cpp src/device_config_loader.cpp
        src/i2c_event_processor.cpp src/i2c_device.cpp src/time_value.cpp)
    target_include_directories(multi_bus_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
    add_test(NAME multi_bus_test COMMAND multi_bus_test)
    add_executable(multi_bus_view_test tests/multi_bus_view_test.cpp
        src/timeline_view.cpp src/device_window.cpp src/view_helpers.cpp
        src/i2c_event_processor.cpp src/i2c_device.cpp src/time_value.cpp
        "${IMGUI_DIR}/imgui.cpp" "${IMGUI_DIR}/imgui_draw.cpp"
        "${IMGUI_DIR}/imgui_widgets.cpp" "${IMGUI_DIR}/imgui_tables.cpp")
    target_include_directories(multi_bus_view_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include" "${IMGUI_DIR}")
    add_test(NAME multi_bus_view_test COMMAND multi_bus_view_test)
endif()
