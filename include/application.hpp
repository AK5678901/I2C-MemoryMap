#pragma once

#include "gui_context.hpp"
#include "i2c_device.hpp"
#include "i2c_event_processor.hpp"
#include "main_window.hpp"
#include "live_receiver.hpp"

#include <memory>

class Application final
{
  public:
    [[nodiscard]] int run();

  private:
    [[nodiscard]] bool loadConfiguration();
    [[nodiscard]] bool selectAndLoadLog();
    void runMainLoop();

    GuiContext gui_;
    I2CDeviceManager devicemanager_;
    I2CEventProcessor processor_;
    MainWindow main_window_;
    std::unique_ptr<LiveReceiver> live_receiver_;
};
