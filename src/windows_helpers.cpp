#include "windows_helpers.hpp"

#define NOMINMAX
#include <windows.h>

// commdlg.h requires the Windows types declared by windows.h.
#include <commdlg.h>

#include <array>

std::optional<std::filesystem::path> openCsvFileDialog()
{
    std::array<wchar_t, MAX_PATH> filename{};
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.lpstrFilter = L"CSV Files (*.csv)\0*.csv\0All Files (*.*)\0*.*\0";
    dialog.lpstrFile = filename.data();
    dialog.nMaxFile = static_cast<DWORD>(filename.size());
    dialog.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameW(&dialog) != TRUE)
    {
        return std::nullopt;
    }
    return std::filesystem::path(filename.data());
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    const auto& value = path.native();
    const auto size =
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (size > 0)
    {
        WideCharToMultiByte(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), size, nullptr,
                            nullptr);
    }
    return result;
}
