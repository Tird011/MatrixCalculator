#include "calculations/ops.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <stdexcept>
#include <utility>

#include "vectors/task_runner.h"

namespace ml {
namespace {

using Clock = std::chrono::steady_clock;

constexpr std::size_t kMaxHowCells = 16;  // keep the worked lines readable

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

// --- number formatting for the worked lines ---
std::string num(double v) {
    if (std::fabs(v) < 1e-9) return "0";
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.6g", v);
    return buf;
}
std::string par(double v) { return v < 0 ? "(" + num(v) + ")" : num(v); }  // negatives get brackets
std::string idx(std::size_t i, std::size_t j) { return "[" + std::to_string(i + 1) + "][" + std::to_string(j + 1) + "]"; }
std::string letter(char c) { return std::string(1, c); }

void moreCells(std::vector<std::string>& how, std::size_t total) {
    if (total > kMaxHowCells) how.push_back("... and " + std::to_string(total - kMaxHowCells) + " more cells");
}

// Wraps an answer that needed one step on the calling thread.
Result single(Kind kind, std::string title, std::string stepText, Matrix value, Clock::time_point t0) {
    Result r;
    r.kind = kind;
    r.title = std::move(title);
    r.value = std::move(value);
    Step st;
    st.text = std::move(stepText);
    r.steps.push_back(std::move(st));
    r.ms = msSince(t0);
    return r;
}

Result elementwise(const Matrix& a, const Matrix& b, double sign) {
    if (a.rows() != b.rows() || a.cols() != b.cols())
        throw std::invalid_argument("A and B must have the same size");
    const auto t0 = Clock::now();
    Matrix out(a.rows(), a.cols());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(i, j) = a(i, j) + sign * b(i, j);

    const bool plus = sign > 0;
    Result r = single(plus ? Kind::Add : Kind::Subtract, plus ? "A + B" : "A - B",
                      plus ? "each cell of A plus the same cell of B" : "each cell of A minus the same cell of B",
                      std::move(out), t0);
    r.why = plus ? "Adding matrices adds their columns as arrows: put the tail of the B arrow on the tip of "
                   "the A arrow, and the sum is the arrow from the origin to the new tip. Each cell needs only "
                   "its own pair of numbers, so no cell depends on another."
                 : "Subtracting matrices subtracts their columns as arrows: flip the B arrow, put its tail on "
                   "the tip of the A arrow, and the difference is the arrow from the origin to the new tip. "
                   "Each cell needs only its own pair of numbers.";
    std::size_t shown = 0;
    for (std::size_t i = 0; i < a.rows() && shown < kMaxHowCells; ++i)
        for (std::size_t j = 0; j < a.cols() && shown < kMaxHowCells; ++j, ++shown)
            r.how.push_back("C" + idx(i, j) + " = " + num(a(i, j)) + (plus ? " + " : " - ") + par(b(i, j)) +
                            " = " + num(r.value(i, j)));
    moreCells(r.how, a.rows() * a.cols());
    return r;
}

void requireSquare(const Matrix& a, const char* what) {
    if (a.rows() != a.cols() || a.rows() == 0)
        throw std::invalid_argument(std::string(what) + " needs a square matrix");
}

unsigned tasksFor(Schedule schedule, std::size_t rows) {
    // ponytail: one thread per row is a demo mode, capped at 64; AllCores is the mode for big matrices.
    return schedule == Schedule::OneTaskPerRow ? static_cast<unsigned>(std::min<std::size_t>(rows, 64))
                                               : hardwareThreads();
}

// out must be zero-filled. Each task owns a range of result rows, so no two threads touch the same memory.
std::vector<Step> multiplyInto(const Matrix& a, const Matrix& b, Matrix& out, unsigned tasks) {
    return runRows(a.rows(), tasks, [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i)
            for (std::size_t k = 0; k < a.cols(); ++k) {  // i-k-j order is cache-friendly
                const double aik = a(i, k);
                for (std::size_t j = 0; j < b.cols(); ++j) out(i, j) += aik * b(k, j);
            }
    });
}

unsigned threadOfRow(const std::vector<Step>& steps, std::size_t row) {
    for (const Step& s : steps)
        if (row >= s.firstRow && row < s.lastRow) return s.thread;
    return 0;
}

// Gaussian elimination with partial pivoting; records what it did so it can be explained.
struct Elimination {
    double det = 0.0;
    std::vector<double> pivots;
    int swaps = 0;
    bool singular = false;
};

Elimination eliminate(const Matrix& a) {
    const std::size_t n = a.rows();
    const double eps = tolerance(a);
    Matrix m = a;
    Elimination e;
    e.det = 1.0;
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::fabs(m(r, c)) > std::fabs(m(pivot, c))) pivot = r;
        if (std::fabs(m(pivot, c)) <= eps) { e.singular = true; e.det = 0.0; return e; }
        if (pivot != c) {
            for (std::size_t j = 0; j < n; ++j) std::swap(m(c, j), m(pivot, j));
            e.det = -e.det;  // a row swap flips the sign
            ++e.swaps;
        }
        e.pivots.push_back(m(c, c));
        e.det *= m(c, c);
        for (std::size_t r = c + 1; r < n; ++r) {
            const double f = m(r, c) / m(c, c);
            for (std::size_t j = c; j < n; ++j) m(r, j) -= f * m(c, j);
        }
    }
    return e;
}

