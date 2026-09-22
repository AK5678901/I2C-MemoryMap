#pragma once

#include <filesystem>
#include <optional>
#include <string>

[[nodiscard]] std::optional<std::filesystem::path> openCsvFileDialog();
[[nodiscard]] std::string pathToUtf8(const std::filesystem::path& path);
