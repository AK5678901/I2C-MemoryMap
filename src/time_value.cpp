#include "time_value.hpp"
#include "log_data.hpp"

#include <charconv>
#include <chrono>
#include <cmath>
#include <format>
#include <limits>

namespace
{
std::string formatCalendar(Timestamp value, bool local, bool short_format);

bool digits(std::string_view value, std::size_t start, std::size_t count, unsigned& result)
{
    if (start + count > value.size())
        return false;
    const auto parsed = std::from_chars(value.data() + start, value.data() + start + count, result);
    return parsed.ec == std::errc{} && parsed.ptr == value.data() + start + count;
}
}

std::optional<Timestamp> TimeValue::parse(std::string_view value)
{
    // ISO 8601: YYYY-MM-DDTHH:MM:SS[.fraction](Z|+HH:MM|-HH:MM).
    if (value.size() < 20 || value[4] != '-' || value[7] != '-' ||
        (value[10] != 'T' && value[10] != 't') || value[13] != ':' || value[16] != ':')
        return std::nullopt;
    unsigned year = 0, month = 0, day = 0, hour = 0, minute = 0, second = 0;
    if (!digits(value, 0, 4, year) || !digits(value, 5, 2, month) || !digits(value, 8, 2, day) ||
        !digits(value, 11, 2, hour) || !digits(value, 14, 2, minute) || !digits(value, 17, 2, second) ||
        hour > 23 || minute > 59 || second > 59)
        return std::nullopt;
    const auto date = std::chrono::year{static_cast<int>(year)} / month / day;
    if (!date.ok())
        return std::nullopt;
    std::size_t pos = 19;
    Timestamp fraction = 0;
    if (pos < value.size() && value[pos] == '.')
    {
        ++pos;
        const auto start = pos;
        unsigned count = 0;
        while (pos < value.size() && value[pos] >= '0' && value[pos] <= '9')
        {
            if (count < 9)
                fraction = fraction * 10 + (value[pos] - '0');
            ++count;
            ++pos;
        }
        if (pos == start)
            return std::nullopt;
        while (count++ < 9)
            fraction *= 10;
    }
    int offset_seconds = 0;
    if (pos < value.size() && (value[pos] == 'Z' || value[pos] == 'z'))
        ++pos;
    else if (pos + 6 == value.size() && (value[pos] == '+' || value[pos] == '-') && value[pos + 3] == ':')
    {
        unsigned offset_hours = 0, offset_minutes = 0;
        if (!digits(value, pos + 1, 2, offset_hours) || !digits(value, pos + 4, 2, offset_minutes) ||
            offset_hours > 23 || offset_minutes > 59)
            return std::nullopt;
        offset_seconds = static_cast<int>(offset_hours * 3600 + offset_minutes * 60);
        if (value[pos] == '-')
            offset_seconds = -offset_seconds;
        pos += 6;
    }
    else
        return std::nullopt;
    if (pos != value.size())
        return std::nullopt;
    const auto base = std::chrono::sys_days{date}.time_since_epoch().count() * 86400 +
                      static_cast<std::int64_t>(hour) * 3600 + minute * 60 + second - offset_seconds;
    if (base > (std::numeric_limits<Timestamp>::max() - fraction) / 1000000000 ||
        base < std::numeric_limits<Timestamp>::min() / 1000000000)
        return std::nullopt;
    return base * 1000000000 + fraction;
}

namespace
{
std::optional<Timestamp> parseSeconds(std::string_view text)
{
    bool negative = false;
    if (!text.empty() && (text.front() == '-' || text.front() == '+'))
    {
        negative = text.front() == '-';
        text.remove_prefix(1);
    }
    const auto dot = text.find('.');
    const auto whole_text = text.substr(0, dot);
    if (whole_text.empty())
        return std::nullopt;
    std::uint64_t whole = 0;
    const auto parsed = std::from_chars(whole_text.data(), whole_text.data() + whole_text.size(), whole);
    if (parsed.ec != std::errc{} || parsed.ptr != whole_text.data() + whole_text.size())
        return std::nullopt;
    Timestamp fraction = 0;
    if (dot != std::string_view::npos)
    {
        const auto decimals = text.substr(dot + 1);
        if (decimals.empty() || decimals.size() > 9)
            return std::nullopt;
        unsigned value = 0;
        if (!digits(decimals, 0, decimals.size(), value))
            return std::nullopt;
        fraction = value;
        for (std::size_t i = decimals.size(); i < 9; ++i)
            fraction *= 10;
    }
    constexpr std::uint64_t billion = 1000000000;
    const auto limit = static_cast<std::uint64_t>(std::numeric_limits<Timestamp>::max()) + (negative ? 1ULL : 0ULL);
    if (whole > limit / billion || (whole == limit / billion && static_cast<std::uint64_t>(fraction) > limit % billion))
        return std::nullopt;
    const auto magnitude = whole * billion + static_cast<std::uint64_t>(fraction);
    if (negative && magnitude == static_cast<std::uint64_t>(std::numeric_limits<Timestamp>::max()) + 1ULL)
        return std::numeric_limits<Timestamp>::min();
    return negative ? -static_cast<Timestamp>(magnitude) : static_cast<Timestamp>(magnitude);
}

std::optional<Timestamp> parseLocal(std::string_view text)
{
    const auto wall = TimeValue::parse(std::string{text} + "Z");
    if (!wall)
        return std::nullopt;
    try
    {
        const auto local = std::chrono::local_time<std::chrono::nanoseconds>{std::chrono::nanoseconds{*wall}};
        return std::chrono::current_zone()->to_sys(local, std::chrono::choose::earliest)
            .time_since_epoch().count();
    }
    catch (const std::chrono::ambiguous_local_time&)
    {
        return std::nullopt;
    }
    catch (const std::chrono::nonexistent_local_time&)
    {
        return std::nullopt;
    }
}
} // namespace

