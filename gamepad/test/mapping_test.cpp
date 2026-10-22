#include "gamepad/mapping.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
void expect(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAILED: %s\n", message);
        std::exit(1);
    }
}

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}
} // namespace

int main()
{
    using namespace allnes::gamepad;

    // --- parseDpadCoverage -------------------------------------------------
    const std::string full =
        "030000005e0400008e02000014010000,Xbox 360 Controller,"
        "a:b0,b:b1,dpup:h0.1,dpright:h0.2,dpdown:h0.4,dpleft:h0.8,"
        "platform:Mac OS X,";
    expect(parseDpadCoverage(full).complete(), "full hat D-pad should be complete");

    const std::string partial =
        "030000005e0400008e02000014010000,Xbox 360 Controller,"
        "a:b0,dpup:b11,dpdown:b12,dpleft:b13,platform:Mac OS X,";
    const DpadCoverage coverage = parseDpadCoverage(partial);
    expect(coverage.up && coverage.down && coverage.left, "partial directions parsed");
    expect(!coverage.right && !coverage.complete(), "missing right should be detected");

    expect(!parseDpadCoverage("guid,name,a:b0,").complete(), "no D-pad fields");
    expect(!parseDpadCoverage("guid,name,a:b11,leftshoulder:b4,").up,
           "button fields must not count as D-pad fields");

    // --- buildGenericMapping ----------------------------------------------
    const std::string hatPad = buildGenericMapping("guid123", { 15, 6, 1 });
    expect(contains(hatPad, ",a:b0,"), "button fields carry the b prefix");
    expect(contains(hatPad, ",leftx:a0,"), "axis fields carry the a prefix");
    expect(contains(hatPad, ",dpup:h0.1,"), "hat pad dpup");
    expect(contains(hatPad, ",dpright:h0.2,"), "hat pad dpright");
    expect(contains(hatPad, ",dpdown:h0.4,"), "hat pad dpdown");
    expect(contains(hatPad, ",dpleft:h0.8,"), "hat pad dpleft");
    expect(contains(hatPad, ",lefttrigger:a4,"), "fallback trigger axis");

    const std::string buttonPad = buildGenericMapping("guid456", { 15, 6, 0 });
    expect(contains(buttonPad, ",dpup:b11,"), "360 dpup");
    expect(contains(buttonPad, ",dpdown:b12,"), "360 dpdown");
    expect(contains(buttonPad, ",dpleft:b13,"), "360 dpleft");
    expect(contains(buttonPad, ",dpright:b14,"), "360 dpright");

    const std::string noDpad = buildGenericMapping("guid789", { 11, 4, 0 });
    expect(!contains(noDpad, "dpup:"), "no D-pad source on an 11-button pad");

    // --- completeDpadMapping ----------------------------------------------
    expect(completeDpadMapping(full, { 15, 6, 1 }).empty(),
           "complete mapping must not be rewritten");

    const std::string patchedHat = completeDpadMapping(partial, { 15, 6, 1 });
    expect(parseDpadCoverage(patchedHat).complete(), "partial mapping completed from hat");
    expect(contains(patchedHat, "dpup:h0.1"), "hat complete replaces button D-pad");
    expect(!contains(patchedHat, "dpup:b11"), "old dp field removed");
    expect(contains(patchedHat, "Xbox 360 Controller"), "name preserved");
    expect(contains(patchedHat, "a:b0"), "other fields preserved");
    expect(contains(patchedHat, "platform:Mac OS X"), "platform preserved");

    const std::string patchedButtons = completeDpadMapping(partial, { 15, 6, 0 });
    expect(parseDpadCoverage(patchedButtons).complete(), "partial mapping completed from buttons");
    expect(contains(patchedButtons, "dpup:b11"), "button completion");
    expect(contains(patchedButtons, "dpright:b14"), "button completion right");

    expect(completeDpadMapping(partial, { 11, 6, 0 }).empty(),
           "cannot complete without a hat or D-pad buttons");
    expect(completeDpadMapping("garbage", { 15, 6, 1 }).empty(),
           "mapping without guid/name is rejected");
    expect(completeDpadMapping("guid,name", { 15, 6, 1 }).find("dpup:h0.1") !=
               std::string::npos,
           "mapping with only guid/name is completed");

    std::puts("gamepad mapping tests passed");
    return 0;
}