volatile double g_sink = 0.0;  // keeps the benchmark loop from being optimised away

}  // namespace

Result add(const Matrix& a, const Matrix& b) { return elementwise(a, b, 1.0); }
Result subtract(const Matrix& a, const Matrix& b) { return elementwise(a, b, -1.0); }

Result transpose(const Matrix& a, char name) {
    const auto t0 = Clock::now();
    Matrix out(a.cols(), a.rows());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(j, i) = a(i, j);

    Result r = single(Kind::Transpose, "transpose(" + letter(name) + ")", "rows become columns", std::move(out), t0);
    r.why = "Transposing mirrors the matrix across its diagonal: row i becomes column i. The numbers stay the "
            "same, only their positions swap, so the determinant does not change.";
    for (std::size_t i = 0; i < a.rows() && i < 8; ++i) {
        std::string row = "(";
        for (std::size_t j = 0; j < a.cols(); ++j) row += (j ? ", " : "") + num(a(i, j));
        r.how.push_back("row " + std::to_string(i + 1) + " of " + letter(name) + " " + row + ") becomes column " +
                        std::to_string(i + 1));
    }
    return r;
}

Result multiply(const Matrix& a, const Matrix& b, Schedule schedule) {
    if (a.cols() != b.rows()) throw std::invalid_argument("A x B needs columns of A == rows of B");
    const auto t0 = Clock::now();
    Matrix out(a.rows(), b.cols());
    std::vector<Step> steps = multiplyInto(a, b, out, tasksFor(schedule, a.rows()));

    Result r;
    r.kind = Kind::Multiply;
    r.title = "A x B";
    r.value = std::move(out);
    r.schedule = schedule;
    r.tasks = static_cast<unsigned>(steps.size());
    r.steps = std::move(steps);
    r.why = "A x B means: move space with B first, then with A. Cell (i,j) of the answer is row i of A times "
            "column j of B, added up. A result row never needs another result row, so each row can be handed "
            "to its own thread with no locking. In the 3D view, row i of the answer is coordinate i of every "
            "arrow tip.";
    std::size_t shown = 0;
    for (std::size_t i = 0; i < a.rows() && shown < kMaxHowCells; ++i)
        for (std::size_t j = 0; j < b.cols() && shown < kMaxHowCells; ++j, ++shown) {
            std::string line = "C" + idx(i, j) + " = ";
            for (std::size_t k = 0; k < a.cols(); ++k)
                line += (k ? " + " : "") + par(a(i, k)) + "*" + par(b(k, j));
            line += " = " + num(r.value(i, j)) + "   [thread " + std::to_string(threadOfRow(r.steps, i)) + "]";
            r.how.push_back(std::move(line));
        }
    moreCells(r.how, a.rows() * b.cols());
    r.ms = msSince(t0);
    return r;
}

