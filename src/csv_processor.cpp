#include "csv_processor.hpp"
#include "i2c_event_processor.hpp"

#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>

void importCsvLog(I2CDeviceManager& devicemanager, I2CEventProcessor& processor, const std::filesystem::path& filename)
{
    if (!std::filesystem::exists(filename))
    {
        return;
    }

    std::ifstream file(filename);
    if (!file)
    {
        return;
    }

    std::string line;
    std::getline(file, line);
    processor.reset(devicemanager);
    processor.SetCsvTimeFormat(true);

    while (std::getline(file, line))
    {
        std::stringstream row(line);
        std::string name;
        std::string type;
        std::string acknowledgement;
        std::string address_text;
        std::string read_text;
        std::string data_text;
        std::string timestamp_text;
        std::string duration_text;

        std::getline(row, name, ',');
        std::getline(row, type, ',');
        std::getline(row, timestamp_text, ',');
        std::getline(row, duration_text, ',');
        std::getline(row, acknowledgement, ',');
        std::getline(row, address_text, ',');
        std::getline(row, read_text, ',');
        std::getline(row, data_text, ',');

        if (timestamp_text.size() >= 2 && timestamp_text.front() == '"' && timestamp_text.back() == '"')
            timestamp_text = timestamp_text.substr(1, timestamp_text.size() - 2);
        auto parsed_timestamp = TimeValue::parse(timestamp_text);
        if (!parsed_timestamp)
        {
            // Legacy Saleae exports use capture-relative seconds, with no absolute origin.
            try
            {
                std::size_t consumed = 0;
                const auto seconds = std::stod(timestamp_text, &consumed);
                if (consumed == timestamp_text.size() && std::isfinite(seconds) &&
                    seconds >= static_cast<double>(std::numeric_limits<Timestamp>::min()) / 1000000000.0 &&
                    seconds <= static_cast<double>(std::numeric_limits<Timestamp>::max()) / 1000000000.0)
                {
                    parsed_timestamp = static_cast<Timestamp>(std::llround(seconds * 1000000000.0));
                    processor.SetCsvTimeFormat(false);
                }
            }
            catch (const std::exception&)
            {
            }
        }
        if (!parsed_timestamp)
            continue;
        const Timestamp timestamp = *parsed_timestamp;
        processor.RecordCsvTimestamp(timestamp, timestamp_text);

        if (type == "\"address\"")
        {
            const auto address = static_cast<std::uint8_t>(std::stoul(address_text, nullptr, 16));
            processor.process({I2CEvent::Type::Address, timestamp, address, read_text == "true"},
                              devicemanager);
        }
        else if (type == "\"data\"")
        {
            const auto data = static_cast<std::uint8_t>(std::stoul(data_text, nullptr, 16));
            processor.process({I2CEvent::Type::Data, timestamp, data}, devicemanager);
        }
        else if (type == "\"stop\"")
        {
            processor.process({I2CEvent::Type::Stop, timestamp}, devicemanager);
        }
    }

    return;
}
