#pragma once
// Pure geometry for the 3D scene: what to draw at animation time T (0..1).
// No raylib in here, so it can be unit-tested without a window.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "vectors/result.h"

namespace ml {

// ---------- small vector maths (x, y, z with z pointing up, like the mockup) ----------
struct V3 {
    double x = 0, y = 0, z = 0;
};
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, double k) { return {a.x * k, a.y * k, a.z * k}; }
inline double dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline double triple(V3 u, V3 v, V3 w) { return dot(u, cross(v, w)); }  // signed volume of the box u, v, w

inline V3 column(const Matrix& m, std::size_t j) { return {m(0, j), m(1, j), m(2, j)}; }

inline double clamp01(double t) { return std::min(1.0, std::max(0.0, t)); }
inline double ease(double t) { t = clamp01(t); return t * t * (3.0 - 2.0 * t); }
inline double progress(double t, double from, double to) { return clamp01((t - from) / (to - from)); }

inline Matrix identity3() {
    Matrix m(3, 3);
    for (std::size_t i = 0; i < 3; ++i) m(i, i) = 1.0;
    return m;
}

// Every cell moves from a to b by t.
inline Matrix mix(const Matrix& a, const Matrix& b, double t) {
    Matrix out(a.rows(), a.cols());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(i, j) = a(i, j) + (b(i, j) - a(i, j)) * t;
    return out;
}

// Row i moves from a to b by rowT[i]: lets each thread's row finish at its own moment.
inline Matrix mixRows(const Matrix& a, const Matrix& b, const std::vector<double>& rowT) {
    Matrix out(a.rows(), a.cols());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(i, j) = a(i, j) + (b(i, j) - a(i, j)) * rowT[i];
    return out;
}

// ---------- threads on the animation timeline ----------
// Every thread starts together at phase 0. The slowest measured thread fills its lane at phase 1;
// faster ones finish earlier but never below 35%, so every lane stays visible.
// (Animation speed is not real time: the real work takes microseconds.)
inline double laneLength(const std::vector<Step>& steps, std::size_t k) {
    double maxMs = 0.0;
    for (const Step& s : steps) maxMs = std::max(maxMs, s.ms);
    return maxMs > 0.0 ? std::max(0.35, steps[k].ms / maxMs) : 1.0;
}

inline double laneFill(const std::vector<Step>& steps, std::size_t k, double phase) {
    return ease(phase / laneLength(steps, k));
}

// How far each row of the answer has got at `phase` (0..1).
inline std::vector<double> rowProgress(const std::vector<Step>& steps, std::size_t rows, double phase) {
    std::vector<double> out(rows, 1.0);
    for (std::size_t k = 0; k < steps.size(); ++k)
        for (std::size_t r = steps[k].firstRow; r < steps[k].lastRow && r < rows; ++r)
            out[r] = laneFill(steps, k, phase);
    return out;
}

// In the multiply animation the threads run during T in [0.25, 0.85].
inline double threadPhase(double T) { return progress(T, 0.25, 0.85); }

// ---------- the box spanned by three arrows ----------
inline std::array<V3, 8> corners(V3 u, V3 v, V3 w) {
    std::array<V3, 8> c;
    for (int i = 0; i < 8; ++i) c[i] = u * ((i & 1) ? 1.0 : 0.0) + v * ((i & 2) ? 1.0 : 0.0) + w * ((i & 4) ? 1.0 : 0.0);
    return c;
}

// Corner indices of one face, in loop order: the face where bit `axis` of the corner index equals `side`.
inline std::array<int, 4> faceCorners(int axis, int side) {
    const int a = (axis + 1) % 3, b = (axis + 2) % 3;
    const int base = side << axis;
    return {base, base | (1 << a), base | (1 << a) | (1 << b), base | (1 << b)};
}

// ---------- what to draw ----------
struct SceneInput {
    Kind kind = Kind::Add;
    Matrix a;  // first operand (the only operand for transpose, determinant, inverse)
    Matrix b;  // second operand (add, subtract, multiply)
    Result result;
};

struct Arrow {
    V3 from, to;
    int column = 0;       // 0..2 picks the colour: columns of a matrix keep their colour while they move
    double weight = 1.0;  // thickness multiplier
    bool light = false;   // lighter tint (used for the second operand)
};

struct Box {
    V3 u, v, w;
    int style = 0;        // 0 = the shape (coloured by orientation), 1 = unit-cube reference
    double alpha = 1.0;
};

struct Frame {
    std::vector<Arrow> arrows;
    std::vector<Box> boxes;
    std::string caption;
};

inline bool is3x3(const Matrix& m) { return m.rows() == 3 && m.cols() == 3; }

// Largest coordinate the scene can reach, so the camera and axes can be sized to fit.
inline double extentOf(const SceneInput& in) {
    double e = 1.0;  // the unit cube
    for (const Matrix* m : {&in.a, &in.b, &in.result.value}) {
        if (!is3x3(*m)) continue;
        for (std::size_t r = 0; r < 3; ++r) {  // a corner of the box can reach the sum of |entries| in a row
            double sum = 0.0;
            for (std::size_t c = 0; c < 3; ++c) sum += std::fabs((*m)(r, c));
            e = std::max(e, sum);
        }
    }
    return e;
}

