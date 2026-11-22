// SPDX-License-Identifier: GPL-3.0-or-later
#include "../src/fluted_reeds.hpp"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using acrylic::FlutedReeds;
void Check(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
bool Near(double a, double b) { return std::fabs(a - b) < 0.001; }
void Layout() {
    Check(FlutedReeds(0, 96).empty() && FlutedReeds(300, 0).empty() && FlutedReeds(-5, 96).empty(), "no width or scale, no reeds");
    for (unsigned dpi : {96u, 144u, 288u}) {
        const double width = 296.0 * dpi / 96;
        const auto reeds = FlutedReeds(width, dpi);
        Check(reeds.size() == 21, "reeds stay about 14 DIPs wide at every scale");
        double edge = 0;
        for (const auto& reed : reeds) {
            Check(Near(reed.left, edge) && Near(reed.width, width / 21), "reeds are equal and touch");
            edge += reed.width;
        }
        Check(Near(edge, width), "reeds fill the glass exactly");
    }
    Check(FlutedReeds(8, 96).size() == 1, "a surface narrower than a reed is one reed");
}
void Bends() {
    const double width = 296;
    const auto reeds = FlutedReeds(width, 96);
    for (const auto& reed : reeds) {
        const double center = reed.left + reed.width / 2;
        for (double k : {reed.red, reed.blue})
            Check(center - k * reed.width / 2 >= -0.001 && center + k * reed.width / 2 <= width + 0.001, "no reed reads past the glass");
        Check(reed.red <= reed.blue && reed.red > 0.999, "every reed flips; blue bends at least as far as red");
    }
    Check(Near(reeds.front().red, 1) && Near(reeds.front().blue, 1) && Near(reeds.back().blue, 1), "the outermost reeds only flip");
    Check(Near(reeds[1].red, acrylic::RedBend) && Near(reeds[1].blue, 3), "the second reed bends red fully, blue as far as it can");
    Check(Near(reeds[10].red, acrylic::RedBend) && Near(reeds[10].blue, acrylic::BlueBend), "the middle reeds bend fully");
}
int main() {
    try {
        Layout();
        Bends();
        std::puts("PASS: fluted glass reeds about 14 DIPs at every scale, equal, touching and filling the glass; bends that never read past the edge, full in the middle.");
        return 0;
    } catch (const std::exception& error) {
        std::printf("FAIL: %s\n", error.what());
        return 1;
    }
}
