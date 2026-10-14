#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <cstring>

int main(int argc, char** argv)
{
    if (argc != 3) return 1;
    // Map only: avoid running the renderer/bridge initialization in this test process.
    HMODULE image = LoadLibraryExA(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!image) { std::printf("Load failed: %lu\n", GetLastError()); return 2; }
    const bool corrected = std::atoi(argv[2]) != 0;
    const unsigned char original[] = {0xe8,0x7e,0x92,0x06,0};
    const unsigned char patched[] = {0xdd,0x04,0x24,0x90,0x90};
    auto base = reinterpret_cast<unsigned char*>(image);
    auto dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.SizeOfImage < 0x26d02 ||
        std::memcmp(base + 0x26cfd, corrected ? patched : original, 5)) {
        std::puts("Unsupported bridge helper layout."); return 5;
    }
    using Count = unsigned (__cdecl*)(unsigned, unsigned, unsigned);
    auto count = reinterpret_cast<Count>(reinterpret_cast<unsigned char*>(image) + 0x26cc0);
    unsigned tests = 0;
    for (unsigned width = 1; width <= 4096; ++width) {
        unsigned height = width % 257 + 1, depth = width % 17 + 1;
        unsigned size = std::max(width, std::max(height, depth));
        unsigned expected = 0;
        for (unsigned n = size; n; n >>= 1) ++expected;
        if (!corrected && (size & (size - 1))) ++expected;
        unsigned actual = count(width, height, depth);
        if (actual != expected) {
            std::printf("FAIL: %u,%u,%u: got %u expected %u\n", width,height,depth,actual,expected);
            return 3;
        }
        ++tests;
    }
    unsigned turin = count(192,128,1);
    if (turin != (corrected ? 8u : 9u)) return 4;
    std::printf("PASS: %u native mip-count cases; 192x128 returns %u (%s).\n", tests,turin,corrected?"corrected":"original");
    return 0;
}
