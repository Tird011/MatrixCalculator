// Plain checks, no framework. Run with: ctest --test-dir build   (or run test_ops directly)
#include <cmath>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

#include "calculations/ops.h"
#include "vectors/task_runner.h"

using namespace ml;

static int failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ++failures;                                                      \
            std::cerr << "FAIL line " << __LINE__ << ": " #cond "\n";        \
        }                                                                    \
    } while (0)

static Matrix make(std::size_t rows, std::size_t cols, std::initializer_list<double> v) {
    Matrix m(rows, cols);
    std::size_t i = 0;
    for (double x : v) { m(i / cols, i % cols) = x; ++i; }
    return m;
}

static bool near(double a, double b) { return std::fabs(a - b) < 1e-9; }

template <class F>
static bool throws(F f) {
    try { f(); } catch (const std::exception&) { return true; }
    return false;
}

int main() {
    const Matrix a = make(2, 2, {1, 2, 3, 4});
    const Matrix b = make(2, 2, {5, 6, 7, 8});

    Result r = add(a, b);
    CHECK(r.value(0, 0) == 6 && r.value(0, 1) == 8 && r.value(1, 0) == 10 && r.value(1, 1) == 12);
    r = subtract(b, a);
    CHECK(r.value(0, 0) == 4 && r.value(0, 1) == 4 && r.value(1, 0) == 4 && r.value(1, 1) == 4);

    r = transpose(make(2, 3, {1, 2, 3, 4, 5, 6}));
    CHECK(r.value.rows() == 3 && r.value.cols() == 2 && r.value(2, 0) == 3 && r.value(0, 1) == 4);

    r = multiply(a, b, Schedule::AllCores);
    CHECK(r.value(0, 0) == 19 && r.value(0, 1) == 22 && r.value(1, 0) == 43 && r.value(1, 1) == 50);

    // Threads must not change the answer: both schedules match a plain triple loop exactly.
    Matrix big1(40, 40), big2(40, 40), ref(40, 40);
    for (std::size_t i = 0; i < 40; ++i)
        for (std::size_t j = 0; j < 40; ++j) {
            big1(i, j) = static_cast<double>((i * 7 + j * 3) % 11) - 5;
            big2(i, j) = static_cast<double>((i * 5 + j * 2) % 13) - 6;
        }
    for (std::size_t i = 0; i < 40; ++i)
        for (std::size_t j = 0; j < 40; ++j)
            for (std::size_t k = 0; k < 40; ++k) ref(i, j) += big1(i, k) * big2(k, j);
    const Result perRow = multiply(big1, big2, Schedule::OneTaskPerRow);
    const Result cores = multiply(big1, big2, Schedule::AllCores);
    bool same = true;
    for (std::size_t i = 0; i < 40; ++i)
        for (std::size_t j = 0; j < 40; ++j)
            same = same && perRow.value(i, j) == ref(i, j) && cores.value(i, j) == ref(i, j);
    CHECK(same);
    CHECK(perRow.tasks == 40);
    CHECK(cores.tasks >= 1 && cores.tasks <= hardwareThreads());
    std::size_t rowsCovered = 0;  // the steps together must cover every row exactly once
    for (const Step& s : cores.steps) rowsCovered += s.lastRow - s.firstRow;
    CHECK(rowsCovered == 40);

    CHECK(near(determinant(make(2, 2, {4, 3, 6, 3})).scalar, -6));
    CHECK(near(determinant(make(3, 3, {6, 1, 1, 4, -2, 5, 2, 8, 7})).scalar, -306));
    CHECK(near(determinant(make(3, 3, {2, 0, 1, 1, 3, 2, 1, 1, 1})).scalar, 0));  // singular
    CHECK(near(determinant(make(1, 1, {5})).scalar, 5));
    CHECK(determinant(a).isScalar());

    r = inverse(make(2, 2, {4, 7, 2, 6}));
    CHECK(near(r.value(0, 0), 0.6) && near(r.value(0, 1), -0.7));
    CHECK(near(r.value(1, 0), -0.2) && near(r.value(1, 1), 0.4));

    const Matrix m3 = make(3, 3, {2, 1, 1, 1, 3, 2, 1, 0, 0});  // determinant is -1
    const Result prod = multiply(m3, inverse(m3).value, Schedule::AllCores);
    bool identity = true;
    for (std::size_t i = 0; i < 3; ++i)
        for (std::size_t j = 0; j < 3; ++j) identity = identity && near(prod.value(i, j), i == j ? 1.0 : 0.0);
    CHECK(identity);

    CHECK(throws([&] { add(a, make(3, 3, {1, 2, 3, 4, 5, 6, 7, 8, 9})); }));
    CHECK(throws([&] { multiply(make(2, 3, {1, 2, 3, 4, 5, 6}), a, Schedule::AllCores); }));
    CHECK(throws([&] { determinant(make(2, 3, {1, 2, 3, 4, 5, 6})); }));
    CHECK(throws([&] { inverse(make(2, 2, {1, 2, 2, 4})); }));  // singular

    if (failures == 0) std::cout << "all tests passed\n";
    return failures == 0 ? 0 : 1;
}