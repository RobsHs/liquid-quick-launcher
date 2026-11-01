// ==WindhawkMod==
// @id              alt-space-liquid-launcher
// @name            Liquid Quick Launcher
// @description     A Spotlight-style productivity launcher triggered by Alt+Space. Features instant calculator, app search, system commands, and web search with a liquid glass aesthetic.
// @version         1.0.0
// @author          RobsHs
// @github          https://github.com/RobsHs
// @include         explorer.exe
// @compilerOptions -lgdi32 -ldwmapi -luxtheme -lcomctl32 -lshell32 -lole32
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Liquid Quick Launcher

A Spotlight / PowerToys Run-style productivity launcher with a Liquid Glass aesthetic.

## Activation
Press **Alt + Space** from anywhere to open the launcher.

## Features
- **App Search**: Finds Start Menu shortcuts and system apps automatically
- **Instant Calculator**: Type any math expression (e.g. `2+2`, `sqrt(16)`, `(10^2)/4`)
  - Press **Enter** to copy the result to clipboard
- **Command Runner**: Prefix with `>` to run terminal commands (e.g. `> ipconfig`)
- **System Commands**: Type `lock`, `sleep`, `restart`, `shutdown`, `empty bin`, `taskmgr`
- **Web / URL**: Automatically detects URLs or falls back to Google Search

## Navigation
- **Arrow Up / Down**: Navigate results
- **Enter**: Execute selected item
- **Escape** or click outside: Close launcher
*/
// ==/WindhawkModReadme==

// NOTE: UNICODE and _UNICODE are pre-defined by the Windhawk compiler.
// Redefining them triggers -Wmacro-redefined. Omit them here.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX

#include <windows.h>
#include <dwmapi.h>
#include <uxtheme.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shlwapi.h>
#include <ole2.h>
#include <windhawk_api.h>

#include <string>
#include <vector>
#include <algorithm>
#include <thread>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <functional>
#include <cctype>
#include <locale>

// ─────────────────────────────────────────────────────────────────────────────
// CONSTANTS & CONFIGURATION
// ─────────────────────────────────────────────────────────────────────────────

static const int    LAUNCHER_WIDTH      = 680;
static const int    LAUNCHER_BASE_H     = 62;   // collapsed (input only)
static const int    RESULT_ITEM_H       = 46;
static const int    RESULT_MAX_ITEMS    = 5;
static const int    EDIT_PADDING_X      = 56;
static const int    EDIT_HEIGHT         = 44;
static const int    EDIT_TOP_MARGIN     = 9;
static const int    HOTKEY_ID           = 0xB0;   // Valid hotkey ID range
static const UINT   WM_SHOW_LAUNCHER    = WM_USER + 1;
static const UINT   WM_HIDE_LAUNCHER    = WM_USER + 2;

// Colors (COLORREF = 0x00BBGGRR)
static const COLORREF COL_BG           = RGB(18,  18,  24);
static const COLORREF COL_BG_ITEM      = RGB(26,  26,  36);
static const COLORREF COL_SEL_BG       = RGB(30,  34,  52);
static const COLORREF COL_ACCENT       = RGB(82,  152, 255); // Electric Blue
static const COLORREF COL_TEXT_PRIMARY  = RGB(230, 232, 240);
static const COLORREF COL_TEXT_SEC     = RGB(130, 135, 160);
static const COLORREF COL_BADGE_APP    = RGB(40,  90,  60);
static const COLORREF COL_BADGE_CALC   = RGB(50,  60,  100);
static const COLORREF COL_BADGE_CMD    = RGB(80,  50,  40);
static const COLORREF COL_BADGE_SYS    = RGB(70,  40,  80);
static const COLORREF COL_BADGE_WEB    = RGB(40,  70,  90);
static const COLORREF COL_BADGE_TEXT   = RGB(200, 220, 255);
static const COLORREF COL_DIVIDER      = RGB(38,  40,  55);

static const WCHAR FONT_NAME[]         = L"Segoe UI";
static const int   FONT_SIZE_INPUT     = 18;
static const int   FONT_SIZE_RESULT    = 14;
static const int   FONT_SIZE_BADGE     = 11;
static const int   FONT_SIZE_ICON      = 20;

// ─────────────────────────────────────────────────────────────────────────────
// DATA STRUCTURES
// ─────────────────────────────────────────────────────────────────────────────

enum class ResultCategory {
    App,
    Calculator,
    Command,
    System,
    Web
};

struct LaunchResult {
    std::wstring    label;
    std::wstring    sublabel;
    std::wstring    iconGlyph;  // BMP Unicode glyph
    std::wstring    execPath;   // For apps / commands
    std::wstring    calcValue;  // For calculator
    ResultCategory  category;
};

struct AppEntry {
    std::wstring name;
    std::wstring path;
};

// ─────────────────────────────────────────────────────────────────────────────
// GLOBALS
// ─────────────────────────────────────────────────────────────────────────────

static HWND             g_hwndLauncher  = nullptr;
static HWND             g_hwndEdit      = nullptr;
static HINSTANCE        g_hInst         = nullptr;
static ATOM             g_wndClass      = 0;

static HFONT            g_fontInput     = nullptr;
static HFONT            g_fontResult    = nullptr;
static HFONT            g_fontBadge     = nullptr;
static HFONT            g_fontIcon      = nullptr;

static std::vector<LaunchResult>    g_results;
static int                          g_selectedIdx   = -1;

// App index (background thread)
static CRITICAL_SECTION             g_appIndexCS;
static std::vector<AppEntry>        g_appIndex;
static std::atomic<bool>            g_indexReady    = false;
static std::atomic<bool>            g_indexing      = false;

// Hotkey thread
static std::thread                  g_hotkeyThread;
static std::atomic<bool>            g_hotkeyStop    = false;
static HWND                         g_hwndHotkeyMsg = nullptr;

// Subclass original Edit proc
static WNDPROC                      g_origEditProc  = nullptr;

// ─────────────────────────────────────────────────────────────────────────────
// FORWARD DECLARATIONS
// ─────────────────────────────────────────────────────────────────────────────

