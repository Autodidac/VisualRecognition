module;
#define NOMINMAX
#include <windows.h>
#include "ids.hpp"
#include <filesystem>
#include <fstream>
#include <chrono>
#include <vector>
#include <optional>
#include <cstdint>
#include <algorithm>

export module ui:filesystem;

import std;
import :common;

namespace ui::detail
{
    // Settings + model path helpers
    std::filesystem::path GetHistoryDir()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD len = ::GetTempPathW(static_cast<DWORD>(std::size(buffer)), buffer);
        if (len == 0 || len > std::size(buffer))
        {
            return std::filesystem::temp_directory_path() / "pixelai_captures";
        }

        std::filesystem::path dir(buffer);
        dir /= "pixelai_captures";
        return dir;
    }

    std::filesystem::path GetModelPath()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD len = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (len > 0 && len < std::size(buffer))
        {
            std::filesystem::path exePath(buffer);
            return exePath.parent_path() / kModelFile;
        }

        std::error_code ec{};
        auto cwd = std::filesystem::current_path(ec);
        if (!ec)
            return cwd / kModelFile;

        return std::filesystem::path(kModelFile);
    }

    std::filesystem::path GetSettingsPath()
    {
        wchar_t buffer[MAX_PATH]{};
        DWORD len = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
        if (len == 0 || len >= std::size(buffer))
            return std::filesystem::path(L"pixelai.ini");

        std::filesystem::path exePath(buffer);
        return exePath.parent_path() / L"pixelai.ini";
    }

    bool EnsureSettingsFile()
    {
        auto ini = GetSettingsPath();
        if (std::filesystem::exists(ini))
            return true;

        std::error_code ec{};
        auto parent = ini.parent_path();
        if (!parent.empty())
            std::filesystem::create_directories(parent, ec);

        std::wofstream ofs(ini);
        if (!ofs)
            return false;

        ofs << L"[Saving]\n";
        ofs << L"BackupRetention=" << kDefaultBackupRetention << L"\n";
        return ofs.good();
    }

    int GetBackupRetention()
    {
        auto ini = GetSettingsPath();
        if (std::filesystem::exists(ini))
        {
            int value = static_cast<int>(::GetPrivateProfileIntW(
                L"Saving",
                L"BackupRetention",
                kDefaultBackupRetention,
                ini.c_str()));
            return std::max(0, value);
        }

        return kDefaultBackupRetention;
    }

    bool CreatePlaceholderModelFile(const std::filesystem::path& modelPath)
    {
        std::error_code ec{};
        std::filesystem::create_directories(modelPath.parent_path(), ec);

        std::ofstream ofs(modelPath, std::ios::binary | std::ios::trunc);
        if (!ofs)
            return false;

        const char magic[5] = { 'P','X','A','I','1' };
        ofs.write(magic, sizeof(magic));

        std::uint32_t count = 0;
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));

        std::int32_t width = kDefaultPatchSize;
        std::int32_t height = kDefaultPatchSize;
        ofs.write(reinterpret_cast<const char*>(&width), sizeof(width));
        ofs.write(reinterpret_cast<const char*>(&height), sizeof(height));

        return ofs.good();
    }

    std::optional<std::filesystem::path> CreateModelBackup(const std::filesystem::path& modelPath)
    {
        if (!std::filesystem::exists(modelPath))
            return std::nullopt;

        std::error_code ec{};
        std::filesystem::create_directories(modelPath.parent_path(), ec);

        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        tm localTm{};
        localtime_s(&localTm, &t);

        wchar_t ts[32]{};
        std::wcsftime(ts, std::size(ts), L"%Y%m%d_%H%M%S", &localTm);

        auto backupName = modelPath.stem().wstring() + L"_" + ts + modelPath.extension().wstring();
        auto backupPath = modelPath.parent_path() / backupName;

        std::filesystem::copy_file(modelPath, backupPath, std::filesystem::copy_options::overwrite_existing, ec);
        if (ec)
            return std::nullopt;

        return backupPath;
    }

    void EnforceBackupRetention(const std::filesystem::path& modelPath)
    {
        int retention = GetBackupRetention();
        if (retention <= 0)
            return;

        std::vector<std::filesystem::directory_entry> backups;
        std::error_code ec{};
        for (auto& entry : std::filesystem::directory_iterator(modelPath.parent_path(), ec))
        {
            if (!entry.is_regular_file())
                continue;

            auto name = entry.path().filename().wstring();
            if (name.starts_with(modelPath.stem().wstring() + L"_") && entry.path().extension() == modelPath.extension())
                backups.push_back(entry);
        }

        std::sort(backups.begin(), backups.end(), [](const auto& a, const auto& b)
            {
                std::error_code aec{};
                std::error_code bec{};
                return a.last_write_time(aec) < b.last_write_time(bec);
            });

        if (backups.size() <= static_cast<std::size_t>(retention))
            return;

        auto toRemove = backups.size() - static_cast<std::size_t>(retention);
        for (std::size_t i = 0; i < toRemove; ++i)
        {
            std::filesystem::remove(backups[i]);
        }
    }

    // Capture persistence helpers
    std::optional<std::filesystem::path> SaveCaptureToDisk(const Capture& cap)
    {
        auto dir = GetHistoryDir();
        std::error_code ec{};
        std::filesystem::create_directories(dir, ec);

        auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(cap.timestamp.time_since_epoch()).count();
        auto file = dir / (std::to_wstring(ticks) + L".bin");

        int dedupe = 1;
        while (std::filesystem::exists(file))
        {
            file = dir / (std::to_wstring(ticks) + L"_" + std::to_wstring(dedupe) + L".bin");
            ++dedupe;
        }

        std::ofstream ofs(file, std::ios::binary | std::ios::trunc);
        if (!ofs)
            return std::nullopt;

        std::int32_t w = cap.width;
        std::int32_t h = cap.height;
        std::int64_t t = ticks;
        std::uint64_t count = cap.pixels.size();

        ofs.write(reinterpret_cast<const char*>(&w), sizeof(w));
        ofs.write(reinterpret_cast<const char*>(&h), sizeof(h));
        ofs.write(reinterpret_cast<const char*>(&t), sizeof(t));
        ofs.write(reinterpret_cast<const char*>(&count), sizeof(count));
        ofs.write(reinterpret_cast<const char*>(cap.pixels.data()), static_cast<std::streamsize>(count * sizeof(std::uint32_t)));

        if (!ofs.good())
            return std::nullopt;

        return file;
    }

    std::optional<Capture> LoadCaptureFromDisk(const std::filesystem::path& path)
    {
        std::ifstream ifs(path, std::ios::binary);
        if (!ifs)
            return std::nullopt;

        std::int32_t w{};
        std::int32_t h{};
        std::int64_t t{};
        std::uint64_t count{};

        ifs.read(reinterpret_cast<char*>(&w), sizeof(w));
        ifs.read(reinterpret_cast<char*>(&h), sizeof(h));
        ifs.read(reinterpret_cast<char*>(&t), sizeof(t));
        ifs.read(reinterpret_cast<char*>(&count), sizeof(count));

        if (w <= 0 || h <= 0 || count == 0)
            return std::nullopt;

        std::uint64_t expected = static_cast<std::uint64_t>(w) * static_cast<std::uint64_t>(h);
        if (count != expected)
            return std::nullopt;

        std::vector<std::uint32_t> pixels;
        pixels.resize(count);
        ifs.read(reinterpret_cast<char*>(pixels.data()), static_cast<std::streamsize>(count * sizeof(std::uint32_t)));
        if (!ifs)
            return std::nullopt;

        Capture cap{};
        cap.width = w;
        cap.height = h;
        cap.timestamp = std::chrono::system_clock::time_point{ std::chrono::milliseconds{ t } };
        cap.pixels = std::move(pixels);
        cap.filePath = path;
        return cap;
    }

    void LoadCaptureHistory()
    {
        g_history.clear();
        g_selectedIndex = -1;

        auto dir = GetHistoryDir();
        if (!std::filesystem::exists(dir))
            return;

        std::vector<Capture> loaded;
        for (auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file())
                continue;

            auto cap = LoadCaptureFromDisk(entry.path());
            if (cap)
                loaded.push_back(std::move(*cap));
        }

        std::sort(loaded.begin(), loaded.end(), [](const Capture& a, const Capture& b)
            {
                return a.timestamp < b.timestamp;
            });

        g_history = std::move(loaded);
        if (!g_history.empty())
            g_selectedIndex = static_cast<int>(g_history.size() - 1);
    }
}