std::optional<Timestamp> TimeValue::DisplayView::parseDisplayed(std::string_view text, Timestamp reference) const
{
    switch (format_)
    {
    case DisplayFormat::IsoUtc: return parse(text);
    case DisplayFormat::IsoLocal: return parseLocal(text);
    case DisplayFormat::SinceFirstEvent:
    {
        const auto elapsed = parseSeconds(text);
        const Timestamp origin = log_.timestamps.empty() ? 0 : log_.timestamps.front();
        if (!elapsed || (*elapsed > 0 && origin > std::numeric_limits<Timestamp>::max() - *elapsed) ||
            (*elapsed < 0 && origin < std::numeric_limits<Timestamp>::min() - *elapsed))
            return std::nullopt;
        return origin + *elapsed;
    }
    case DisplayFormat::RawCsv:
        if (const auto iso = parse(text))
            return iso;
        return parseSeconds(text);
    case DisplayFormat::ShortUtc:
    case DisplayFormat::ShortLocal:
    {
        const bool local = format_ == DisplayFormat::ShortLocal;
        if (text.size() != 18 || text[2] != ':' || text[5] != ':' || text[8] != '.')
            return std::nullopt;
        const auto reference_text = local ? formatCalendar(reference, true, false) : formatIso(reference);
        const auto reference_day = parse(reference_text.substr(0, 10) + "T00:00:00Z");
        if (!reference_day)
            return std::nullopt;
        std::optional<Timestamp> nearest;
        for (int delta = -1; delta <= 1; ++delta)
        {
            const auto date = formatIso(*reference_day + static_cast<Timestamp>(delta) * 86400000000000).substr(0, 10);
            const auto full = date + "T" + std::string{text};
            const auto candidate = local ? parseLocal(full) : parse(full + "Z");
            if (candidate && (!nearest || std::abs(static_cast<double>(*candidate) - reference) <
                                           std::abs(static_cast<double>(*nearest) - reference)))
                nearest = candidate;
        }
        return nearest;
    }
    }
    return std::nullopt;
}

namespace
{
std::string formatCalendar(Timestamp value, bool local, bool short_format)
{
    constexpr Timestamp billion = 1000000000;
    auto seconds = value / billion;
    auto fraction = value % billion;
    if (fraction < 0)
    {
        fraction += billion;
        --seconds;
    }
    if (local)
    {
        const auto info = std::chrono::current_zone()->get_info(
            std::chrono::sys_seconds{std::chrono::seconds{seconds}});
        const auto offset = info.offset.count();
        seconds += offset;
    }
    const auto time = std::chrono::sys_seconds{std::chrono::seconds{seconds}};
    const auto day = std::chrono::floor<std::chrono::days>(time);
    const auto date = std::chrono::year_month_day{day};
    const auto within_day = std::chrono::hh_mm_ss{time - day};
    if (short_format)
        return std::format("{:02}:{:02}:{:02}.{:09}", within_day.hours().count(), within_day.minutes().count(),
                           within_day.seconds().count(), fraction);
    return std::format("{:04}-{:02}-{:02}T{:02}:{:02}:{:02}.{:09}{}", int(date.year()),
                       unsigned(date.month()), unsigned(date.day()), within_day.hours().count(),
                       within_day.minutes().count(), within_day.seconds().count(), fraction, local ? "" : "Z");
}

std::string formatSeconds(Timestamp value)
{
    constexpr Timestamp billion = 1000000000;
    const bool negative = value < 0;
    const auto magnitude = negative ? static_cast<std::uint64_t>(-(value + 1)) + 1
                                    : static_cast<std::uint64_t>(value);
    return std::format("{}{}.{:09}", negative ? "-" : "", magnitude / billion, magnitude % billion);
}
} // namespace

std::string TimeValue::formatIso(Timestamp value) { return formatCalendar(value, false, false); }

std::string TimeValue::formatElapsed(Timestamp value, Timestamp base)
{
    return formatSeconds(value - base);
}

std::string TimeValue::DisplayView::formatTimestamp(Timestamp value) const
{
    switch (format_)
    {
    case DisplayFormat::IsoUtc: return formatIso(value);
    case DisplayFormat::IsoLocal: return formatCalendar(value, true, false);
    case DisplayFormat::ShortUtc: return formatCalendar(value, false, true);
    case DisplayFormat::ShortLocal: return formatCalendar(value, true, true);
    case DisplayFormat::SinceFirstEvent:
        return formatElapsed(value, log_.timestamps.empty() ? 0 : log_.timestamps.front());
    case DisplayFormat::RawCsv:
        if (const auto found = log_.raw_csv_timestamps.find(value); found != log_.raw_csv_timestamps.end())
            return found->second;
        return log_.has_csv_timestamps && !log_.has_absolute_timestamps && !log_.raw_csv_timestamps.empty()
                   ? formatSeconds(value) : formatIso(value);
    }
    return {};
}

double TimeValue::durationSeconds(Timestamp nanoseconds)
{
    return static_cast<double>(nanoseconds) / 1000000000.0;
}
