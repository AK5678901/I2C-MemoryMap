#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "timestamp.hpp"
#include "timeline_info.hpp"

namespace TimeValue
{
enum class DisplayFormat { IsoUtc, IsoLocal, ShortUtc, ShortLocal, SinceFirstEvent, RawCsv };

[[nodiscard]] std::optional<Timestamp> parse(std::string_view text);
[[nodiscard]] std::string formatIso(Timestamp timestamp);
[[nodiscard]] std::string formatElapsed(Timestamp timestamp, Timestamp origin);
[[nodiscard]] double durationSeconds(Timestamp nanoseconds);

class DisplayView
{
  public:
    DisplayView(const TimelineInfo& info, DisplayFormat format) : info_(info), format_(format) {}
    [[nodiscard]] DisplayFormat format() const { return format_; }
    [[nodiscard]] std::string formatTimestamp(Timestamp timestamp) const;
    [[nodiscard]] std::optional<Timestamp> parseDisplayed(std::string_view text, Timestamp reference) const;

  private:
    const TimelineInfo& info_;
    DisplayFormat format_;
};
} // namespace TimeValue
