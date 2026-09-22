#pragma once

struct GLFWwindow;

class GuiContext final
{
  public:
    GuiContext();
    ~GuiContext();

    GuiContext(const GuiContext&) = delete;
    GuiContext& operator=(const GuiContext&) = delete;
    GuiContext(GuiContext&&) = delete;
    GuiContext& operator=(GuiContext&&) = delete;

    [[nodiscard]] bool isReady() const noexcept;
    [[nodiscard]] bool shouldClose() const;
    [[nodiscard]] GLFWwindow& window() const;

    void beginFrame();
    void endFrame();
    void waitForEvents();

  private:
    GLFWwindow* window_{nullptr};
    bool glfw_initialized_{false};
    bool imgui_context_created_{false};
    bool glfw_backend_initialized_{false};
    bool opengl_backend_initialized_{false};
};