static void     StartIndexing();
static void     UpdateResults(const std::wstring& query);
static void     ExecuteResult(const LaunchResult& res);
static void     ShowLauncher();
static void     HideLauncher();
static void     UpdateWindowHeight();
static LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
static void     PaintLauncher(HWND hwnd);
static bool     CreateLauncherWindow();
static void     HotkeyThreadFunc();
static void     DestroyFonts();
static void     CreateFonts();

// ─────────────────────────────────────────────────────────────────────────────
// UTILITY: String helpers
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring ToLower(const std::wstring& s) {
    std::wstring out = s;
    for (wchar_t& c : out) {
        if (c >= L'A' && c <= L'Z') c = c + (L'a' - L'A');
    }
    return out;
}

static bool ContainsI(const std::wstring& haystack, const std::wstring& needle) {
    if (needle.empty()) return true;
    std::wstring h = ToLower(haystack);
    std::wstring n = ToLower(needle);
    return h.find(n) != std::wstring::npos;
}

static bool StartsWithI(const std::wstring& s, const std::wstring& prefix) {
    if (s.size() < prefix.size()) return false;
    return ToLower(s.substr(0, prefix.size())) == ToLower(prefix);
}

// Trim leading whitespace
static std::wstring LTrim(const std::wstring& s) {
    size_t start = 0;
    while (start < s.size() && iswspace(s[start])) start++;
    return s.substr(start);
}

// ─────────────────────────────────────────────────────────────────────────────
// CALCULATOR: Recursive-descent parser
// Supports: +, -, *, /, %, ^, unary minus, parentheses, sqrt(x)
// ─────────────────────────────────────────────────────────────────────────────

struct CalcParser {
    const std::wstring& expr;
    size_t pos;
    bool   error;

    CalcParser(const std::wstring& e) : expr(e), pos(0), error(false) {}

    void skipSpace() {
        while (pos < expr.size() && iswspace(expr[pos])) pos++;
    }

    double parseNumber() {
        skipSpace();
        // Handle sqrt
        if (pos + 4 <= expr.size()) {
            std::wstring chunk = ToLower(expr.substr(pos, 4));
            if (chunk == L"sqrt") {
                pos += 4;
                skipSpace();
                if (pos < expr.size() && expr[pos] == L'(') {
                    pos++;
                    double v = parseExpr();
                    skipSpace();
                    if (pos < expr.size() && expr[pos] == L')') pos++;
                    else error = true;
                    if (v < 0) { error = true; return 0; }
                    return std::sqrt(v);
                }
                error = true;
                return 0;
            }
        }
        // Parenthesis
        if (pos < expr.size() && expr[pos] == L'(') {
            pos++;
            double v = parseExpr();
            skipSpace();
            if (pos < expr.size() && expr[pos] == L')') pos++;
            else error = true;
            return v;
        }
        // Unary minus
        if (pos < expr.size() && expr[pos] == L'-') {
            pos++;
            return -parseNumber();
        }
        // Unary plus
        if (pos < expr.size() && expr[pos] == L'+') {
            pos++;
            return parseNumber();
        }
        // Numeric literal
        skipSpace();
        size_t start = pos;
        bool hasDot = false;
        while (pos < expr.size()) {
            wchar_t c = expr[pos];
            if (c >= L'0' && c <= L'9') { pos++; }
            else if (c == L'.' && !hasDot) { hasDot = true; pos++; }
            else break;
        }
        if (pos == start) { error = true; return 0; }
        std::wstring numStr = expr.substr(start, pos - start);
        return std::stod(numStr);
    }

    double parsePow() {
        double base = parseNumber();
        skipSpace();
        if (pos < expr.size() && expr[pos] == L'^') {
            pos++;
            double exp = parsePow(); // right-associative
            return std::pow(base, exp);
        }
        return base;
    }

    double parseMulDiv() {
        double left = parsePow();
        while (true) {
            skipSpace();
            if (pos >= expr.size()) break;
            wchar_t op = expr[pos];
            if (op != L'*' && op != L'/' && op != L'%') break;
            pos++;
            double right = parsePow();
            if (op == L'*') left *= right;
            else if (op == L'/') {
                if (right == 0) { error = true; return 0; }
                left /= right;
            }
            else {
                if (right == 0) { error = true; return 0; }
                left = std::fmod(left, right);
            }
        }
        return left;
    }

    double parseExpr() {
        double left = parseMulDiv();
        while (true) {
            skipSpace();
            if (pos >= expr.size()) break;
            wchar_t op = expr[pos];
            if (op != L'+' && op != L'-') break;
            pos++;
            double right = parseMulDiv();
            if (op == L'+') left += right;
            else left -= right;
        }
        return left;
    }

    double parse() {
        double result = parseExpr();
        skipSpace();
        if (pos != expr.size()) error = true;
        return result;
    }
};

