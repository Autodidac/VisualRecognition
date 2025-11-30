module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"

#include <filesystem>
#include <fstream>
#include <vector>
#include <system_error>
#include <chrono>

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

    export bool SaveModel(const PixelRecognizer& model)
    {
        auto path = GetModelFilePath();
        return model.save_to_file(path.string());
    }

    export std::optional<std::filesystem::path>
        SaveCaptureToDisk(const Capture& cap)
    {
        auto dir = GetCaptureDirectory();

        // FIX: fully qualified chrono
        auto ts = std::chrono::system_clock::to_time_t(cap.timestamp);

        std::tm tm{};
        localtime_s(&tm, &ts);

        wchar_t name[256]{};
        swprintf(name, 256,
            L"cap_%04d%02d%02d_%02d%02d%02d.bmp",
            tm.tm_year + 1900,
            tm.tm_mon + 1,
            tm.tm_mday,
            tm.tm_hour,
            tm.tm_min,
            tm.tm_sec);

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
