#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <vector>
#include <string>
#include <cstring>
#include <cstdlib>
#include <cstdio>
#include "anchor_math.h"
#include "patterns.h"

namespace {
HMODULE selfModule;
volatile LONG started = 0, firstDraw = 0;
int* screenWidth = nullptr;
int* screenHeight = nullptr;
using UiUnits = float (__cdecl*)(int);
UiUnits originalUnits = nullptr;

void Log(const char* message)
{
    char path[MAX_PATH];
    if (!GetModuleFileNameA(selfModule, path, MAX_PATH)) return;
    char* dot = strrchr(path, '.');
    if (!dot) return;
    strcpy_s(dot, MAX_PATH - static_cast<size_t>(dot - path), ".log");
    HANDLE file = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(file, message, static_cast<DWORD>(strlen(message)), &written, nullptr);
    WriteFile(file, "\r\n", 2, &written, nullptr);
    CloseHandle(file);
}

struct Pattern {
    std::vector<int> bytes;
    explicit Pattern(const char* text)
    {
        while (*text) {
            if (*text == ' ') { ++text; continue; }
            if (*text == '?') { bytes.push_back(-1); while (*text == '?') ++text; continue; }
            char pair[3] = { text[0], text[1], 0 };
            bytes.push_back(static_cast<int>(strtoul(pair, nullptr, 16)));
            text += 2;
        }
    }
};

std::vector<unsigned char*> Find(unsigned char* base, const Pattern& pattern)
{
    std::vector<unsigned char*> matches;
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    auto section = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        auto start = base + section->VirtualAddress;
        size_t length = section->Misc.VirtualSize;
        if (length < pattern.bytes.size()) continue;
        for (size_t offset = 0; offset <= length - pattern.bytes.size(); ++offset) {
            bool match = true;
            for (size_t j = 0; j < pattern.bytes.size(); ++j)
                if (pattern.bytes[j] >= 0 && start[offset + j] != pattern.bytes[j]) { match = false; break; }
            if (match) matches.push_back(start + offset);
        }
    }
    return matches;
}

float __cdecl RightAlignedUiUnits(int pixels)
{
    const int width = *screenWidth, height = *screenHeight;
    if (width > 0 && height > 0 && InterlockedCompareExchange(&firstDraw, 1, 0) == 0) {
        char message[160];
        sprintf_s(message, "Active HUD anchor: %dx%d. Gauge, bezel and needle moved together.", width, height);
        Log(message);
    }
    return originalUnits(pixels) + RightAnchorOffset(width, height);
}

DWORD WINAPI Install(void*)
{
    if (InterlockedCompareExchange(&started, 1, 0) != 0) return 0;
    char executable[MAX_PATH]; GetModuleFileNameA(nullptr, executable, MAX_PATH);
    const char* name = strrchr(executable, '\\'); name = name ? name + 1 : executable;
    if (strlen(name) != 10 || _strnicmp(name, "stg", 3) || _stricmp(name + 6, ".exe") ||
        name[3] < '0' || name[3] > '9' || name[4] < '0' || name[4] > '9' || name[5] < '0' || name[5] > '9') return 0;
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const uintptr_t imageStart = reinterpret_cast<uintptr_t>(base), imageEnd = imageStart + nt->OptionalHeader.SizeOfImage;
    auto rects = Find(base, Pattern(GAUGE_RECT_PATTERN));
    auto needles = Find(base, Pattern(GAUGE_NEEDLE_PATTERN));
    auto resolution = Find(base, Pattern(RESOLUTION_PATTERN));
    if (rects.size() != 2 || needles.size() != 1 || resolution.size() != 1) {
        Log("Unsupported mission layout; no hooks installed."); return 0;
    }
    auto widthAddress = *reinterpret_cast<uint32_t*>(resolution[0] + 25);
    auto heightAddress = *reinterpret_cast<uint32_t*>(resolution[0] + 36);
    if (widthAddress < imageStart || widthAddress + 4 > imageEnd || heightAddress < imageStart || heightAddress + 4 > imageEnd) {
        Log("Invalid resolution globals; no hooks installed."); return 0;
    }
    unsigned char* calls[3] = { rects[0] + 41, rects[1] + 41, needles[0] + 15 };
    unsigned char* target = nullptr;
    for (auto call : calls) {
        if (*call != 0xE8) { Log("Call guard failed; no hooks installed."); return 0; }
        auto current = call + 5 + *reinterpret_cast<int32_t*>(call + 1);
        if (!target) target = current;
        if (current != target || reinterpret_cast<uintptr_t>(target) < imageStart || reinterpret_cast<uintptr_t>(target) >= imageEnd) {
            Log("UI conversion target mismatch; no hooks installed."); return 0;
        }
    }
    screenWidth = reinterpret_cast<int*>(widthAddress); screenHeight = reinterpret_cast<int*>(heightAddress);
    originalUnits = reinterpret_cast<UiUnits>(target);
    // All guards pass before any call site is redirected.
    DWORD protections[3];
    unsigned unlocked = 0;
    for (; unlocked < 3; ++unlocked) {
        if (!VirtualProtect(calls[unlocked], 5, PAGE_EXECUTE_READWRITE, &protections[unlocked])) break;
    }
    if (unlocked != 3) {
        while (unlocked) { --unlocked; DWORD unused; VirtualProtect(calls[unlocked], 5, protections[unlocked], &unused); }
        Log("Cannot write hooks; no hooks installed."); return 0;
    }
    for (unsigned i = 0; i < 3; ++i) {
        int32_t relative = static_cast<int32_t>(reinterpret_cast<uintptr_t>(&RightAlignedUiUnits) - reinterpret_cast<uintptr_t>(calls[i] + 5));
        memcpy(calls[i] + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), calls[i], 5);
    }
    for (int i = 2; i >= 0; --i) { DWORD unused; VirtualProtect(calls[i], 5, protections[i], &unused); }
    Log("Installed: 3 right-aligned speedometer anchors. World, damage bar and other UI unchanged.");
    return 0;
}
}

extern "C" __declspec(dllexport) void InitializeASI() { Install(nullptr); }

BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH) {
        selfModule = module;
        DisableThreadLibraryCalls(module);
        HANDLE worker = CreateThread(nullptr, 0, Install, nullptr, 0, nullptr);
        if (worker) CloseHandle(worker);
    }
    return TRUE;
}
