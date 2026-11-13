#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <objbase.h>
#include <gdiplus.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <memory>
#include <cwctype>
#include <algorithm>
#include <shlobj.h>

namespace Utils {

inline std::string WideToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return std::string();
    int size = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), nullptr, 0, nullptr, nullptr);
    std::string str(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &str[0], size, nullptr, nullptr);
    return str;
}

inline std::wstring Utf8ToWide(const std::string& str) {
    if (str.empty()) return std::wstring();
    int size = MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), nullptr, 0);
    std::wstring wstr(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.data(), (int)str.size(), &wstr[0], size);
    return wstr;
}

inline std::wstring CombinePath(const std::wstring& dir, const std::wstring& file) {
    if (dir.empty()) return file;
    if (dir.back() == L'\\' || dir.back() == L'/') return dir + file;
    return dir + L"\\" + file;
}

inline std::wstring GetFilenameFromPath(const std::wstring& path) {
    size_t pos = path.find_last_of(L"\\/");
    if (pos == std::wstring::npos) return path;
    return path.substr(pos + 1);
}

inline bool EnsureDirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) return true;
    return CreateDirectoryW(path.c_str(), nullptr) || GetLastError() == ERROR_ALREADY_EXISTS;
}

inline bool DirectoryExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY));
}

inline bool FileExists(const std::wstring& path) {
    DWORD attr = GetFileAttributesW(path.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

inline std::wstring GetExecutablePath() {
    wchar_t buf[MAX_PATH] = {};
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return (len > 0) ? std::wstring(buf, len) : L"";
}

inline std::wstring GetExecutableDirectory() {
    std::wstring p = GetExecutablePath();
    size_t pos = p.find_last_of(L"\\/");
    return (pos != std::wstring::npos) ? p.substr(0, pos) : L"";
}

inline std::wstring ToLower(const std::wstring& s) {
    std::wstring res = s;
    for (wchar_t& c : res) {
        c = static_cast<wchar_t>(std::towlower(c));
    }
    return res;
}

inline std::wstring GetDefaultAppDataDir() {
    PWSTR localAppData = nullptr;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &localAppData))) {
        std::wstring dir = CombinePath(localAppData, L"BorderWatch");
        CoTaskMemFree(localAppData);
        EnsureDirectoryExists(dir);
        return dir;
    }
    return L".";
}

inline std::vector<uint8_t> LoadResourceBytes(int resourceId) {
    HRSRC hRes = FindResourceW(nullptr, MAKEINTRESOURCEW(resourceId), MAKEINTRESOURCEW(10));
    if (!hRes) return {};
    HGLOBAL hData = LoadResource(nullptr, hRes);
    if (!hData) return {};
    DWORD size = SizeofResource(nullptr, hRes);
    if (size == 0) return {};
    const uint8_t* ptr = static_cast<const uint8_t*>(LockResource(hData));
    if (!ptr) return {};
    return std::vector<uint8_t>(ptr, ptr + size);
}

inline Gdiplus::Color HexToColor(const std::wstring& hex, BYTE alpha = 255) {
    std::wstring h = hex;
    if (!h.empty() && h[0] == L'#') h = h.substr(1);
    if (h.size() != 6) return Gdiplus::Color(alpha, 255, 255, 255);
    unsigned int val = 0;
    try {
        val = std::stoul(h, nullptr, 16);
    } catch (...) {
        return Gdiplus::Color(alpha, 255, 255, 255);
    }
    BYTE r = (val >> 16) & 0xFF;
    BYTE g = (val >> 8) & 0xFF;
    BYTE b = val & 0xFF;
    return Gdiplus::Color(alpha, r, g, b);
}

inline Gdiplus::Color LerpColor(const Gdiplus::Color& c1, const Gdiplus::Color& c2, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    BYTE a = static_cast<BYTE>(c1.GetA() + (c2.GetA() - c1.GetA()) * t);
    BYTE r = static_cast<BYTE>(c1.GetR() + (c2.GetR() - c1.GetR()) * t);
    BYTE g = static_cast<BYTE>(c1.GetG() + (c2.GetG() - c1.GetG()) * t);
    BYTE b = static_cast<BYTE>(c1.GetB() + (c2.GetB() - c1.GetB()) * t);
    return Gdiplus::Color(a, r, g, b);
}

inline float EaseOutBack(float t) {
    const float c1 = 1.70158f;
    const float c3 = c1 + 1.0f;
    float tm1 = t - 1.0f;
    return 1.0f + c3 * tm1 * tm1 * tm1 + c1 * tm1 * tm1;
}

inline void AddRoundedRect(Gdiplus::GraphicsPath& path, const Gdiplus::RectF& rect, float radius) {
    float diameter = radius * 2.0f;
    path.AddArc(rect.X, rect.Y, diameter, diameter, 180, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y, diameter, diameter, 270, 90);
    path.AddArc(rect.X + rect.Width - diameter, rect.Y + rect.Height - diameter, diameter, diameter, 0, 90);
    path.AddArc(rect.X, rect.Y + rect.Height - diameter, diameter, diameter, 90, 90);
    path.CloseFigure();
}

inline bool CopyTextToClipboard(HWND hWnd, const std::wstring& text) {
    if (text.empty()) return false;
    if (!OpenClipboard(hWnd)) return false;
    EmptyClipboard();

    size_t bytes = (text.length() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        memcpy(GlobalLock(hMem), text.c_str(), bytes);
        GlobalUnlock(hMem);
        SetClipboardData(CF_UNICODETEXT, hMem);
    }
    CloseClipboard();
    return true;
}