// Returns false if not a valid math expression
static bool TryCalc(const std::wstring& query, double& outVal) {
    // Must contain at least one digit
    bool hasDigit = false;
    for (wchar_t c : query) {
        if (c >= L'0' && c <= L'9') { hasDigit = true; break; }
    }
    if (!hasDigit) return false;

    // Check for math chars only (digits, operators, spaces, parens, dot, letters for sqrt)
    for (wchar_t c : query) {
        bool ok = (c >= L'0' && c <= L'9')
               || c == L'+' || c == L'-' || c == L'*' || c == L'/'
               || c == L'%' || c == L'^' || c == L'(' || c == L')'
               || c == L'.' || c == L' '
               || (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z');
        if (!ok) return false;
    }

    CalcParser p(query);
    outVal = p.parse();
    return !p.error;
}

// Format double cleanly
static std::wstring FormatDouble(double v) {
    // If it's an integer value (no fractional part), display as integer
    if (v == std::floor(v) && std::abs(v) < 1e15) {
        long long i = static_cast<long long>(v);
        return std::to_wstring(i);
    }
    // Up to 8 significant digits
    wchar_t buf[64] = {};
    swprintf_s(buf, 64, L"%.8g", v);
    return buf;
}

// ─────────────────────────────────────────────────────────────────────────────
// URL DETECTION
// ─────────────────────────────────────────────────────────────────────────────

static bool IsUrlLike(const std::wstring& q) {
    // Must contain a dot and no spaces
    if (q.find(L' ') != std::wstring::npos) return false;
    if (q.find(L'.') == std::wstring::npos) return false;
    if (StartsWithI(q, L"http://")) return true;
    if (StartsWithI(q, L"https://")) return true;
    if (StartsWithI(q, L"www.")) return true;
    // domain.tld pattern: must have alphabetic char after dot
    size_t dot = q.rfind(L'.');
    if (dot != std::wstring::npos && dot + 1 < q.size()) {
        std::wstring tld = ToLower(q.substr(dot + 1));
        // Common TLDs
        static const WCHAR* tlds[] = {
            L"com", L"net", L"org", L"io", L"co", L"id", L"uk", L"edu",
            L"gov", L"dev", L"app", L"ai", L"info", L"biz", nullptr
        };
        for (int i = 0; tlds[i]; i++) {
            if (tld == tlds[i]) return true;
        }
    }
    return false;
}

static std::wstring BuildUrl(const std::wstring& q) {
    if (StartsWithI(q, L"http://") || StartsWithI(q, L"https://")) return q;
    if (StartsWithI(q, L"www.")) return L"https://" + q;
    if (IsUrlLike(q)) return L"https://" + q;
    // Google search
    return L"https://www.google.com/search?q=" + q;
}

// ─────────────────────────────────────────────────────────────────────────────
// SYSTEM COMMAND DETECTION
// ─────────────────────────────────────────────────────────────────────────────

struct SysCmd {
    const WCHAR* keyword;
    const WCHAR* label;
    const WCHAR* sublabel;
    const WCHAR* glyph;
};

// All glyphs use BMP-safe Unicode codepoints (U+0000..U+FFFF only)
// to avoid "hex escape sequence out of range" on Windhawk's 16-bit wchar_t.
static const SysCmd g_sysCmdsSafe[] = {
    { L"lock",      L"Lock Workstation",    L"Lock screen immediately",         L"\x25A3"  },
    { L"sleep",     L"Sleep",               L"Put computer to sleep",           L"\x23F8"  },
    { L"restart",   L"Restart",             L"Restart Windows",                 L"\x21BB"  },
    { L"shutdown",  L"Shut Down",           L"Shut down Windows",               L"\x23FB"  },
    { L"empty bin", L"Empty Recycle Bin",   L"Permanently delete all items",    L"\x267B"  },
    { L"taskmgr",   L"Task Manager",        L"Open Task Manager",               L"\x2699"  },
    { nullptr, nullptr, nullptr, nullptr }
};

// ─────────────────────────────────────────────────────────────────────────────
// BUILT-IN SYSTEM APPS
// ─────────────────────────────────────────────────────────────────────────────

struct BuiltInApp {
    const WCHAR* name;
    const WCHAR* exec;
    const WCHAR* glyph;
};

// BMP-safe built-in apps; all glyphs within U+0000..U+FFFF.
static const BuiltInApp g_builtinAppsSafe[] = {
    { L"Notepad",         L"notepad.exe",          L"\x2710"  }, // pencil
    { L"Calculator",      L"calc.exe",             L"\x229E"  }, // squared plus
    { L"Task Manager",    L"taskmgr.exe",          L"\x2699"  }, // gear
    { L"Command Prompt",  L"cmd.exe",              L">_"       },
    { L"PowerShell",      L"powershell.exe",       L">_"       },
    { L"Paint",           L"mspaint.exe",          L"\x25A2"  }, // white square with rounded corners
    { L"File Explorer",   L"explorer.exe",         L"\x2750"  }, // dingbat heavy wide-headed rightwards arrow (folder-like)
    { L"Registry Editor", L"regedit.exe",          L"\x2699"  },
    { L"Control Panel",   L"control.exe",          L"\x2756"  }, // black diamond minus white X
    { nullptr, nullptr, nullptr }
};

// ─────────────────────────────────────────────────────────────────────────────
// APP INDEXER (Background Thread)
// ─────────────────────────────────────────────────────────────────────────────

static void ScanLnkFolder(const std::wstring& folderPath, std::vector<AppEntry>& out) {
    std::wstring pattern = folderPath + L"\\*.lnk";
    WIN32_FIND_DATAW fd = {};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;

    do {
        std::wstring filename = fd.cFileName;
        // Remove .lnk extension
        std::wstring name = filename;
        if (name.size() > 4) {
            std::wstring ext = ToLower(name.substr(name.size() - 4));
            if (ext == L".lnk") name = name.substr(0, name.size() - 4);
        }
        AppEntry ae;
        ae.name = name;
        ae.path = folderPath + L"\\" + filename;
        out.push_back(ae);
    } while (FindNextFileW(hFind, &fd));

    FindClose(hFind);

    // Recurse into subdirectories
    std::wstring subPattern = folderPath + L"\\*";
    HANDLE hSub = FindFirstFileW(subPattern.c_str(), &fd);
    if (hSub == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) continue;
        if (fd.cFileName[0] == L'.') continue;
        ScanLnkFolder(folderPath + L"\\" + fd.cFileName, out);
    } while (FindNextFileW(hSub, &fd));
    FindClose(hSub);
}

static void IndexerThreadFunc() {
    std::vector<AppEntry> newIndex;

    // Add built-in apps
    for (int i = 0; g_builtinAppsSafe[i].name != nullptr; i++) {
        AppEntry ae;
        ae.name = g_builtinAppsSafe[i].name;
        ae.path = g_builtinAppsSafe[i].exec;
        newIndex.push_back(ae);
    }

    // Scan user Start Menu
    WCHAR userStartMenu[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_STARTMENU, nullptr, SHGFP_TYPE_CURRENT, userStartMenu))) {
        ScanLnkFolder(userStartMenu, newIndex);
    }

    // Scan all-users Start Menu
    WCHAR allStartMenu[MAX_PATH] = {};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_COMMON_STARTMENU, nullptr, SHGFP_TYPE_CURRENT, allStartMenu))) {
        ScanLnkFolder(allStartMenu, newIndex);
    }

    // Deduplicate by lowercased name
    std::vector<AppEntry> deduped;
    for (auto& ae : newIndex) {
        std::wstring lower = ToLower(ae.name);
        bool found = false;
        for (auto& ex : deduped) {
            if (ToLower(ex.name) == lower) { found = true; break; }
        }
        if (!found) deduped.push_back(ae);
    }

    EnterCriticalSection(&g_appIndexCS);
    g_appIndex = std::move(deduped);
    LeaveCriticalSection(&g_appIndexCS);

    g_indexReady.store(true);
    g_indexing.store(false);
}

