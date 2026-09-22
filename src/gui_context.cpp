#include "gui_context.hpp"

#include <GLFW/glfw3.h>

#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#include <windows.h>
#include "app_icon_resource.h"
#endif

#include "imgui.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"

#include <stdexcept>

GuiContext::GuiContext()
{
    if (!glfwInit())
        return;
    glfw_initialized_ = true;
    window_ = glfwCreateWindow(1920, 1080, "I2C_MemoryMap", nullptr, nullptr);
    if (window_ == nullptr)
        return;

#ifdef _WIN32
    const auto module = GetModuleHandleW(nullptr);
    const auto large_icon = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON),
                                                         LR_SHARED));
    const auto small_icon = static_cast<HICON>(LoadImageW(module, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
                                                         GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                                         LR_SHARED));
    const auto hwnd = glfwGetWin32Window(window_);
    if (large_icon != nullptr)
        SendMessageW(hwnd, WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(large_icon));
    if (small_icon != nullptr)
        SendMessageW(hwnd, WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(small_icon));
#endif

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imgui_context_created_ = true;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    ImGui::GetIO().IniFilename = nullptr;
    glfw_backend_initialized_ = ImGui_ImplGlfw_InitForOpenGL(window_, true);
    opengl_backend_initialized_ = ImGui_ImplOpenGL3_Init("#version 130");
}

GuiContext::~GuiContext()
{
    if (opengl_backend_initialized_)
        ImGui_ImplOpenGL3_Shutdown();
    if (glfw_backend_initialized_)
        ImGui_ImplGlfw_Shutdown();
    if (imgui_context_created_)
        ImGui::DestroyContext();
    if (window_ != nullptr)
        glfwDestroyWindow(window_);
    if (glfw_initialized_)
        glfwTerminate();
}

bool GuiContext::isReady() const noexcept
{
    return window_ != nullptr && glfw_backend_initialized_ && opengl_backend_initialized_;
}

bool GuiContext::shouldClose() const
{
    return !isReady() || glfwWindowShouldClose(window_);
}

GLFWwindow& GuiContext::window() const
{
    if (window_ == nullptr)
    {
        throw std::logic_error("GUI window is not initialized.");
    }
    return *window_;
}

void GuiContext::beginFrame()
{
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void GuiContext::endFrame()
{
    ImGui::Render();
    int display_width = 0;
    int display_height = 0;
    glfwGetFramebufferSize(window_, &display_width, &display_height);
    glViewport(0, 0, display_width, display_height);
    glClearColor(0.2F, 0.2F, 0.2F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window_);
}

void GuiContext::waitForEvents()
{
    glfwWaitEvents();
}