namespace detail {

inline std::string fmt(const char* format, double v) {
    char buf[96];
    std::snprintf(buf, sizeof buf, format, v);
    return buf;
}

inline void addColumns(Frame& f, const Matrix& m, double weight = 1.0) {
    for (std::size_t j = 0; j < 3; ++j) f.arrows.push_back({V3{}, column(m, j), static_cast<int>(j), weight, false});
}
inline void addBox(Frame& f, const Matrix& m, double alpha = 1.0) {
    f.boxes.push_back({column(m, 0), column(m, 1), column(m, 2), 0, alpha});
}
inline void addUnitCube(Frame& f) { f.boxes.push_back({V3{1, 0, 0}, V3{0, 1, 0}, V3{0, 0, 1}, 1, 1.0}); }

}  // namespace detail

inline Frame buildFrame(const SceneInput& in, double T) {
    using namespace detail;
    Frame f;
    T = clamp01(T);

    const bool binary = in.kind == Kind::Add || in.kind == Kind::Subtract || in.kind == Kind::Multiply;
    const bool hasMatrixResult = in.kind != Kind::Determinant;
    if (!is3x3(in.a) || (binary && !is3x3(in.b)) || (hasMatrixResult && !is3x3(in.result.value))) {
        f.caption = "The 3D view needs 3x3 matrices";
        return f;
    }

    const Matrix I = identity3();

    switch (in.kind) {
        case Kind::Add:
        case Kind::Subtract: {
            const double sign = in.kind == Kind::Add ? 1.0 : -1.0;
            const double width = 0.28, start = 0.05;
            int active = 2;
            for (int j = 0; j < 3; ++j) {
                const double w0 = start + j * width;
                const double p = progress(T, w0, w0 + width);
                const V3 aj = column(in.a, j), bj = column(in.b, j) * sign, rj = aj + bj;
                const double slide = ease(progress(p, 0.0, 0.5)), grow = ease(progress(p, 0.5, 1.0));
                f.arrows.push_back({V3{}, aj, j, 1.0, false});                     // column of A
                f.arrows.push_back({aj * slide, aj * slide + bj, j, 0.8, true});   // column of B, sliding to A's tip
                if (grow > 0.0) f.arrows.push_back({V3{}, rj * grow, j, 1.4, false});  // the sum
                if (T < w0 + width && active == 2 && j < 2) active = j;
            }
            const double done = ease(progress(T, 0.89, 1.0));
            if (done > 0.0) addBox(f, in.result.value, done);
            if (T < start)
                f.caption = "Solid arrows: columns of A. Light arrows: columns of B";
            else if (T >= 0.89)
                f.caption = std::string("Result: the box spanned by the three ") + (sign > 0 ? "sums" : "differences");
            else
                f.caption = "Column " + std::to_string(active + 1) + ": slide the " + (sign > 0 ? "" : "flipped ") +
                            "B arrow to the tip of A. The " + (sign > 0 ? "sum" : "difference") + " starts at the origin";
            break;
        }

        case Kind::Multiply: {
            addUnitCube(f);
            Matrix M;
            if (T < 0.25) {
                M = mix(I, in.b, ease(progress(T, 0.0, 0.25)));
                f.caption = "1. B moves the unit cube: its columns are where x, y, z land";
            } else {
                std::vector<double> rows = rowProgress(in.result.steps, 3, threadPhase(T));
                M = mixRows(in.b, in.result.value, rows);
                f.caption = T < 0.85 ? "2. A moves it again. Thread k finishes row k = coordinate k of every tip"
                                     : "3. Result A x B: the box after both moves";
            }
            addColumns(f, M, 1.2);
            addBox(f, M);
            break;
        }

        case Kind::Transpose: {
            addUnitCube(f);
            const Matrix M = mix(in.a, in.result.value, ease(progress(T, 0.1, 0.85)));
            addColumns(f, M, 1.2);
            addBox(f, M);
            f.caption = T < 0.1 ? "A: its columns span this box"
                        : T < 0.85 ? "Rows become columns: the numbers swap across the diagonal"
                                   : "The transpose: each row of A is now a column";
            break;
        }

        case Kind::Determinant: {
            addUnitCube(f);
            const Matrix M = mix(I, in.a, ease(progress(T, 0.1, 0.8)));
            addColumns(f, M, 1.2);
            addBox(f, M);
            const double vol = triple(column(M, 0), column(M, 1), column(M, 2));
            f.caption = T < 0.1 ? "Unit cube: volume 1" : fmt("Volume of the box (unit cube = 1): %.4g", vol);
            if (T >= 0.8) f.caption += vol < -1e-9 ? "  (mirrored)" : vol < 1e-9 ? "  (flat!)" : "";
            break;
        }

        case Kind::Inverse: {
            addUnitCube(f);
            Matrix M = T < 0.4 ? mix(I, in.a, ease(progress(T, 0.0, 0.4)))
                               : mix(in.a, I, ease(progress(T, 0.4, 0.8)));
            addColumns(f, M, 1.0);
            addBox(f, M);
            const double show = ease(progress(T, 0.8, 1.0));
            if (show > 0.0) {  // the answer: columns of inverse(A)
                for (std::size_t j = 0; j < 3; ++j)
                    f.arrows.push_back({V3{}, column(in.result.value, j) * show, static_cast<int>(j), 1.5, true});
                addBox(f, in.result.value, show);
            }
            f.caption = T < 0.4 ? "1. A turns the unit cube into this box"
                        : T < 0.8 ? "2. inverse(A) undoes it: the box goes back to the unit cube"
                                  : "3. Bright arrows and box: inverse(A)";
            break;
        }
    }
    return f;
}

}  // namespace ml