static void StartIndexing() {
    if (g_indexing.load()) return;
    g_indexing.store(true);
    std::thread t(IndexerThreadFunc);
    t.detach();
}

// ─────────────────────────────────────────────────────────────────────────────
// RESULT ENGINE
// ─────────────────────────────────────────────────────────────────────────────

static void UpdateResults(const std::wstring& rawQuery) {
    g_results.clear();
    g_selectedIdx = -1;

    std::wstring query = LTrim(rawQuery);
    if (query.empty()) {
        UpdateWindowHeight();
        return;
    }

    std::wstring queryL = ToLower(query);

    // ── 1. Command prefix: '>'
    if (!query.empty() && query[0] == L'>') {
        std::wstring cmd = LTrim(query.substr(1));
        if (!cmd.empty()) {
            LaunchResult r;
            r.label     = L"Run: " + cmd;
            r.sublabel  = L"Execute in terminal";
            r.iconGlyph = L">_";
            r.execPath  = cmd;
            r.category  = ResultCategory::Command;
            g_results.push_back(r);
        }
        UpdateWindowHeight();
        return;
    }

    // ── 2. System commands
    for (int i = 0; g_sysCmdsSafe[i].keyword != nullptr; i++) {
        std::wstring kw = ToLower(g_sysCmdsSafe[i].keyword);
        // Match if query is a prefix of the keyword, or keyword contains the query
        if (StartsWithI(kw, queryL) || ContainsI(kw, queryL)) {
            LaunchResult r;
            r.label     = g_sysCmdsSafe[i].label;
            r.sublabel  = g_sysCmdsSafe[i].sublabel;
            r.iconGlyph = g_sysCmdsSafe[i].glyph;
            r.execPath  = g_sysCmdsSafe[i].keyword;
            r.category  = ResultCategory::System;
            g_results.push_back(r);
        }
    }

    // ── 3. Calculator
    double calcVal = 0;
    if (TryCalc(query, calcVal)) {
        std::wstring valStr = FormatDouble(calcVal);
        LaunchResult r;
        r.label     = valStr;
        r.sublabel  = query + L"  =  " + valStr;
        r.iconGlyph = L"\x229E";  // squared plus
        r.calcValue = valStr;
        r.category  = ResultCategory::Calculator;
        // Prepend calculator result
        g_results.insert(g_results.begin(), r);
    }

    // ── 4. App search
    if (g_indexReady.load()) {
        EnterCriticalSection(&g_appIndexCS);
        std::vector<AppEntry> localIndex = g_appIndex;
        LeaveCriticalSection(&g_appIndexCS);

        int appCount = 0;
        for (auto& ae : localIndex) {
            if (appCount >= RESULT_MAX_ITEMS) break;
            if (!ContainsI(ae.name, query)) continue;
            // Skip duplicates already in results
            bool dup = false;
            for (auto& ex : g_results) {
                if (ex.category == ResultCategory::App && ToLower(ex.label) == ToLower(ae.name)) {
                    dup = true; break;
                }
            }
            if (dup) continue;

            LaunchResult r;
            r.label     = ae.name;
            r.sublabel  = ae.path;
            r.iconGlyph = L"\x25A0";  // black square (generic app icon placeholder)
            r.execPath  = ae.path;
            r.category  = ResultCategory::App;
            g_results.push_back(r);
            appCount++;
        }
    }

    // ── 5. URL / Web search (always last)
    if (g_results.size() < (size_t)RESULT_MAX_ITEMS) {
        LaunchResult r;
        if (IsUrlLike(query)) {
            r.label     = query;
            r.sublabel  = L"Open URL in browser";
            r.iconGlyph = L"\x2318";  // place of interest (command-like)
            r.execPath  = BuildUrl(query);
            r.category  = ResultCategory::Web;
        } else {
            r.label     = L"Search: " + query;
            r.sublabel  = L"Search on Google";
            r.iconGlyph = L"\x2318";
            r.execPath  = BuildUrl(query);
            r.category  = ResultCategory::Web;
        }
        g_results.push_back(r);
    }

    // Cap to RESULT_MAX_ITEMS
    if (g_results.size() > (size_t)RESULT_MAX_ITEMS) {
        g_results.resize(RESULT_MAX_ITEMS);
    }

    // Auto-select first item if results present
    if (!g_results.empty()) g_selectedIdx = 0;

    UpdateWindowHeight();
}

// ─────────────────────────────────────────────────────────────────────────────
// EXECUTION
// ─────────────────────────────────────────────────────────────────────────────

static void CopyToClipboard(const std::wstring& text) {
    if (!OpenClipboard(g_hwndLauncher)) return;
    EmptyClipboard();
    size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (hMem) {
        void* ptr = GlobalLock(hMem);
        if (ptr) {
            memcpy(ptr, text.c_str(), bytes);
            GlobalUnlock(hMem);
            SetClipboardData(CF_UNICODETEXT, hMem);
        }
    }
    CloseClipboard();
}

static void ExecuteResult(const LaunchResult& res) {
    HideLauncher();

    switch (res.category) {
    case ResultCategory::Calculator:
        CopyToClipboard(res.calcValue);
        return;

    case ResultCategory::Command: {
        // Run via cmd.exe /k so user can see output
        std::wstring cmdLine = L"cmd.exe /k " + res.execPath;
        ShellExecuteW(nullptr, L"open", L"cmd.exe",
                      (L"/k " + res.execPath).c_str(), nullptr, SW_SHOW);
        return;
    }

    case ResultCategory::System: {
        std::wstring kw = ToLower(res.execPath);
        if (kw == L"lock") {
            LockWorkStation();
        } else if (kw == L"sleep") {
            SetSystemPowerState(FALSE, FALSE);
        } else if (kw == L"restart") {
            HANDLE hToken;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
                TOKEN_PRIVILEGES tp = {};
                LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
                CloseHandle(hToken);
            }
            ExitWindowsEx(EWX_REBOOT | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
        } else if (kw == L"shutdown") {
            HANDLE hToken;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken)) {
                TOKEN_PRIVILEGES tp = {};
                LookupPrivilegeValueW(nullptr, SE_SHUTDOWN_NAME, &tp.Privileges[0].Luid);
                tp.PrivilegeCount = 1;
                tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
                AdjustTokenPrivileges(hToken, FALSE, &tp, 0, nullptr, nullptr);
                CloseHandle(hToken);
            }
            ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, SHTDN_REASON_MAJOR_OTHER);
        } else if (kw == L"empty bin") {
            SHEmptyRecycleBinW(nullptr, nullptr, SHERB_NOCONFIRMATION | SHERB_NOPROGRESSUI | SHERB_NOSOUND);
        } else if (kw == L"taskmgr") {
            ShellExecuteW(nullptr, L"open", L"taskmgr.exe", nullptr, nullptr, SW_SHOW);
        }
        return;
    }

    case ResultCategory::Web:
        ShellExecuteW(nullptr, L"open", res.execPath.c_str(), nullptr, nullptr, SW_SHOW);
        return;

    case ResultCategory::App: {
        std::wstring path = res.execPath;
        // Built-in: check for bare exe names
        if (path == L"shell32_run") {
            ShellExecuteW(nullptr, L"open", L"shell32.dll", nullptr, nullptr, SW_SHOW);
            return;
        }
        // If it ends in .lnk, use ShellExecute directly
        // Otherwise treat as exe/system command
        ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOW);
        return;
    }
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW LAYOUT
// ─────────────────────────────────────────────────────────────────────────────

