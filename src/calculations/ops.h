#pragma once
// The shared language between calculations and graphics.
// calculations fills a Result; the panel prints it and the 3D scene replays it.
#include <cmath>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

#include "vectors/matrix.h"

namespace ml {

enum class Kind { Add, Subtract, Multiply, Transpose, Determinant, Inverse };

enum class Schedule {
    OneTaskPerRow,  // demo: every row of the answer gets its own thread, so you can see them
    AllCores,       // performance: rows are split into one chunk per hardware thread
};

// One unit of work: which thread did which rows, and how long it took.
struct Step {
    unsigned thread = 0;
    std::size_t firstRow = 0;  // 0-based, first row handled
    std::size_t lastRow = 0;   // 0-based, one past the last row handled
    double ms = 0.0;
    std::string text;          // human-readable line for Output Details
};

// Same multiplication, timed on one thread and on the chosen schedule (averaged over `reps` runs).
struct Comparison {
    double seqMs = 0.0;
    double parMs = 0.0;
    unsigned reps = 1;
    double speedup() const { return parMs > 0.0 ? seqMs / parMs : 0.0; }
};

struct Result {
    Kind kind = Kind::Add;
    std::string title;
    Matrix value;                    // 0x0 when the answer is a single number (see scalar)
    double scalar = 0.0;
    std::string why;                 // plain-language reason this operation does what it does
    std::vector<std::string> how;    // worked lines using the real numbers
    std::vector<Step> steps;         // one per thread (or one line for single-thread operations)
    double ms = 0.0;
    Schedule schedule = Schedule::AllCores;
    unsigned tasks = 1;              // how many threads shared the work
    std::optional<Comparison> comparison;

    bool isScalar() const { return value.rows() == 0; }
};

// Text for the Output Details box. Lines starting with "# " are section headings.
inline std::string describe(const Result& r) {
    char buf[160];
    auto cell = [&buf](double v) {
        if (std::fabs(v) < 0.0005) v = 0.0;  // avoid printing "-0.000"
        std::snprintf(buf, sizeof buf, "%10.3f", v);
        return std::string(buf);
    };

    std::string s = "# " + r.title + "\n";
    if (r.isScalar()) {
        s += "  " + cell(r.scalar) + "\n";
    } else {
        for (std::size_t i = 0; i < r.value.rows(); ++i) {
            s += "  ";
            for (std::size_t j = 0; j < r.value.cols(); ++j) s += cell(r.value(i, j));
            s += "\n";
        }
    }

    if (!r.why.empty()) s += "\n# WHY\n" + r.why + "\n";

    if (!r.how.empty()) {
        s += "\n# HOW\n";
        for (const std::string& line : r.how) s += "  " + line + "\n";
    }

    if (r.tasks > 1) {
        s += "\n# THREADS\n";
        for (const Step& st : r.steps) s += "  " + st.text + "\n";
    }

    if (r.comparison) {
        const Comparison& c = *r.comparison;
        std::snprintf(buf, sizeof buf, "\n# COMPARISON (average of %u runs)\n", c.reps);
        s += buf;
        const std::string parLabel = std::to_string(r.tasks) + " threads " +
                                     (r.schedule == Schedule::OneTaskPerRow ? "(1 per row)" : "(all cores)");
        std::snprintf(buf, sizeof buf, "  %-22s: %.4f ms\n", "1 thread", c.seqMs);
        s += buf;
        std::snprintf(buf, sizeof buf, "  %-22s: %.4f ms\n", parLabel.c_str(), c.parMs);
        s += buf;
        if (c.speedup() >= 1.0)
            std::snprintf(buf, sizeof buf, "  %-22s: %.2fx faster with threads\n", "speedup", c.speedup());
        else
            std::snprintf(buf, sizeof buf, "  %-22s: %.2fx (threads are SLOWER here)\n", "speedup", c.speedup());
        s += buf;
        s += c.speedup() >= 1.0
                 ? "  Why: the work was big enough to pay for starting the threads.\n"
                 : "  Why: starting and joining a thread costs far more than this tiny amount of math. "
                   "Threads only win on big matrices.\n";
    } else {
        std::snprintf(buf, sizeof buf, "\nTook %.3f ms on %u thread%s.\n", r.ms, r.tasks, r.tasks == 1 ? "" : "s");
        s += buf;
    }
    return s;
}

}  // namespace ml