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
#include <algorithm>

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
    if (!std::getline(file, line))
        throw std::runtime_error("CSV header is missing");
    if (line.starts_with("\xEF\xBB\xBF"))
        line.erase(0, 3); // UTF-8 BOM precedes the first column name.
    const auto header = csvFields(line);
    const auto column = [&](const std::string& name) {
        auto found = std::ranges::find(header, name);
        if (found == header.end() && name == "start_time")
            found = std::ranges::find(header, "time"); // Legacy CSV header.
        if (found == header.end())
            throw std::runtime_error("CSV header is missing column: " + name);
        if (std::find(std::next(found), header.end(), *found) != header.end())
            throw std::runtime_error("CSV header has duplicate column: " + name);
        return static_cast<std::size_t>(std::distance(header.begin(), found));
    };
    const auto name_column = column("name");
    const auto type_column = column("type");
    const auto time_column = column("start_time");
    const auto address_column = column("address");
    const auto read_column = column("read");
    const auto data_column = column("data");
    const auto last_column = std::max({name_column, type_column, time_column, address_column, read_column, data_column});
    std::map<std::string, Timestamp> last_times;
    std::size_t line_number = 1;

    while (std::getline(file, line))
    {
        ++line_number;
        if (line.empty())
            continue;
        const auto fields = csvFields(line);
        if (fields.size() <= last_column)
            throw std::runtime_error("CSV line " + std::to_string(line_number) + ": required column is missing");
        const auto& name = fields[name_column];
        const auto& type = fields[type_column];
        const auto& timestamp_text = fields[time_column];
        const auto& address_text = fields[address_column];
        const auto& read_text = fields[read_column];
        const auto& data_text = fields[data_column];
        const auto parse_byte = [&](const std::string& text, const char* field, unsigned long maximum) {
            try
            {
                std::size_t consumed = 0;
                const auto value = std::stoul(text, &consumed, 16);
                if (consumed == text.size() && value <= maximum && text.find('-') == std::string::npos)
                    return static_cast<std::uint8_t>(value);
            }
            catch (const std::invalid_argument&) {}
            catch (const std::out_of_range&) {}
            throw std::runtime_error("CSV line " + std::to_string(line_number) + " (bus " + name +
                                     "): invalid " + field + " value '" + text + "' (expected hexadecimal)");
        };
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
            const auto address = parse_byte(address_text, "address", 127);
            emit({I2CEvent::Type::Address, timestamp, address, read_text == "true"});
        }
        else if (type == "data")
        {
            const auto data = parse_byte(data_text, "data", 255);
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