static int CalcWindowHeight() {
    int itemCount = static_cast<int>(g_results.size());
    if (itemCount <= 0) return LAUNCHER_BASE_H;
    return LAUNCHER_BASE_H + RESULT_ITEM_H * itemCount + 8; // 8px bottom padding
}

static void UpdateWindowHeight() {
    if (!g_hwndLauncher) return;
    int newH = CalcWindowHeight();
    RECT rc;
    GetWindowRect(g_hwndLauncher, &rc);
    int currentH = rc.bottom - rc.top;
    if (currentH != newH) {
        SetWindowPos(g_hwndLauncher, nullptr,
                     rc.left, rc.top, LAUNCHER_WIDTH, newH,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    InvalidateRect(g_hwndLauncher, nullptr, FALSE);
}

// ─────────────────────────────────────────────────────────────────────────────
// FONTS
// ─────────────────────────────────────────────────────────────────────────────

static void CreateFonts() {
    HDC hdc = GetDC(nullptr);
    int dpi = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(nullptr, hdc);

    auto MakeFont = [&](int ptSize, int weight, bool italic) -> HFONT {
        int height = -MulDiv(ptSize, dpi, 72);
        return CreateFontW(height, 0, 0, 0, weight, italic ? TRUE : FALSE,
                           FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                           DEFAULT_PITCH | FF_DONTCARE, FONT_NAME);
    };

    g_fontInput  = MakeFont(FONT_SIZE_INPUT,  FW_NORMAL,    false);
    g_fontResult = MakeFont(FONT_SIZE_RESULT, FW_NORMAL,    false);
    g_fontBadge  = MakeFont(FONT_SIZE_BADGE,  FW_SEMIBOLD,  false);
    g_fontIcon   = MakeFont(FONT_SIZE_ICON,   FW_NORMAL,    false);
}

static void DestroyFonts() {
    if (g_fontInput)  { DeleteObject(g_fontInput);  g_fontInput  = nullptr; }
    if (g_fontResult) { DeleteObject(g_fontResult); g_fontResult = nullptr; }
    if (g_fontBadge)  { DeleteObject(g_fontBadge);  g_fontBadge  = nullptr; }
    if (g_fontIcon)   { DeleteObject(g_fontIcon);   g_fontIcon   = nullptr; }
}

// ─────────────────────────────────────────────────────────────────────────────
// PAINTING (Double-buffered, anti-flicker)
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring CategoryLabel(ResultCategory c) {
    switch (c) {
    case ResultCategory::App:        return L"App";
    case ResultCategory::Calculator: return L"Calculator";
    case ResultCategory::Command:    return L"Command";
    case ResultCategory::System:     return L"System";
    case ResultCategory::Web:        return L"Web";
    }
    return L"";
}

static COLORREF CategoryBadgeColor(ResultCategory c) {
    switch (c) {
    case ResultCategory::App:        return COL_BADGE_APP;
    case ResultCategory::Calculator: return COL_BADGE_CALC;
    case ResultCategory::Command:    return COL_BADGE_CMD;
    case ResultCategory::System:     return COL_BADGE_SYS;
    case ResultCategory::Web:        return COL_BADGE_WEB;
    }
    return COL_BADGE_APP;
}

// Draw a filled rounded rectangle
static void FillRoundRect(HDC hdc, const RECT& rc, int radius, COLORREF color) {
    HBRUSH br = CreateSolidBrush(color);
    HPEN   pn = CreatePen(PS_NULL, 0, 0);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPn = SelectObject(hdc, pn);
    RoundRect(hdc, rc.left, rc.top, rc.right, rc.bottom, radius, radius);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPn);
    DeleteObject(br);
    DeleteObject(pn);
}

static void PaintLauncher(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdcReal = BeginPaint(hwnd, &ps);

    RECT clientRc;
    GetClientRect(hwnd, &clientRc);
    int W = clientRc.right;
    int H = clientRc.bottom;

    // Create back buffer
    HDC     hdcMem = CreateCompatibleDC(hdcReal);
    HBITMAP hBmp   = CreateCompatibleBitmap(hdcReal, W, H);
    HGDIOBJ oldBmp = SelectObject(hdcMem, hBmp);

    SetBkMode(hdcMem, TRANSPARENT);

    // ── Background fill
    HBRUSH bgBrush = CreateSolidBrush(COL_BG);
    FillRect(hdcMem, &clientRc, bgBrush);
    DeleteObject(bgBrush);

    // ── Search icon area (left side of input)
    {
        int iconX = 18;
        int iconY = EDIT_TOP_MARGIN + (EDIT_HEIGHT - 20) / 2;
        HFONT oldFont = (HFONT)SelectObject(hdcMem, g_fontIcon);
        SetTextColor(hdcMem, COL_TEXT_SEC);
        // Magnifying glass: U+1F50D is outside BMP. Use U+2315 (telephone recorder) or
        // U+2316 (position indicator) or better U+26B2 (neuter) or simple \x25CB (circle)
        // Use \x2315 (telephone) no... use U+26B2 (neuter sign) as search indicator
        // Actually best BMP search approximation: \x2315 = TELEPHONE RECORDER looks odd
        // Use a simple circle with a stem: \x29BE or just the letter Q with tilt, or
        // just draw a simple circle manually
        // We'll draw a simple magnifier manually:
        HPEN  circPen  = CreatePen(PS_SOLID, 2, COL_TEXT_SEC);
        HBRUSH nullBr  = (HBRUSH)GetStockObject(NULL_BRUSH);
        HGDIOBJ oldPn  = SelectObject(hdcMem, circPen);
        HGDIOBJ oldBr2 = SelectObject(hdcMem, nullBr);
        Ellipse(hdcMem, iconX, iconY, iconX + 16, iconY + 16);
        // Handle line
        MoveToEx(hdcMem, iconX + 12, iconY + 12, nullptr);
        LineTo(hdcMem,   iconX + 18, iconY + 18);
        SelectObject(hdcMem, oldPn);
        SelectObject(hdcMem, oldBr2);
        DeleteObject(circPen);
        SelectObject(hdcMem, oldFont);
    }

    // ── Divider below input bar (only if results exist)
    if (!g_results.empty()) {
        HPEN divPen = CreatePen(PS_SOLID, 1, COL_DIVIDER);
        HGDIOBJ oldPn = SelectObject(hdcMem, divPen);
        MoveToEx(hdcMem, 12, LAUNCHER_BASE_H - 1, nullptr);
        LineTo(hdcMem,   W - 12, LAUNCHER_BASE_H - 1);
        SelectObject(hdcMem, oldPn);
        DeleteObject(divPen);
    }

    // ── Result items
    for (int i = 0; i < (int)g_results.size(); i++) {
        const LaunchResult& res = g_results[i];
        bool selected = (i == g_selectedIdx);

        int itemTop = LAUNCHER_BASE_H + i * RESULT_ITEM_H;
        RECT itemRc = { 0, itemTop, W, itemTop + RESULT_ITEM_H };

        // Selection background
        if (selected) {
            RECT selRc = { 6, itemTop + 3, W - 6, itemTop + RESULT_ITEM_H - 3 };
            FillRoundRect(hdcMem, selRc, 8, COL_SEL_BG);

            // Left accent pill (Electric Blue)
            RECT pillRc = { 8, itemTop + 10, 12, itemTop + RESULT_ITEM_H - 10 };
            FillRoundRect(hdcMem, pillRc, 2, COL_ACCENT);
        }

        // Icon glyph (left column)
        {
            HFONT oldFont = (HFONT)SelectObject(hdcMem, g_fontIcon);
            COLORREF iconColor = selected ? COL_ACCENT : COL_TEXT_SEC;
            SetTextColor(hdcMem, iconColor);
            RECT iconRc = { 18, itemTop, 46, itemTop + RESULT_ITEM_H };
            DrawTextW(hdcMem, res.iconGlyph.c_str(), -1, &iconRc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdcMem, oldFont);
        }

        // Label (main text)
        {
            HFONT oldFont = (HFONT)SelectObject(hdcMem, g_fontResult);
            COLORREF labelColor = selected ? COL_TEXT_PRIMARY : COL_TEXT_PRIMARY;
            SetTextColor(hdcMem, labelColor);

            int labelX = 52;
            int labelY = itemTop + 7;
            RECT labelRc = { labelX, labelY, W - 120, labelY + 20 };
            DrawTextW(hdcMem, res.label.c_str(), -1, &labelRc,
                      DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

            // Sublabel
            if (!res.sublabel.empty()) {
                SetTextColor(hdcMem, COL_TEXT_SEC);
                RECT subRc = { labelX, labelY + 20, W - 120, itemTop + RESULT_ITEM_H - 4 };
                DrawTextW(hdcMem, res.sublabel.c_str(), -1, &subRc,
                          DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
            }
            SelectObject(hdcMem, oldFont);
        }

        // Badge (right side)
        {
            std::wstring badge = CategoryLabel(res.category);
            COLORREF badgeBg = CategoryBadgeColor(res.category);

            HFONT oldFont = (HFONT)SelectObject(hdcMem, g_fontBadge);
            SetTextColor(hdcMem, COL_BADGE_TEXT);

            // Measure badge text
            SIZE textSz = {};
            GetTextExtentPoint32W(hdcMem, badge.c_str(), (int)badge.size(), &textSz);
            int badgeW = textSz.cx + 16;
            int badgeH = 18;
            int badgeX = W - badgeW - 12;
            int badgeY = itemTop + (RESULT_ITEM_H - badgeH) / 2;

            RECT badgeRc = { badgeX, badgeY, badgeX + badgeW, badgeY + badgeH };
            FillRoundRect(hdcMem, badgeRc, 9, badgeBg);
            DrawTextW(hdcMem, badge.c_str(), -1, &badgeRc,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(hdcMem, oldFont);
        }

        // Subtle divider between items
        if (i < (int)g_results.size() - 1 && !selected) {
            HPEN divPen = CreatePen(PS_SOLID, 1, COL_DIVIDER);
            HGDIOBJ oldPn = SelectObject(hdcMem, divPen);
            MoveToEx(hdcMem, 52, itemTop + RESULT_ITEM_H - 1, nullptr);
            LineTo(hdcMem,   W - 12, itemTop + RESULT_ITEM_H - 1);
            SelectObject(hdcMem, oldPn);
            DeleteObject(divPen);
        }
    }

    // ── Blit to real DC
    BitBlt(hdcReal, 0, 0, W, H, hdcMem, 0, 0, SRCCOPY);

    SelectObject(hdcMem, oldBmp);
    DeleteObject(hBmp);
    DeleteDC(hdcMem);

    EndPaint(hwnd, &ps);
}

// ─────────────────────────────────────────────────────────────────────────────
// EDIT CONTROL SUBCLASS (captures arrow keys, Enter, Escape)
// ─────────────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK EditSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (msg == WM_KEYDOWN) {
        switch (wParam) {
        case VK_ESCAPE:
            HideLauncher();
            return 0;

        case VK_RETURN:
            if (g_selectedIdx >= 0 && g_selectedIdx < (int)g_results.size()) {
                ExecuteResult(g_results[g_selectedIdx]);
            }
            return 0;

        case VK_DOWN:
            if (!g_results.empty()) {
                g_selectedIdx++;
                if (g_selectedIdx >= (int)g_results.size()) g_selectedIdx = 0;
                InvalidateRect(g_hwndLauncher, nullptr, FALSE);
            }
            return 0;

        case VK_UP:
            if (!g_results.empty()) {
                g_selectedIdx--;
                if (g_selectedIdx < 0) g_selectedIdx = (int)g_results.size() - 1;
                InvalidateRect(g_hwndLauncher, nullptr, FALSE);
            }
            return 0;
        }
    }

    if (msg == WM_CHAR) {
        LRESULT res = CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
        // Fetch text and update results
        wchar_t buf[512] = {};
        GetWindowTextW(hwnd, buf, 512);
        UpdateResults(buf);
        return res;
    }

    if (msg == WM_KEYUP) {
        // Handle Delete, Backspace, etc.
        if (wParam == VK_DELETE || wParam == VK_BACK) {
            wchar_t buf[512] = {};
            GetWindowTextW(hwnd, buf, 512);
            UpdateResults(buf);
        }
    }

    return CallWindowProcW(g_origEditProc, hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// SHOW / HIDE LAUNCHER
// ─────────────────────────────────────────────────────────────────────────────

static void ShowLauncher() {
    if (!g_hwndLauncher) return;

    // Clear state
    SetWindowTextW(g_hwndEdit, L"");
    g_results.clear();
    g_selectedIdx = -1;

    // Center on primary monitor
    HMONITOR hMon = MonitorFromWindow(GetDesktopWindow(), MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(hMon, &mi);
    int monW = mi.rcWork.right  - mi.rcWork.left;
    int monH = mi.rcWork.bottom - mi.rcWork.top;

    int posX = mi.rcWork.left + (monW - LAUNCHER_WIDTH) / 2;
    int posY = mi.rcWork.top  + (monH / 5);  // Upper-center (20% from top)

    SetWindowPos(g_hwndLauncher, HWND_TOPMOST,
                 posX, posY, LAUNCHER_WIDTH, LAUNCHER_BASE_H,
                 SWP_SHOWWINDOW);

    SetForegroundWindow(g_hwndLauncher);
    SetFocus(g_hwndEdit);
    InvalidateRect(g_hwndLauncher, nullptr, FALSE);
}

static void HideLauncher() {
    if (!g_hwndLauncher) return;
    ShowWindow(g_hwndLauncher, SW_HIDE);
    g_results.clear();
    g_selectedIdx = -1;
}

// ─────────────────────────────────────────────────────────────────────────────
// LAUNCHER WINDOW PROCEDURE
// ─────────────────────────────────────────────────────────────────────────────

static LRESULT CALLBACK LauncherWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // Apply DWM effects: Mica/Acrylic transient backdrop
        BOOL dark = TRUE;
        DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));

        // Rounded corners
        DWM_WINDOW_CORNER_PREFERENCE corner = DWMWCP_ROUND;
        DwmSetWindowAttribute(hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &corner, sizeof(corner));

        // Glass border: subtle blue-gray
        COLORREF borderColor = RGB(55, 55, 65);
        DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &borderColor, sizeof(borderColor));

        // Acrylic/Mica backdrop (Windows 11)
        DWM_SYSTEMBACKDROP_TYPE backdrop = DWMSBT_TRANSIENTWINDOW;
        DwmSetWindowAttribute(hwnd, DWMWA_SYSTEMBACKDROP_TYPE, &backdrop, sizeof(backdrop));

        // Extend frame into client area for blur effect
        MARGINS margins = { -1, -1, -1, -1 };
        DwmExtendFrameIntoClientArea(hwnd, &margins);

        // Create Edit control
        g_hwndEdit = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
            EDIT_PADDING_X, EDIT_TOP_MARGIN,
            LAUNCHER_WIDTH - EDIT_PADDING_X - 12, EDIT_HEIGHT,
            hwnd, (HMENU)1, g_hInst, nullptr
        );

        // Style the edit control
        SetWindowTheme(g_hwndEdit, L"DarkMode_Explorer", nullptr);
        SendMessageW(g_hwndEdit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(4, 4));

        if (g_fontInput) SendMessageW(g_hwndEdit, WM_SETFONT, (WPARAM)g_fontInput, TRUE);

        // Remove edit border / background
        LONG_PTR style = GetWindowLongPtrW(g_hwndEdit, GWL_STYLE);
        style &= ~WS_BORDER;
        SetWindowLongPtrW(g_hwndEdit, GWL_STYLE, style);

        LONG_PTR exStyle = GetWindowLongPtrW(g_hwndEdit, GWL_EXSTYLE);
        exStyle &= ~WS_EX_CLIENTEDGE;
        SetWindowLongPtrW(g_hwndEdit, GWL_EXSTYLE, exStyle);

        // Subclass edit control
        g_origEditProc = (WNDPROC)SetWindowLongPtrW(g_hwndEdit, GWLP_WNDPROC,
                                                      (LONG_PTR)EditSubclassProc);
        return 0;
    }

    case WM_PAINT:
        PaintLauncher(hwnd);
        return 0;

    case WM_ERASEBKGND:
        return 1;  // Prevent flicker; we handle full repaint in WM_PAINT

    case WM_ACTIVATE:
        // Hide when losing focus to another application (not to our own child edit)
        if (LOWORD(wParam) == WA_INACTIVE) {
            // Only hide if the new focus goes to a truly foreign window
            HWND hwndNew = (HWND)lParam;
            if (hwndNew != g_hwndEdit && hwndNew != g_hwndLauncher) {
                HideLauncher();
            }
        }
        return 0;

    case WM_CTLCOLOREDIT: {
        // Make edit control transparent (matches dark background)
        HDC hdcEdit = (HDC)wParam;
        SetBkMode(hdcEdit, TRANSPARENT);
        SetTextColor(hdcEdit, COL_TEXT_PRIMARY);
        SetBkColor(hdcEdit, COL_BG);
        return (LRESULT)GetStockObject(NULL_BRUSH);
    }

    case WM_LBUTTONDOWN: {
        // Check if click is on a result item
        // Use explicit cast instead of GET_X/Y_LPARAM (requires windowsx.h not always available)
        POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
        if (pt.y >= LAUNCHER_BASE_H && !g_results.empty()) {
            int idx = (pt.y - LAUNCHER_BASE_H) / RESULT_ITEM_H;
            if (idx >= 0 && idx < (int)g_results.size()) {
                g_selectedIdx = idx;
                ExecuteResult(g_results[idx]);
            }
        }
        return 0;
    }

    case WM_MOUSEMOVE: {
        // Hover highlight
        POINT pt = { (int)(short)LOWORD(lParam), (int)(short)HIWORD(lParam) };
        if (pt.y >= LAUNCHER_BASE_H && !g_results.empty()) {
            int idx = (pt.y - LAUNCHER_BASE_H) / RESULT_ITEM_H;
            if (idx >= 0 && idx < (int)g_results.size()) {
                if (g_selectedIdx != idx) {
                    g_selectedIdx = idx;
                    InvalidateRect(hwnd, nullptr, FALSE);
                }
            }
        }
        return 0;
    }

    case WM_SHOW_LAUNCHER:
        ShowLauncher();
        return 0;

    case WM_HIDE_LAUNCHER:
        HideLauncher();
        return 0;

    case WM_KEYDOWN:
        if (wParam == VK_ESCAPE) { HideLauncher(); return 0; }
        break;

    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wParam, lParam);
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDOW CREATION
// ─────────────────────────────────────────────────────────────────────────────

