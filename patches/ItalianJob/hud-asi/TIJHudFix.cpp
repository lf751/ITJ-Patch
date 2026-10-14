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
#include "pause_patterns.h"
#include "frontend_patterns.h"
#include "frontend_custom_patterns.h"

namespace {
HMODULE selfModule;
volatile LONG started = 0, firstDraw = 0;
int* screenWidth = nullptr;
int* screenHeight = nullptr;
using UiUnits = float (__cdecl*)(int);
UiUnits originalUnits = nullptr;
using Identity = int (__cdecl*)();
using Translate = int (__cdecl*)(float, float);
using PathRect = void* (__cdecl*)(void*, float, float, float, float);
Identity originalIdentity = nullptr;
Translate translate = nullptr;
PathRect originalRect = nullptr;

int __cdecl CenteredIdentity()
{
    int result = originalIdentity();
    translate(CenterAnchorOffset(*screenWidth, *screenHeight), 0.0f);
    return result;
}

void* __cdecl FullscreenRect(void* path, float, float y, float, float height)
{
    const int width = *screenWidth, screenH = *screenHeight;
    float offset = CenterAnchorOffset(width, screenH);
    float span = 4.0f / 3.0f + RightAnchorOffset(width, screenH);
    return originalRect(path, -offset, y, span, height);
}

void* __cdecl FadeRect(void* path, float x, float y, float, float height)
{
    float span = 4.0f / 3.0f + RightAnchorOffset(*screenWidth, *screenHeight);
    return originalRect(path, x, y, span, height);
}

void* __cdecl MenuArtRect(void* path)
{
    return originalRect(path, CenterAnchorOffset(*screenWidth, *screenHeight),
        0.0f, 4.0f / 3.0f, 1.0f);
}

struct Hook { unsigned char* call; uintptr_t replacement; };

unsigned char* CallTarget(unsigned char* call)
{
    return *call == 0xE8 ? call + 5 + *reinterpret_cast<int32_t*>(call + 1) : nullptr;
}

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
        if (!(section->Characteristics & IMAGE_SCN_MEM_EXECUTE) &&
            memcmp(section->Name, ".text\0\0\0", 8) && memcmp(section->Name, "_rwcseg\0", 8)) continue;
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

bool ApplyHooks(const std::vector<Hook>& hooks)
{
    // All guards pass before any call site is redirected.
    std::vector<DWORD> protections(hooks.size());
    size_t unlocked = 0;
    for (; unlocked < hooks.size(); ++unlocked) {
        if (!VirtualProtect(hooks[unlocked].call, 5, PAGE_EXECUTE_READWRITE, &protections[unlocked])) break;
    }
    if (unlocked != hooks.size()) {
        while (unlocked) { --unlocked; DWORD unused; VirtualProtect(hooks[unlocked].call, 5, protections[unlocked], &unused); }
        Log("Cannot write hooks; no hooks installed."); return false;
    }
    for (const auto& hook : hooks) {
        int32_t relative = static_cast<int32_t>(hook.replacement - reinterpret_cast<uintptr_t>(hook.call + 5));
        memcpy(hook.call + 1, &relative, 4);
        FlushInstructionCache(GetCurrentProcess(), hook.call, 5);
    }
    for (size_t i = hooks.size(); i-- > 0;) { DWORD unused; VirtualProtect(hooks[i].call, 5, protections[i], &unused); }
    return true;
}

void InstallFrontend(unsigned char* base)
{
    auto text = Find(base, Pattern(FRONTEND_TEXT_IDENTITY_PATTERN));
    auto highlight = Find(base, Pattern(FRONTEND_HIGHLIGHT_IDENTITY_PATTERN));
    auto selection = Find(base, Pattern(FRONTEND_SELECTION_IDENTITY_PATTERN));
    auto resolution = Find(base, Pattern(FRONTEND_RESOLUTION_PATTERN));
    auto translation = Find(base, Pattern(FRONTEND_TRANSLATE_PATTERN));
    auto background = Find(base, Pattern(FRONTEND_BACKGROUND_PATTERN));
    if (text.size() != 1 || highlight.size() != 1 || selection.size() != 1 ||
        resolution.size() != 1 || translation.size() != 1 || background.size() != 1) {
        Log("Unsupported frontend layout; no hooks installed."); return;
    }
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const uintptr_t start = reinterpret_cast<uintptr_t>(base), end = start + nt->OptionalHeader.SizeOfImage;
    auto height = *reinterpret_cast<uint32_t*>(resolution[0] + 3);
    auto width = *reinterpret_cast<uint32_t*>(resolution[0] + 10);
    std::vector<unsigned char*> calls = {text[0] + 5, highlight[0] + 5, selection[0] + 5};
    struct Group { const char* pattern; size_t count; };
    const Group groups[] = {
        {FRONTEND_CUSTOM_00405561_PATTERN, 1}, {FRONTEND_CUSTOM_00405666_PATTERN, 1},
        {FRONTEND_CUSTOM_0040579D_PATTERN, 1}, {FRONTEND_CUSTOM_0040693F_PATTERN, 2},
        {FRONTEND_CUSTOM_00406B33_PATTERN, 2}, {FRONTEND_CUSTOM_00408323_PATTERN, 1},
        {FRONTEND_CUSTOM_0040900C_PATTERN, 1}, {FRONTEND_CUSTOM_0040B43E_PATTERN, 1}
    };
    for (const auto& group : groups) {
        auto matches = Find(base, Pattern(group.pattern));
        if (matches.size() != group.count) { Log("Unsupported frontend graphics layout; no hooks installed."); return; }
        calls.insert(calls.end(), matches.begin(), matches.end());
    }
    auto identity = CallTarget(calls[0]);
    auto fades = Find(base, Pattern(FRONTEND_FADE_RECT_PATTERN));
    auto rectangle = fades.size() == 2 ? CallTarget(fades[0] + 67) : nullptr;
    auto fullViewport = CallTarget(background[0] + 29);
    if (!identity || !rectangle || CallTarget(fades[1] + 67) != rectangle ||
        !fullViewport || reinterpret_cast<uintptr_t>(fullViewport) < start || reinterpret_cast<uintptr_t>(fullViewport) >= end ||
        CallTarget(background[0] + 8) != identity ||
        reinterpret_cast<uintptr_t>(identity) < start || reinterpret_cast<uintptr_t>(identity) >= end ||
        reinterpret_cast<uintptr_t>(rectangle) < start || reinterpret_cast<uintptr_t>(rectangle) >= end ||
        width < start || width + 4 > end || height < start || height + 4 > end) {
        Log("Frontend target guards failed; no hooks installed."); return;
    }
    for (auto call : calls) if (CallTarget(call) != identity) { Log("Frontend identity targets differ; no hooks installed."); return; }
    screenWidth = reinterpret_cast<int*>(width); screenHeight = reinterpret_cast<int*>(height);
    originalIdentity = reinterpret_cast<Identity>(identity);
    translate = reinterpret_cast<Translate>(translation[0]);
    originalRect = reinterpret_cast<PathRect>(rectangle);
    std::vector<Hook> hooks;
    for (auto call : calls) hooks.push_back({call, reinterpret_cast<uintptr_t>(&CenteredIdentity)});
    for (auto fade : fades) hooks.push_back({fade + 67, reinterpret_cast<uintptr_t>(&FadeRect)});
    hooks.push_back({background[0] + 29, reinterpret_cast<uintptr_t>(&MenuArtRect)});
    if (ApplyHooks(hooks)) Log("Installed: coherent centered 4:3 frontend artwork, 13 UI scopes and 2 full-width fades.");
}

DWORD WINAPI Install(void*)
{
    if (InterlockedCompareExchange(&started, 1, 0) != 0) return 0;
    char executable[MAX_PATH]; GetModuleFileNameA(nullptr, executable, MAX_PATH);
    const char* name = strrchr(executable, '\\'); name = name ? name + 1 : executable;
    auto base = reinterpret_cast<unsigned char*>(GetModuleHandleA(nullptr));
    if (!_stricmp(name, "TIJ.exe")) { InstallFrontend(base); return 0; }
    if (strlen(name) != 10 || _strnicmp(name, "stg", 3) || _stricmp(name + 6, ".exe") ||
        name[3] < '0' || name[3] > '9' || name[4] < '0' || name[4] > '9' || name[5] < '0' || name[5] > '9') return 0;
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
    auto pause = Find(base, Pattern(PAUSE_IDENTITY_PATTERN));
    auto translation = Find(base, Pattern(PAUSE_TRANSLATE_PATTERN));
    auto background = Find(base, Pattern(PAUSE_BACKGROUND_RECT_PATTERN));
    auto topBar = Find(base, Pattern(PAUSE_TOP_BAR_RECT_PATTERN));
    auto bottomBar = Find(base, Pattern(PAUSE_BOTTOM_BAR_RECT_PATTERN));
    if (pause.size() != 1 || translation.size() != 1 || background.size() != 1 ||
        topBar.size() != 1 || bottomBar.size() != 1 || background[0] != pause[0] + 1581 ||
        topBar[0] != pause[0] + 1734 || bottomBar[0] != pause[0] + 1892) {
        Log("Unsupported pause layout; no hooks installed."); return 0;
    }
    auto identityCall = pause[0] + PAUSE_IDENTITY_CALL_OFFSET;
    auto identity = CallTarget(identityCall);
    unsigned char* rectCalls[] = {background[0] + PAUSE_BACKGROUND_RECT_CALL_OFFSET,
        topBar[0] + PAUSE_TOP_BAR_RECT_CALL_OFFSET, bottomBar[0] + PAUSE_BOTTOM_BAR_RECT_CALL_OFFSET};
    auto rectangle = CallTarget(rectCalls[0]);
    if (!identity || !rectangle || CallTarget(rectCalls[1]) != rectangle || CallTarget(rectCalls[2]) != rectangle ||
        reinterpret_cast<uintptr_t>(identity) < imageStart || reinterpret_cast<uintptr_t>(identity) >= imageEnd ||
        reinterpret_cast<uintptr_t>(rectangle) < imageStart || reinterpret_cast<uintptr_t>(rectangle) >= imageEnd) {
        Log("Pause target guards failed; no hooks installed."); return 0;
    }
    originalIdentity = reinterpret_cast<Identity>(identity);
    translate = reinterpret_cast<Translate>(translation[0]);
    originalRect = reinterpret_cast<PathRect>(rectangle);
    std::vector<Hook> hooks;
    for (auto call : calls) hooks.push_back({call, reinterpret_cast<uintptr_t>(&RightAlignedUiUnits)});
    hooks.push_back({identityCall, reinterpret_cast<uintptr_t>(&CenteredIdentity)});
    for (auto call : rectCalls) hooks.push_back({call, reinterpret_cast<uintptr_t>(&FullscreenRect)});
    if (!ApplyHooks(hooks)) return 0;
    Log("Installed: right-aligned speedometer, centered pause UI and full-width pause backgrounds.");
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
