// Checks the 3D scene's geometry without opening a window.
// The key idea: what the scene draws must agree with what calculations computed.
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <string>

#include "calculations/ops.h"
#include "graphics/scene_math.h"

using namespace ml;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n";        \
        }                                                                    \
    } while (0)

static Matrix make3(std::initializer_list<double> v) {
    Matrix m(3, 3);
    std::size_t i = 0;
    for (double x : v) { m(i / 3, i % 3) = x; ++i; }
    return m;
}
static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }
static bool same(V3 a, V3 b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.z, b.z); }

int main() {
    const Matrix A = make3({2, 1, 1, 1, 3, 2, 1, 0, 0});   // determinant -1
    const Matrix B = make3({1, 2, 0, 0, 1, 3, 4, 0, 1});
    const Matrix I = identity3();

    // --- small pieces ---
    CHECK(near(ease(0), 0) && near(ease(1), 1) && near(ease(0.5), 0.5) && ease(-3) == 0 && ease(7) == 1);
    CHECK(same(column(A, 0), V3{2, 1, 1}) && near(triple({1, 0, 0}, {0, 1, 0}, {0, 0, 1}), 1));
    const Matrix half = mix(I, A, 0.5);
    CHECK(near(half(0, 0), 1.5) && near(half(1, 0), 0.5) && near(half(1, 1), 2.0));
    const Matrix rows = mixRows(I, A, {0.0, 1.0, 0.5});
    CHECK(near(rows(0, 0), 1.0) && near(rows(1, 1), 3.0) && near(rows(2, 2), 0.5));

    // Faces: each face has 4 distinct valid corners, and a face is a loop (neighbours differ by one bit).
    for (int axis = 0; axis < 3; ++axis)
        for (int side = 0; side < 2; ++side) {
            const auto f = faceCorners(axis, side);
            for (int i = 0; i < 4; ++i) {
                CHECK(f[i] >= 0 && f[i] < 8 && ((f[i] >> axis) & 1) == side);
                const int diff = f[i] ^ f[(i + 1) % 4];
                CHECK(diff == 1 || diff == 2 || diff == 4);
                for (int k = i + 1; k < 4; ++k) CHECK(f[i] != f[k]);
            }
        }

    // --- threads on the timeline ---
    std::vector<Step> steps(3);
    const double msList[3] = {1.0, 2.0, 4.0};
    for (std::size_t k = 0; k < 3; ++k) { steps[k].thread = static_cast<unsigned>(k); steps[k].firstRow = k; steps[k].lastRow = k + 1; steps[k].ms = msList[k]; }
    CHECK(near(laneLength(steps, 2), 1.0) && near(laneLength(steps, 1), 0.5) && near(laneLength(steps, 0), 0.35));
    auto r0 = rowProgress(steps, 3, 0.0), r1 = rowProgress(steps, 3, 1.0), rm = rowProgress(steps, 3, 0.3);
    CHECK(r0[0] == 0 && r0[1] == 0 && r0[2] == 0);
    CHECK(r1[0] == 1 && r1[1] == 1 && r1[2] == 1);
    CHECK(rm[0] > rm[1] && rm[1] > rm[2]);  // the shortest thread is furthest along
    CHECK(near(threadPhase(0.25), 0) && near(threadPhase(0.85), 1));

    // --- the scene agrees with the calculations ---
    SceneInput mul{Kind::Multiply, A, B, multiply(A, B, Schedule::OneTaskPerRow)};
    {
        Frame f0 = buildFrame(mul, 0.0), f1 = buildFrame(mul, 1.0);
        CHECK(f0.arrows.size() == 3 && f1.arrows.size() == 3 && f1.boxes.size() == 2);  // arrows + box + unit cube
        for (int j = 0; j < 3; ++j) {
            CHECK(same(f0.arrows[j].to, column(I, j)));                  // starts as the unit cube
            CHECK(same(f1.arrows[j].to, column(mul.result.value, j)));   // ends as the answer
        }
        const Box& end = f1.boxes.back();
        CHECK(near(triple(end.u, end.v, end.w), determinant(mul.result.value).scalar));  // box volume = det
        const Frame mid = buildFrame(mul, 0.25);  // B has fully moved the cube, A has not started
        for (int j = 0; j < 3; ++j) CHECK(same(mid.arrows[j].to, column(B, j)));
    }

    SceneInput add{Kind::Add, A, B, ml::add(A, B)};
    SceneInput sub{Kind::Subtract, A, B, subtract(A, B)};
    for (const SceneInput* in : {&add, &sub}) {
        const Frame f = buildFrame(*in, 1.0);
        CHECK(f.boxes.size() == 1);
        CHECK(same(f.boxes[0].u, column(in->result.value, 0)) && same(f.boxes[0].w, column(in->result.value, 2)));
        int sums = 0;
        for (const Arrow& a : f.arrows)
            if (a.weight > 1.3) { ++sums; CHECK(same(a.to, column(in->result.value, a.column))); }
        CHECK(sums == 3);
    }

    SceneInput tr{Kind::Transpose, A, Matrix(), transpose(A)};
    {
        const Frame f = buildFrame(tr, 1.0);
        for (int j = 0; j < 3; ++j) CHECK(same(f.arrows[j].to, column(tr.result.value, j)));
    }

    SceneInput det{Kind::Determinant, A, Matrix(), determinant(A)};
    {
        const Frame f = buildFrame(det, 1.0);
        const Box& b = f.boxes.back();
        CHECK(near(triple(b.u, b.v, b.w), det.result.scalar) && det.result.scalar < 0);  // volume -1: mirrored
        CHECK(f.caption.find("mirrored") != std::string::npos);
        const Frame s = buildFrame(det, 0.0);
        CHECK(near(triple(s.boxes.back().u, s.boxes.back().v, s.boxes.back().w), 1.0));  // starts as the unit cube
    }

    SceneInput inv{Kind::Inverse, A, Matrix(), inverse(A)};
    {
        const Frame back = buildFrame(inv, 0.8);  // the box has gone back to the unit cube
        for (int j = 0; j < 3; ++j) CHECK(same(back.arrows[j].to, column(I, j)));
        const Frame end = buildFrame(inv, 1.0);
        int bright = 0;
        for (const Arrow& a : end.arrows)
            if (a.weight > 1.4) { ++bright; CHECK(same(a.to, column(inv.result.value, a.column))); }
        CHECK(bright == 3);
        const Frame peak = buildFrame(inv, 0.4);  // halfway: the box is A's box
        for (int j = 0; j < 3; ++j) CHECK(same(peak.arrows[j].to, column(A, j)));
    }

    // Sizing and safety: always finite, always a caption, and non-3x3 input is refused politely.
    for (const SceneInput* in : {&mul, &add, &sub, &tr, &det, &inv}) {
        CHECK(extentOf(*in) >= 1.0);
        for (int i = 0; i <= 40; ++i) {
            const Frame f = buildFrame(*in, i / 40.0);
            CHECK(!f.caption.empty());
            for (const Arrow& a : f.arrows) CHECK(std::isfinite(a.to.x + a.to.y + a.to.z + a.from.x + a.from.y + a.from.z));
        }
    }
    SceneInput bad{Kind::Add, Matrix(2, 2), Matrix(2, 2), Result()};
    CHECK(buildFrame(bad, 0.5).arrows.empty() && !buildFrame(bad, 0.5).caption.empty());

    if (failures == 0) std::cout << "all scene tests passed\n";
    return failures == 0 ? 0 : 1;
}