static bool CreateLauncherWindow() {
    // Register window class
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.style         = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc   = LauncherWndProc;
    wc.hInstance     = g_hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
    wc.lpszClassName = L"LiquidQuickLauncher";

    g_wndClass = RegisterClassExW(&wc);
    if (!g_wndClass) return false;

    // Create the window (initially hidden)
    // WS_EX_TOOLWINDOW: no taskbar entry
    // No WS_EX_NOACTIVATE so SetFocus/SetForegroundWindow work correctly
    g_hwndLauncher = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        L"LiquidQuickLauncher", L"Liquid Quick Launcher",
        WS_POPUP,
        0, 0, LAUNCHER_WIDTH, LAUNCHER_BASE_H,
        nullptr, nullptr, g_hInst, nullptr
    );

    if (!g_hwndLauncher) return false;

    // Initially hidden
    ShowWindow(g_hwndLauncher, SW_HIDE);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// HOTKEY THREAD (independent from explorer.exe message loop)
// ─────────────────────────────────────────────────────────────────────────────

static void HotkeyThreadFunc() {
    // Create a message-only window in this thread for hotkey registration
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = g_hInst;
    wc.lpszClassName = L"LQLHotkeyMsg";
    RegisterClassExW(&wc);

    g_hwndHotkeyMsg = CreateWindowExW(0, L"LQLHotkeyMsg", nullptr, 0,
                                       0, 0, 0, 0, HWND_MESSAGE,
                                       nullptr, g_hInst, nullptr);
    if (!g_hwndHotkeyMsg) return;

    if (!RegisterHotKey(g_hwndHotkeyMsg, HOTKEY_ID, MOD_ALT | MOD_NOREPEAT, VK_SPACE)) {
        // Try without NOREPEAT flag (older Windows)
        RegisterHotKey(g_hwndHotkeyMsg, HOTKEY_ID, MOD_ALT, VK_SPACE);
    }

    MSG msg = {};
    while (!g_hotkeyStop.load()) {
        BOOL ret = PeekMessageW(&msg, g_hwndHotkeyMsg, 0, 0, PM_REMOVE);
        if (ret) {
            if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
                // Toggle: if visible, hide; else show
                if (g_hwndLauncher) {
                    if (IsWindowVisible(g_hwndLauncher)) {
                        PostMessageW(g_hwndLauncher, WM_HIDE_LAUNCHER, 0, 0);
                    } else {
                        PostMessageW(g_hwndLauncher, WM_SHOW_LAUNCHER, 0, 0);
                    }
                }
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        } else {
            Sleep(10); // Low CPU poll
        }
    }

    UnregisterHotKey(g_hwndHotkeyMsg, HOTKEY_ID);
    DestroyWindow(g_hwndHotkeyMsg);
    g_hwndHotkeyMsg = nullptr;

    UnregisterClassW(L"LQLHotkeyMsg", g_hInst);
}

