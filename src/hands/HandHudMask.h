#pragma once
#include <array>
#include <vector>
namespace kharvox::hands {
// Corners in triangle-strip order; native triangles repeat corner 2 as corner 3.
// Do not replace native coverage with material rectangles. Each eye projects
// these independently; calibration outlines also use nondegenerate quads.
using HandHudQuad=std::array<std::array<float,3>,4>;
using HandHudPanels=std::vector<HandHudQuad>;
}
