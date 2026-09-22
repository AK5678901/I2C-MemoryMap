#include "time_value.hpp"
#include "log_data.hpp"

#include <stdexcept>
#include <cstdio>
#include <unordered_map>

void check(bool value, int line)
{
    if (!value)
    {
        std::fprintf(stderr, "Time value check failed at line %d\n", line);
        throw std::runtime_error("Time value check failed at line " + std::to_string(line));
    }
}
#define check(value) check((value), __LINE__)

int main()
{
    LogData log;
    TimeValue::DisplayFormat format = TimeValue::DisplayFormat::ShortLocal;
    const auto display = [&] { return TimeValue::DisplayView(log, format); };
    const auto time = TimeValue::parse("2026-09-22T08:20:18.204048396+00:00");
    check(time.has_value());
    check(TimeValue::formatIso(*time) == "2026-09-22T08:20:18.204048396Z");
    check(TimeValue::parse("2026-09-22T17:20:18.204048396+09:00") == time);
    check(TimeValue::parse("2026-09-22T08:20:18.204048396000Z") == time);
    check(!TimeValue::parse("2026-02-30T08:20:18Z"));
    check(!TimeValue::parse("2026-09-22T08:20:18"));
    log.timestamps.push_back(*time);
    format = TimeValue::DisplayFormat::SinceFirstEvent;
    check(display().formatTimestamp(*time + 123456789) == "0.123456789");
    format = TimeValue::DisplayFormat::ShortUtc;
    check(display().formatTimestamp(*time) == "08:20:18.204048396");
    format = TimeValue::DisplayFormat::IsoLocal;
    check(display().formatTimestamp(*time).find('+') == std::string::npos);
    check(display().formatTimestamp(*time).find('Z') == std::string::npos);
    format = TimeValue::DisplayFormat::ShortLocal;
    const auto short_local = display().formatTimestamp(*time);
    check(short_local.size() == 18 && short_local[8] == '.' &&
          short_local.find('+') == std::string::npos);
    check(display().parseDisplayed(short_local, *time) == time);
    format = TimeValue::DisplayFormat::ShortUtc;
    check(display().parseDisplayed(display().formatTimestamp(*time), *time) == time);
    format = TimeValue::DisplayFormat::IsoLocal;
    check(display().parseDisplayed(display().formatTimestamp(*time), *time) == time);
    format = TimeValue::DisplayFormat::SinceFirstEvent;
    check(display().parseDisplayed("0.123456789", *time) == *time + 123456789);
    const std::unordered_map<Timestamp, std::string> original{{*time, "2026-09-22T08:20:18.204048396+00:00"}};
    log.has_csv_timestamps = true;
    log.raw_csv_timestamps = original;
    format = TimeValue::DisplayFormat::RawCsv;
    check(display().formatTimestamp(*time) == original.at(*time));
    check(display().formatTimestamp(*time + 1) == TimeValue::formatIso(*time + 1));
    const std::unordered_map<Timestamp, std::string> relative{{1000000000, "1.0"}};
    log.raw_csv_timestamps = relative;
    log.has_absolute_timestamps = false;
    check(display().formatTimestamp(1500000000) == "1.500000000");
    check(display().parseDisplayed(display().formatTimestamp(1500000000), 0) == 1500000000);
    const TimeValue::DisplayView second_display(log, TimeValue::DisplayFormat::ShortUtc);
    check(second_display.formatTimestamp(*time) == "08:20:18.204048396");
    check(display().formatTimestamp(1500000000) == "1.500000000");
    format = TimeValue::DisplayFormat::SinceFirstEvent;
    log.timestamps.front() = 1000000000;
    check(display().formatTimestamp(1500000000) == "0.500000000");
}
