#pragma once

#include <cstdlib>
#include <string>

#include <windows.h>

// 极简日志：插件和浮窗共用一份（%TEMP%\nxgeo.log）。
namespace NxGeoLog {

inline void Write(const std::string& text) {
    const char* temp = std::getenv("TEMP");
    std::string path = (temp != nullptr ? temp : ".");
    path += "\\nxgeo.log";

    FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "a") == 0 && file != nullptr) {
        SYSTEMTIME now{};
        GetLocalTime(&now);
        fprintf(file, "%04d-%02d-%02d %02d:%02d:%02d  %s\n",
                now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond, text.c_str());
        fclose(file);
    }
}

}  // namespace NxGeoLog
