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
    const int mode = MessageBoxW(nullptr, L"Saleae Logic 2 live mode?\nYes: live receive\nNo: open CSV\nCancel: exit",
                                 L"I2C MemoryMap", MB_YESNOCANCEL | MB_ICONQUESTION);
    if (mode == IDCANCEL)
        return 0;
    if (mode == IDYES)
    {
        live_receiver_ = std::make_unique<LiveReceiver>();
        if (!live_receiver_->ready())
        {
            MessageBoxW(nullptr, L"Could not listen on UDP port 48152.", L"Error", MB_OK | MB_ICONERROR);
            return 1;
        }
        main_window_.reset(processor_.GetInfo());
        glfwSetWindowTitle(&gui_.window(), "I2C_MemoryMap - Saleae Logic 2 live");
    }
    else if (!selectAndLoadLog())
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

    importCsvLog(devicemanager_, processor_, *csv_path);
    main_window_.reset(processor_.GetInfo());
    const auto title = std::format("I2C_MemoryMap - [{}]", pathToUtf8(*csv_path));
    glfwSetWindowTitle(&gui_.window(), title.c_str());
    return true;
}

void Application::runMainLoop()
{
    bool render_follow_up_frame = true;
    while (!gui_.shouldClose())
    {
        if (live_receiver_)
        {
            bool reset = false;
            const bool changed = live_receiver_->poll(devicemanager_, processor_, reset);
            if (reset)
                main_window_.reset(processor_.GetInfo());
            if (changed)
                main_window_.refreshLive(devicemanager_, processor_.GetInfo());
        }
        gui_.beginFrame();
        main_window_.render(devicemanager_, processor_.GetInfo(), live_receiver_.get());
        gui_.endFrame();
        if (gui_.shouldClose())
            break;
        if (render_follow_up_frame)
        {
            render_follow_up_frame = false;
            continue;
        }
        if (live_receiver_)
            glfwWaitEventsTimeout(0.03);
        else
            gui_.waitForEvents();
        render_follow_up_frame = true;
    }
}