Result determinant(const Matrix& a, char name) {
    requireSquare(a, "determinant");
    const auto t0 = Clock::now();
    const std::size_t n = a.rows();
    const Elimination e = eliminate(a);

    Result r = single(Kind::Determinant, "det(" + letter(name) + ")", "Gaussian elimination with partial pivoting",
                      Matrix(), t0);
    r.scalar = e.det;

    r.why = "The determinant is the signed volume of the box spanned by the columns (the unit cube has volume 1). ";
    if (std::fabs(e.det) < 1e-9)
        r.why += "Here det = 0: the box is completely flat, so the matrix squashes space onto a plane or a line "
                 "and cannot be undone (no inverse).";
    else
        r.why += "Here det = " + num(e.det) + ": the box has " + num(std::fabs(e.det)) +
                 " times the volume of the unit cube" + (e.det < 0 ? " and is mirrored (its orientation flipped)." : ".");

    if (n == 2) {
        r.how.push_back("a*d - b*c = " + par(a(0, 0)) + "*" + par(a(1, 1)) + " - " + par(a(0, 1)) + "*" +
                        par(a(1, 0)) + " = " + num(a(0, 0) * a(1, 1) - a(0, 1) * a(1, 0)));
    } else if (n == 3) {
        auto crossTerm = [&](std::size_t r1, std::size_t c1, std::size_t r2, std::size_t c2, std::size_t r3,
                         std::size_t c3, std::size_t r4, std::size_t c4) {
            return "(" + par(a(r1, c1)) + "*" + par(a(r2, c2)) + " - " + par(a(r3, c3)) + "*" + par(a(r4, c4)) + ")";
        };
        const double cof = a(0, 0) * (a(1, 1) * a(2, 2) - a(1, 2) * a(2, 1)) -
                           a(0, 1) * (a(1, 0) * a(2, 2) - a(1, 2) * a(2, 0)) +
                           a(0, 2) * (a(1, 0) * a(2, 1) - a(1, 1) * a(2, 0));
        r.how.push_back("expand along row 1: a(ei - fh) - b(di - fg) + c(dh - eg)");
        r.how.push_back("= " + par(a(0, 0)) + "*" + crossTerm(1, 1, 2, 2, 1, 2, 2, 1) + " - " + par(a(0, 1)) + "*" +
                        crossTerm(1, 0, 2, 2, 1, 2, 2, 0) + " + " + par(a(0, 2)) + "*" + crossTerm(1, 0, 2, 1, 1, 1, 2, 0));
        r.how.push_back("= " + num(cof));
    }

    if (e.singular) {
        r.how.push_back("elimination: a pivot became 0, so the columns depend on each other, det = 0");
    } else {
        std::string line = "elimination pivots: ";
        for (std::size_t i = 0; i < e.pivots.size(); ++i) line += (i ? ", " : "") + num(e.pivots[i]);
        line += " (row swaps: " + std::to_string(e.swaps) + ") -> det = product";
        if (e.swaps % 2) line += " x -1";
        line += " = " + num(e.det);
        r.how.push_back(std::move(line));
    }
    return r;
}

Result inverse(const Matrix& a, char name) {
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

    // Proof: A x inverse(A) should be the identity. Report how close it got.
    double worst = 0.0;
    for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j) {
            double sum = 0.0;
            for (std::size_t k = 0; k < n; ++k) sum += a(i, k) * inv(k, j);
            worst = std::max(worst, std::fabs(sum - (i == j ? 1.0 : 0.0)));
        }

    const std::string L = letter(name);
    Result r = single(Kind::Inverse, "inverse(" + L + ")", "Gauss-Jordan on [A | I] with partial pivoting",
                      std::move(inv), t0);
    r.why = "The inverse undoes the matrix: " + L + " x inverse(" + L + ") = I, so the box goes back to the unit "
            "cube. It exists only when the determinant is not 0, because a flat box cannot be un-flattened.";
    r.how.push_back("det(" + L + ") = " + num(eliminate(a).det) + " (not 0, so the inverse exists)");
    r.how.push_back("Gauss-Jordan on [" + L + " | I]: scale each pivot row to 1 and clear the rest of its column.");
    r.how.push_back("When the left half is I, the right half is inverse(" + L + ").");
    char buf[80];
    std::snprintf(buf, sizeof buf, "check: largest |%s x inverse - I| = %.2e", L.c_str(), worst);
    r.how.push_back(buf);
    return r;
}

Comparison compareMultiply(const Matrix& a, const Matrix& b, Schedule schedule) {
    if (a.cols() != b.rows()) throw std::invalid_argument("A x B needs columns of A == rows of B");
    const double work = static_cast<double>(a.rows()) * static_cast<double>(b.cols()) * static_cast<double>(a.cols());
    const unsigned reps = std::clamp(static_cast<unsigned>(2e7 / std::max(1.0, work)), 1u, 300u);
    const unsigned tasks = tasksFor(schedule, a.rows());

    auto timeIt = [&](unsigned t) {
        const auto t0 = Clock::now();
        for (unsigned i = 0; i < reps; ++i) {
            Matrix out(a.rows(), b.cols());
            multiplyInto(a, b, out, t);
            g_sink = out(0, 0);
        }
        return msSince(t0) / reps;
    };

    Comparison c;
    c.reps = reps;
    c.seqMs = timeIt(1);
    c.parMs = timeIt(tasks);
    return c;
}

}  // namespace ml