inline Gdiplus::Bitmap* DecodePngToBitmap(const std::vector<uint8_t>& bytes) {
    if (bytes.size() < 8) return nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hMem) return nullptr;
    void* pMem = GlobalLock(hMem);
    if (!pMem) {
        GlobalFree(hMem);
        return nullptr;
    }
    memcpy(pMem, bytes.data(), bytes.size());
    GlobalUnlock(hMem);

    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream))) {
        GlobalFree(hMem);
        return nullptr;
    }

    Gdiplus::Bitmap* srcBmp = Gdiplus::Bitmap::FromStream(pStream);
    if (!srcBmp || srcBmp->GetLastStatus() != Gdiplus::Ok || srcBmp->GetWidth() == 0) {
        delete srcBmp;
        pStream->Release();
        return nullptr;
    }

    // Clone into an independent 32bpp PARGB bitmap so we can release the stream immediately
    int w = (int)srcBmp->GetWidth();
    int h = (int)srcBmp->GetHeight();
    Gdiplus::Bitmap* cloned = new Gdiplus::Bitmap(w, h, PixelFormat32bppPARGB);
    {
        Gdiplus::Graphics g(cloned);
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.DrawImage(srcBmp, 0, 0, w, h);
    }
    delete srcBmp;
    pStream->Release();
    return cloned;
}

inline Gdiplus::Bitmap* GetCachedAvatarBitmap(const std::wstring& key, const std::vector<uint8_t>& pngBytes) {
    if (key.empty() || pngBytes.empty()) return nullptr;
    static std::mutex s_cacheMutex;
    static std::unordered_map<std::wstring, std::unique_ptr<Gdiplus::Bitmap>> s_bitmapCache;

    std::lock_guard<std::mutex> lock(s_cacheMutex);
    auto it = s_bitmapCache.find(key);
    if (it != s_bitmapCache.end()) {
        return it->second.get();
    }
    Gdiplus::Bitmap* bmp = DecodePngToBitmap(pngBytes);
    if (bmp) {
        s_bitmapCache[key] = std::unique_ptr<Gdiplus::Bitmap>(bmp);
    }
    return bmp;
}

inline void DrawAvatarCircle(Gdiplus::Graphics& g,
                                const std::vector<uint8_t>& pngBytes,
                                const std::wstring& username,
                                const std::wstring& status,
                                float x, float y, float diameter,
                                bool isDark,
                                bool isSummary = false,
                                bool isUpdate = false) {
    bool isEnemy = (status == L"Enemy");
    bool isUpd = isUpdate || (status == L"Update");
    Gdiplus::Color ringColor = isUpd
        ? Gdiplus::Color(255, 99, 102, 241)
        : (isEnemy ? Gdiplus::Color(255, 229, 72, 77) : Gdiplus::Color(255, 48, 164, 108));
    Gdiplus::Color fillBg = isUpd
        ? (isDark ? Gdiplus::Color(255, 36, 42, 68) : Gdiplus::Color(255, 238, 242, 255))
        : (isEnemy
            ? (isDark ? Gdiplus::Color(255, 58, 28, 36) : Gdiplus::Color(255, 254, 226, 229))
            : (isDark ? Gdiplus::Color(255, 24, 52, 38) : Gdiplus::Color(255, 224, 248, 234)));

    Gdiplus::RectF circleRect(x, y, diameter, diameter);
    Gdiplus::SolidBrush bgBrush(fillBg);
    g.FillEllipse(&bgBrush, circleRect);

    if (isUpd) {
        Gdiplus::FontFamily ff(L"Segoe UI");
        float fontSize = std::max(9.0f, diameter * 0.44f);
        Gdiplus::Font font(&ff, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
        Gdiplus::SolidBrush txtBrush(ringColor);
        Gdiplus::StringFormat sf;
        sf.SetAlignment(Gdiplus::StringAlignmentCenter);
        sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        const wchar_t* arrowSym = L"\u2193"; // ↓
        g.DrawString(arrowSym, -1, &font, circleRect, &sf, &txtBrush);
    } else {
        std::wstring cacheKey = username;
        for (wchar_t& c : cacheKey) c = (wchar_t)towlower(c);

        Gdiplus::Bitmap* bmp = (!isSummary && !pngBytes.empty()) ? GetCachedAvatarBitmap(cacheKey, pngBytes) : nullptr;
        if (bmp) {
            Gdiplus::GraphicsPath clipPath;
            clipPath.AddEllipse(circleRect);
            Gdiplus::Region oldClip;
            g.GetClip(&oldClip);
            g.SetClip(&clipPath, Gdiplus::CombineModeIntersect);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            g.DrawImage(bmp, circleRect);
            g.SetClip(&oldClip, Gdiplus::CombineModeReplace);
        } else {
            std::wstring initials = L"?";
            if (isSummary) {
                initials = L"!";
            } else if (!username.empty()) {
                initials.clear();
                initials.push_back((wchar_t)towupper(username[0]));
                if (username.size() > 1 && diameter >= 30.0f) {
                    initials.push_back((wchar_t)towupper(username[1]));
                }
            }
            Gdiplus::FontFamily ff(L"Segoe UI");
            float fontSize = std::max(7.5f, diameter * 0.34f);
            Gdiplus::Font font(&ff, fontSize, Gdiplus::FontStyleBold, Gdiplus::UnitPoint);
            Gdiplus::SolidBrush txtBrush(ringColor);
            Gdiplus::StringFormat sf;
            sf.SetAlignment(Gdiplus::StringAlignmentCenter);
            sf.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            g.DrawString(initials.c_str(), -1, &font, circleRect, &sf, &txtBrush);
        }
    }

    Gdiplus::Pen ringPen(ringColor, diameter >= 32.0f ? 1.8f : 1.2f);
    g.DrawEllipse(&ringPen, circleRect);
}

} // namespace utils
