#include "csv_processor.hpp"
#include "i2c_event_processor.hpp"

#include <cstdint>
#include <cmath>
#include <fstream>
#include <limits>
#include <string>
#include <functional>
#include <stdexcept>
#include <vector>

namespace
{
// Decode quoted fields, including commas and doubled quotes in analyzer names.
std::vector<std::string> csvFields(const std::string& line)
{
    std::vector<std::string> fields(1);
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i)
    {
        const char ch = line[i];
        if (ch == '"')
        {
            if (quoted && i + 1 < line.size() && line[i + 1] == '"')
            {
                fields.back() += '"';
                ++i;
            }
            else
                quoted = !quoted;
        }
        else if (ch == ',' && !quoted)
            fields.emplace_back();
        else if (ch != '\r' || quoted)
            fields.back() += ch;
    }
    if (quoted)
        throw std::runtime_error("Unterminated CSV quoted field");
    return fields;
}

void readCsv(const std::filesystem::path& filename,
             const std::function<void(const std::string&, const I2CEvent&, const std::string&, bool)>& consume)
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
    std::map<std::string, Timestamp> last_times;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;
        const auto fields = csvFields(line);
        if (fields.size() < 8)
            throw std::runtime_error("CSV row requires at least eight columns");
        const auto& name = fields[0];
        const auto& type = fields[1];
        const auto& timestamp_text = fields[2];
        const auto& address_text = fields[5];
        const auto& read_text = fields[6];
        const auto& data_text = fields[7];
        if (name.empty())
            throw std::runtime_error("CSV bus name is empty");
        bool absolute = true;
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
                    absolute = false;
                }
            }
            catch (const std::exception&)
            {
            }
        }
        if (!parsed_timestamp)
            continue;
        const Timestamp timestamp = *parsed_timestamp;
        if (const auto previous = last_times.find(name); previous != last_times.end() && timestamp < previous->second)
            throw std::runtime_error("CSV time moves backwards on bus: " + name);
        last_times[name] = timestamp;
        const auto emit = [&](const I2CEvent& event) { consume(name, event, timestamp_text, absolute); };

        if (type == "address")
        {
            const auto address = static_cast<std::uint8_t>(std::stoul(address_text, nullptr, 16));
            emit({I2CEvent::Type::Address, timestamp, address, read_text == "true"});
        }
        else if (type == "data")
        {
            const auto data = static_cast<std::uint8_t>(std::stoul(data_text, nullptr, 16));
            emit({I2CEvent::Type::Data, timestamp, data});
        }
        else if (type == "stop")
        {
            emit({I2CEvent::Type::Stop, timestamp});
        }
    }

    return;
}

} // namespace

void importCsvLog(I2CBusManager& bus_manager, const std::filesystem::path& filename)
{
    bus_manager.reset();
    readCsv(filename, [&](const std::string& name, const I2CEvent& event, const std::string& raw, bool absolute) {
        bus_manager.processCsvEvent(name, event, raw, absolute);
    });
}
