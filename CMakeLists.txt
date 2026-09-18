cmake_minimum_required(VERSION 3.20)
project(i2c_memorymap VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ImGuiのパス定義
set(IMGUI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodule/imgui")
set(IMGUI_CLUB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/submodule/imgui_club")

# ソースファイルを明示的に定義
set(SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/i2c_device.cpp"
    
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
add_subdirectory(submodule/glfw)
target_link_libraries(${PROJECT_NAME} PRIVATE glfw)

# OpenGLライブラリのリンク (Windows/Linux環境で一般的な処理)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE opengl32)
    # MSVC環境（Windows）のとき、コンソール画面を非表示にする
    if(MSVC)
        target_link_options(${PROJECT_NAME} PRIVATE /SUBSYSTEM:WINDOWS)
    endif()
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE GL)
endif()
