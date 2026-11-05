// A core manifest is what stands between a tampered download and
// LoadLibrary, so every way it can be malformed has to fail closed, and the
// formats real tools emit have to be accepted.
#include "core_manifest.h"

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <string>

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
    std::fflush(stdout);
    if (!ok) ++g_failures;
}

void group(const char* name) {
    std::printf("\n%s\n", name);
}

emu::frontend::CoreManifest parse(const std::string& text) {
    std::istringstream input(text);
    return emu::frontend::parseCoreManifest(input);
}

// A valid 64-character digest.
const std::string kDigest =
    "023fa2f7ec559e58a1c082a3fa2612f8149d8d9b0657371a69b4fad10f9420db";

} // namespace

int main() {
    std::printf("core manifest parsing\n");

    group("formats real tools produce");
    {
        // "sha256sum -t" and the PowerShell Get-FileHash recipe.
        const auto text = parse(kDigest + "  swanstation_libretro.dll\n");
        check(text.size() == 1 && text.count("swanstation_libretro.dll") == 1,
              "text-mode sha256sum output parses");
        if (text.count("swanstation_libretro.dll")) {
            check(text.at("swanstation_libretro.dll") == kDigest,
                  "the digest is kept");
        }

        // sha256sum defaults to binary mode on Windows and marks the name
        // with an asterisk. Treating it as part of the filename was a real
        // trap: the lookup then silently missed.
        const auto binary = parse(kDigest + " *swanstation_libretro.dll\n");
        check(binary.count("swanstation_libretro.dll") == 1,
              "binary-mode output drops the leading asterisk");

        // Get-FileHash writes upper case.
        std::string upper = kDigest;
        for (char& c : upper) c = static_cast<char>(std::toupper(c));
        const auto cased = parse(upper + "  core.dll\n");
        check(cased.count("core.dll") == 1 && cased.at("core.dll") == kDigest,
              "an upper-case digest is normalised");

        // A manifest edited on Windows.
        const auto crlf = parse(kDigest + "  core.dll\r\n");
        check(crlf.count("core.dll") == 1, "CRLF line endings are tolerated");

        const auto many = parse(
            kDigest + "  a.dll\n" + kDigest + " *b.dll\r\n" +
            kDigest + "  c.dll\n");
        check(many.size() == 3, "several entries parse");
    }

    group("malformed entries are dropped");
    {
        check(parse("").empty(), "an empty manifest yields nothing");
        check(parse("\n\n   \n").empty(), "blank lines yield nothing");
        check(parse("not-a-digest  core.dll\n").empty(),
              "a digest that is not hex is rejected");
        check(parse("abc123  core.dll\n").empty(),
              "a digest that is too short is rejected");
        check(parse(kDigest + "0  core.dll\n").empty(),
              "a digest that is too long is rejected");
        check(parse(kDigest + "\n").empty(),
              "a digest with no filename is rejected");

        // A truncated final line must not poison the entries before it.
        const auto partial = parse(kDigest + "  good.dll\n" + kDigest);
        check(partial.size() == 1 && partial.count("good.dll") == 1,
              "a truncated last line leaves earlier entries intact");
    }

    group("path traversal is refused");
    {
        check(parse(kDigest + "  ../evil.dll\n").empty(),
              "a relative parent path is rejected");
        check(parse(kDigest + "  sub/evil.dll\n").empty(),
              "a forward-slash path is rejected");
        check(parse(kDigest + "  sub\\evil.dll\n").empty(),
              "a backslash path is rejected");
        check(parse(kDigest + "  C:\\windows\\system32\\evil.dll\n").empty(),
              "an absolute Windows path is rejected");

        // One bad line must not discard the good ones around it.
        const auto mixed = parse(
            kDigest + "  good.dll\n" +
            kDigest + "  ../evil.dll\n" +
            kDigest + "  also-good.dll\n");
        check(mixed.size() == 2 && mixed.count("good.dll") &&
                  mixed.count("also-good.dll"),
              "a rejected line does not discard valid neighbours");
    }

    group("duplicates");
    {
        const std::string other =
            "9865e52c2a037f2a2abe7e0465e3a918470e5bae6e4ecdb5b4ab6bc9a9e8909e";
        const auto repeated =
            parse(kDigest + "  core.dll\n" + other + "  core.dll\n");
        check(repeated.size() == 1 && repeated.at("core.dll") == other,
              "a repeated filename keeps the last digest");
    }

    std::printf("\n%s (%d failure%s)\n",
                g_failures == 0 ? "ALL TESTS PASSED" : "TESTS FAILED",
                g_failures, g_failures == 1 ? "" : "s");
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
