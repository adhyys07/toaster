#include "core/registry.h"
#include "core/system.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <vector>

static int g_failures = 0;
static void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

// `state_advances` says whether this core's save state must differ after the
// machine runs on. It is false for a core whose test ROM idles in a tight loop
// and whose state holds no running clock, where returning to an identical
// state is correct rather than a bug.
static void exercise(const char* path, const char* expect_core, int w, int h,
                     bool state_advances = true) {
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

    std::vector<uint8_t> state;
    check(sys->saveState(state) && !state.empty(), "produces a save state");

    // Run on, then rewind. Comparing the framebuffer alone would pass on a
    // core whose CPU was never restored, so the real check is that saving
    // again after a load reproduces the original blob byte for byte.
    const emu::VideoFrame before = sys->video();
    std::vector<uint32_t> snapshot(
        before.pixels, before.pixels + before.width * before.height);

    for (int i = 0; i < 30; ++i) sys->runFrame();
    if (state_advances) {
        std::vector<uint8_t> advanced;
        check(sys->saveState(advanced) && advanced != state,
              "running the machine changes its state");
    }

    check(sys->loadState(state), "save state restores successfully");
    const emu::VideoFrame after = sys->video();
    check(std::equal(snapshot.begin(), snapshot.end(), after.pixels),
          "restored state reproduces the saved frame");

    std::vector<uint8_t> resaved;
    check(sys->saveState(resaved) && resaved == state,
          "a load followed by a save round-trips exactly");

    // A corrupt state must be rejected, not half-applied: the machine has to
    // still match the state that was last loaded successfully.
    std::vector<uint8_t> truncated(state.begin(),
                                   state.begin() + state.size() / 2);
    check(!sys->loadState(truncated), "truncated states are rejected");
    std::vector<uint8_t> tampered = state;
    tampered[0] ^= 0xFF;
    check(!sys->loadState(tampered), "states with a bad magic are rejected");

    check(sys->saveState(resaved) && resaved == state,
          "a rejected state leaves the machine untouched");

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
    // The CHIP-8 ROM draws one glyph and then jumps to itself forever.
    exercise("roms/fonttest.ch8", "CHIP-8", 64, 32, /*state_advances=*/false);

    std::printf("Game Boy core via the shell interface\n");
    exercise("roms/regtest.gb", "GAME BOY", 160, 144);

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
