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
        main_window_.reset(bus_manager_.GetInfo());
        glfwSetWindowTitle(&gui_.window(), "I2C_MemoryMap - Saleae Logic 2 live");
    }
    else if (!selectAndLoadLog())
        return 0;
    runMainLoop();
    return 0;
}

bool Application::loadConfiguration()
{
    std::string error;
    if (loadDeviceConfigs(bus_manager_, "devices", &error))
        return true;
    const int length = MultiByteToWideChar(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), nullptr, 0);
    std::wstring detail(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, error.data(), static_cast<int>(error.size()), detail.data(), length);
    const auto message = L"Failed to load device configuration.\n\n" + detail;
    MessageBoxW(nullptr, message.c_str(), L"Error", MB_OK | MB_ICONERROR);
    return false;
}

bool Application::selectAndLoadLog()
{
    const auto csv_path = openCsvFileDialog();
    if (!csv_path)
        return false;

    try
    {
        importCsvLog(bus_manager_, *csv_path);
    }
    catch (const std::exception& error)
    {
        MessageBoxA(nullptr, error.what(), "CSV import error", MB_OK | MB_ICONERROR);
        return false;
    }
    main_window_.reset(bus_manager_.GetInfo());
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
            const bool changed = live_receiver_->poll(bus_manager_, reset);
            if (reset)
                main_window_.reset(bus_manager_.GetInfo());
            if (changed)
                main_window_.refreshLive(bus_manager_, bus_manager_.GetInfo());
        }
        gui_.beginFrame();
        main_window_.render(bus_manager_, bus_manager_.GetInfo(), live_receiver_.get());
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