// ─────────────────────────────────────────────────────────────────────────────
// WINDHAWK MOD ENTRY POINTS
// ─────────────────────────────────────────────────────────────────────────────

BOOL Wh_ModInit() {
    Wh_Log(L"[LiquidQuickLauncher] Initializing...");

    g_hInst = GetModuleHandleW(nullptr);

    // Initialize synchronization
    InitializeCriticalSection(&g_appIndexCS);

    // Create fonts
    CreateFonts();

    // Create launcher window
    if (!CreateLauncherWindow()) {
        Wh_Log(L"[LiquidQuickLauncher] ERROR: Failed to create launcher window.");
        return FALSE;
    }

    // Start hotkey listener thread
    g_hotkeyStop.store(false);
    g_hotkeyThread = std::thread(HotkeyThreadFunc);

    // Start app indexer in background
    StartIndexing();

    Wh_Log(L"[LiquidQuickLauncher] Ready. Press Alt+Space to activate.");
    return TRUE;
}

void Wh_ModUninit() {
    Wh_Log(L"[LiquidQuickLauncher] Uninitializing...");

    // Stop hotkey thread
    g_hotkeyStop.store(true);
    if (g_hotkeyThread.joinable()) {
        // Wake the thread by posting a dummy message
        if (g_hwndHotkeyMsg) {
            PostMessageW(g_hwndHotkeyMsg, WM_NULL, 0, 0);
        }
        g_hotkeyThread.join();
    }

    // Destroy launcher window
    if (g_hwndLauncher) {
        DestroyWindow(g_hwndLauncher);
        g_hwndLauncher = nullptr;
    }

    // Unregister window class
    if (g_wndClass && g_hInst) {
        UnregisterClassW(L"LiquidQuickLauncher", g_hInst);
        g_wndClass = 0;
    }

    // Destroy fonts
    DestroyFonts();

    // Cleanup
    DeleteCriticalSection(&g_appIndexCS);

    Wh_Log(L"[LiquidQuickLauncher] Uninitialized.");
}

void Wh_ModSettingsChanged() {
    // No settings for this mod currently; reserved for future extensibility
}
