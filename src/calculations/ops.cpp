#include "calculations/ops.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "vectors/task_runner.h"

namespace ml {
namespace {

using Clock = std::chrono::steady_clock;

double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

double maxAbs(const Matrix& m) {
    double best = 0.0;
    for (std::size_t i = 0; i < m.rows(); ++i)
        for (std::size_t j = 0; j < m.cols(); ++j) best = std::max(best, std::fabs(m(i, j)));
    return best;
}

// "Is this pivot zero?" threshold, scaled to the size of the numbers in the matrix.
double tolerance(const Matrix& m) { return 1e-12 * std::max(1.0, maxAbs(m)); }

// Wraps an answer that needed one step on the calling thread.
Result single(std::string title, std::string text, Matrix value, Clock::time_point t0) {
    Result r;
    r.title = std::move(title);
    r.value = std::move(value);
    Step st;
    st.text = std::move(text);
    r.steps.push_back(std::move(st));
    r.ms = msSince(t0);
    return r;
}

Result elementwise(const Matrix& a, const Matrix& b, double sign, const char* title, const char* text) {
    if (a.rows() != b.rows() || a.cols() != b.cols())
        throw std::invalid_argument("A and B must have the same size");
    const auto t0 = Clock::now();
    Matrix out(a.rows(), a.cols());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(i, j) = a(i, j) + sign * b(i, j);
    return single(title, text, std::move(out), t0);
}

void requireSquare(const Matrix& a, const char* what) {
    if (a.rows() != a.cols() || a.rows() == 0)
        throw std::invalid_argument(std::string(what) + " needs a square matrix");
}

}  // namespace

Result add(const Matrix& a, const Matrix& b) {
    return elementwise(a, b, 1.0, "A + B", "each cell of A plus the same cell of B");
}

Result subtract(const Matrix& a, const Matrix& b) {
    return elementwise(a, b, -1.0, "A - B", "each cell of A minus the same cell of B");
}

Result transpose(const Matrix& a) {
    const auto t0 = Clock::now();
    Matrix out(a.cols(), a.rows());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(j, i) = a(i, j);
    return single("transpose", "rows become columns", std::move(out), t0);
}

Result multiply(const Matrix& a, const Matrix& b, Schedule schedule) {
    if (a.cols() != b.rows()) throw std::invalid_argument("A x B needs columns of A == rows of B");
    const auto t0 = Clock::now();
    Matrix out(a.rows(), b.cols());

    // ponytail: one thread per row is a demo mode, capped at 64; AllCores is the mode for big matrices.
    const unsigned tasks = schedule == Schedule::OneTaskPerRow
                               ? static_cast<unsigned>(std::min<std::size_t>(a.rows(), 64))
                               : hardwareThreads();

    // Each task owns a range of result rows, so no two threads touch the same memory.
    auto steps = runRows(a.rows(), tasks, [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i)
            for (std::size_t k = 0; k < a.cols(); ++k) {  // i-k-j order is cache-friendly
                const double aik = a(i, k);
                for (std::size_t j = 0; j < b.cols(); ++j) out(i, j) += aik * b(k, j);
            }
    });

    Result r;
    r.title = "A x B";
    r.value = std::move(out);
    r.schedule = schedule;
    r.tasks = static_cast<unsigned>(steps.size());
    r.steps = std::move(steps);
    r.ms = msSince(t0);
    return r;
}

Result determinant(const Matrix& a) {
    requireSquare(a, "determinant");
    const auto t0 = Clock::now();
    const std::size_t n = a.rows();
    const double eps = tolerance(a);
    Matrix m = a;
    double det = 1.0;

    for (std::size_t c = 0; c < n && det != 0.0; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::fabs(m(r, c)) > std::fabs(m(pivot, c))) pivot = r;
        if (std::fabs(m(pivot, c)) <= eps) { det = 0.0; break; }
        if (pivot != c) {
            for (std::size_t j = 0; j < n; ++j) std::swap(m(c, j), m(pivot, j));
            det = -det;  // a row swap flips the sign
        }
        det *= m(c, c);
        for (std::size_t r = c + 1; r < n; ++r) {
            const double f = m(r, c) / m(c, c);
            for (std::size_t j = c; j < n; ++j) m(r, j) -= f * m(c, j);
        }
    }

    Result r = single("determinant", "Gaussian elimination with partial pivoting", Matrix(), t0);
    r.scalar = det;
    return r;
}

Result inverse(const Matrix& a) {
    requireSquare(a, "inverse");
    const auto t0 = Clock::now();
    const std::size_t n = a.rows();
    const double eps = tolerance(a);
    Matrix m = a;
    Matrix inv(n, n);
    for (std::size_t i = 0; i < n; ++i) inv(i, i) = 1.0;

    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::fabs(m(r, c)) > std::fabs(m(pivot, c))) pivot = r;
        if (std::fabs(m(pivot, c)) <= eps)
            throw std::domain_error("matrix is singular (determinant is 0), so it has no inverse");
        if (pivot != c)
            for (std::size_t j = 0; j < n; ++j) {
                std::swap(m(c, j), m(pivot, j));
                std::swap(inv(c, j), inv(pivot, j));
            }
        const double p = m(c, c);
        for (std::size_t j = 0; j < n; ++j) { m(c, j) /= p; inv(c, j) /= p; }
        for (std::size_t r = 0; r < n; ++r) {
            const double f = m(r, c);
            if (r == c || f == 0.0) continue;
            for (std::size_t j = 0; j < n; ++j) { m(r, j) -= f * m(c, j); inv(r, j) -= f * inv(c, j); }
        }
    }
    return single("inverse", "Gauss-Jordan on [A | I] with partial pivoting", std::move(inv), t0);
}

}  // namespace ml