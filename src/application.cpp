#include "application.hpp"

#include "csv_processor.hpp"
#include "device_config_loader.hpp"
#include "windows_helpers.hpp"

#define NOMINMAX
#include <windows.h>

#include <GLFW/glfw3.h>

#include <format>

int Application::run()
{
    if (!gui_.isReady())
        return -1;
    if (!loadConfiguration())
        return 0;
    if (!selectAndLoadLog())
        return 0;
    runMainLoop();
    return 0;
}

bool Application::loadConfiguration()
{
    if (loadDeviceConfigs(devicemanager_))
        return true;
    MessageBoxW(nullptr, L"Failed to load JSON files from the 'devices' directory.", L"Error", MB_OK | MB_ICONERROR);
    return false;
}

bool Application::selectAndLoadLog()
{
    const auto csv_path = openCsvFileDialog();
    if (!csv_path)
        return false;

    log_ = importCsvLog(devicemanager_, *csv_path);
    main_window_.reset(log_);
    const auto title = std::format("I2C_MemoryMap - [{}]", pathToUtf8(*csv_path));
    glfwSetWindowTitle(&gui_.window(), title.c_str());
    return true;
}

void Application::runMainLoop()
{
    bool render_follow_up_frame = true;
    while (!gui_.shouldClose())
    {
        gui_.beginFrame();
        main_window_.render(devicemanager_, log_);
        gui_.endFrame();
        if (gui_.shouldClose())
            break;
        if (render_follow_up_frame)
        {
            render_follow_up_frame = false;
            continue;
        }
        gui_.waitForEvents();
        render_follow_up_frame = true;
    }
}
