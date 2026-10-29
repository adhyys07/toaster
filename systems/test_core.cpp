#include "core/registry.h"
#include "core/system.h"
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

static void exercise(const char* path, const char* expect_core, int w, int h) {
    std::string err;
    emu::SystemPtr sys = emu::createForFile(path, err);
    if (!sys) { std::printf("  routing failed: %s\n", err.c_str()); ++g_failures; return; }
    check(std::string(sys->name()) == expect_core, "routed to the right core");
    if (!sys->loadRom(path, err)) { std::printf("  load: %s\n", err.c_str()); ++g_failures; return; }

    emu::VideoFrame f = sys->video();
    check(f.width == w && f.height == h, "reports its native resolution");
    check(sys->refreshRate() > 50 && sys->refreshRate() < 70, "refresh rate is sane");
    check(sys->aspectRatio() > 0.5 && sys->aspectRatio() < 4.0, "aspect ratio is sane");
    check(!sys->buttons().empty(), "declares its buttons");

    for (const emu::ButtonInfo& b : sys->buttons())
        check(b.name && b.default_key && b.mask != 0, "button entry is fully populated");

    for (int i = 0; i < 10; ++i) sys->runFrame();
    f = sys->video();
    check(f.pixels != nullptr, "produces a framebuffer after running");

    std::vector<float> buf(4096);
    size_t got = sys->readAudio(buf.data(), buf.size());
    check(got <= buf.size(), "audio drain respects the caller's buffer");

    sys->setInput(0, 0xFFFFFFFFu); 
    sys->runFrame();
    sys->setInput(0, 0);
    sys->reset();
    sys->runFrame();
    check(true, "survives input, reset and re-run");
}

int main() {
    std::printf("registry\n");
    check(emu::registeredCores().size() >= 2, "at least two cores are compiled in");
    {
        std::string err;
        check(emu::createForFile("game.unknownext", err) == nullptr && !err.empty(),
              "an unknown extension is rejected with a helpful message");
        check(err.find(".nes") != std::string::npos,
              "the error lists the extensions that are supported");
    }

    std::printf("NES core via the shell interface\n");
    exercise("roms/bgtest.nes", "NES", 256, 240);

    std::printf("CHIP-8 core via the shell interface\n");
    exercise("roms/fonttest.ch8", "CHIP-8", 64, 32);

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
