module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"

#include <filesystem>
#include <fstream>
#include <vector>
#include <system_error>
#include <chrono>
#include <optional>
#include <string>
#include <atomic>
#include <charconv>
#include <algorithm>
#include <cctype>
#include <string_view>
#include <cwchar>
#include <ctime>

export module ui:filesystem;

import :common;       // partition where kModelFileName lives
import pixelai;

namespace ui::detail
{
    using pixelai::PixelRecognizer;

    export std::filesystem::path GetAppDirectory()
    {
        wchar_t buf[MAX_PATH]{};
        DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);

        if (len == 0 || len == MAX_PATH)
            return std::filesystem::current_path();

        std::filesystem::path p(buf);
        return p.remove_filename();
    }

    export std::filesystem::path GetCaptureDirectory()
    {
        auto dir = GetAppDirectory() / "captures";
        std::error_code ec{};
        std::filesystem::create_directories(dir, ec);
        return dir;
    }

    export std::filesystem::path GetModelFilePath()
    {
        // FIX: namespace-qualified
        return GetAppDirectory() / ui::detail::kModelFileName;
    }

    export bool LoadModel(PixelRecognizer& model)
    {
        auto path = GetModelFilePath();
        if (!std::filesystem::exists(path))
            return false;

        return model.load_from_file(path.string());
    }

    namespace
    {
        std::wstring MakeBackupFileName(const std::wstring& stem,
                                        const std::wstring& ext)
        {
            const auto ts = std::chrono::system_clock::to_time_t(
                std::chrono::system_clock::now());

            std::tm tm{};
            localtime_s(&tm, &ts);

            wchar_t name[256]{};
            swprintf(name, 256,
                L"%s_%04d%02d%02d_%02d%02d%02d%s",
                stem.c_str(),
                tm.tm_year + 1900,
                tm.tm_mon + 1,
                tm.tm_mday,
                tm.tm_hour,
                tm.tm_min,
                tm.tm_sec,
                ext.c_str());

            return name;
        }

        std::optional<int> ParseBackupRetention(std::string_view value)
        {
            value.remove_prefix(std::min(value.find_first_not_of(" \t"), value.size()));
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            {
                value.remove_suffix(1);
            }

            int parsed{};
            const auto* begin = value.data();
            const auto* end   = value.data() + value.size();
            const auto result = std::from_chars(begin, end, parsed);

            if (result.ec == std::errc{} && result.ptr == end && parsed > 0)
                return parsed;

            return std::nullopt;
        }

        int LoadBackupRetention()
        {
            static std::optional<int> cached{};
            if (cached)
                return *cached;

            int retention = kDefaultBackupRetention;

            const auto iniPath = GetAppDirectory() / "pixelai.ini";
            std::ifstream ifs(iniPath);
            if (!ifs)
            {
                cached = retention;
                return retention;
            }

            std::string line;
            while (std::getline(ifs, line))
            {
                std::string_view view(line);

                auto trim = [](std::string_view sv) -> std::string_view
                {
                    const auto start = sv.find_first_not_of(" \t");
                    if (start == std::string_view::npos)
                        return {};

                    const auto end = sv.find_last_not_of(" \t");
                    return sv.substr(start, end - start + 1);
                };

                view = trim(view);
                if (view.empty() || view.front() == '#' || view.front() == ';')
                    continue;

                const auto eq = view.find('=');
                if (eq == std::string_view::npos)
                    continue;

                auto key = trim(view.substr(0, eq));
                auto val = trim(view.substr(eq + 1));

                std::string keyLower(key);
                std::transform(keyLower.begin(), keyLower.end(), keyLower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

                if (keyLower == "backupretention")
                {
                    if (auto parsed = ParseBackupRetention(val))
                    {
                        retention = *parsed;
                        break;
                    }
                }
            }

            cached = retention;
            return retention;
        }

        bool IsBackupFile(const std::filesystem::path& path,
                          const std::wstring& stem,
                          const std::wstring& ext)
        {
            const auto filename = path.filename().wstring();
            if (filename == std::filesystem::path(kModelFileName).filename())
                return false;

            const std::wstring prefix = stem + L"_";
            if (!std::wstring_view(filename).starts_with(prefix))
                return false;

            return std::wstring_view(filename).ends_with(ext);
        }

        void PruneBackups(std::size_t retention,
                          const std::filesystem::path& appDir,
                          const std::wstring& stem,
                          const std::wstring& ext)
        {
            std::error_code ec{};
            std::vector<std::pair<std::filesystem::file_time_type, std::filesystem::path>> backups;

            for (const auto& entry : std::filesystem::directory_iterator(appDir, ec))
            {
                if (ec)
                    break;

                if (!entry.is_regular_file())
                    continue;

                const auto& path = entry.path();
                if (!IsBackupFile(path, stem, ext))
                    continue;

                const auto time = std::filesystem::last_write_time(path, ec);
                if (ec)
                    continue;

                backups.emplace_back(time, path);
            }

            if (backups.size() <= retention)
                return;

            std::sort(backups.begin(), backups.end(),
                [](const auto& a, const auto& b)
                {
                    return a.first < b.first;
                });

            const auto toDelete = backups.size() - retention;
            for (std::size_t i = 0; i < toDelete; ++i)
            {
                std::filesystem::remove(backups[i].second, ec);
            }
        }
    }

    export bool SaveModel(const PixelRecognizer& model)
    {
        auto path = GetModelFilePath();
        if (!model.save_to_file(path.string()))
            return false;

        const int retention = LoadBackupRetention();
        if (retention <= 0)
            return true;

        const auto modelPath = std::filesystem::path(kModelFileName);
        const auto stem = modelPath.stem().wstring();
        const auto ext  = modelPath.extension().wstring();
        const auto backupPath = GetAppDirectory() / MakeBackupFileName(stem, ext);

        std::error_code ec{};
        std::filesystem::copy_file(path, backupPath,
            std::filesystem::copy_options::overwrite_existing, ec);

        if (!ec)
        {
            PruneBackups(static_cast<std::size_t>(retention), GetAppDirectory(), stem, ext);
        }

        return true;
    }

    export std::optional<std::filesystem::path>
        SaveCaptureToDisk(const Capture& cap)
    {
        auto dir = GetCaptureDirectory();

        // FIX: fully qualified chrono
        static std::atomic_uint32_t s_captureCounter{0};

        const auto timePoint = cap.timestamp;
        const auto ts        = std::chrono::system_clock::to_time_t(
            std::chrono::floor<std::chrono::seconds>(timePoint));
        const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            timePoint.time_since_epoch()) % 1000;
        const auto sequence = s_captureCounter.fetch_add(1, std::memory_order_relaxed);

        std::tm tm{};
        localtime_s(&tm, &ts);

        wchar_t name[256]{};
        swprintf(name, 256,
            L"cap_%04d%02d%02d_%02d%02d%02d_%03lld_%06u.bmp",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec,
            static_cast<long long>(millis.count()),
            sequence);

        std::filesystem::path path = dir / name;

        int w = cap.width;
        int h = cap.height;

        if (w <= 0 || h <= 0 || cap.pixels.size() != (size_t)(w * h))
            return std::nullopt;

        BITMAPFILEHEADER bfh{};
        BITMAPINFOHEADER bih{};

        bih.biSize = sizeof(BITMAPINFOHEADER);
        bih.biWidth = w;
        bih.biHeight = -h;
        bih.biPlanes = 1;
        bih.biBitCount = 32;
        bih.biCompression = BI_RGB;

        DWORD imageSize = DWORD(w * h * 4);

        bfh.bfType = 0x4D42;
        bfh.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        bfh.bfSize = bfh.bfOffBits + imageSize;

        std::ofstream ofs(path, std::ios::binary);
        if (!ofs)
            return std::nullopt;

        ofs.write((const char*)&bfh, sizeof(bfh));
        ofs.write((const char*)&bih, sizeof(bih));
        ofs.write((const char*)cap.pixels.data(), imageSize);

        if (!ofs)
            return std::nullopt;

        return path;
    }
}
