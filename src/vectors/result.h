#pragma once
// The shared language between calculations and graphics.
// calculations fills a Result; the panel prints it and (later) the 3D scene replays its steps.
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#include "vectors/matrix.h"

namespace ml {

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

struct Result {
    std::string title;
    Matrix value;              // 0x0 when the answer is a single number (see scalar)
    double scalar = 0.0;
    std::vector<Step> steps;
    double ms = 0.0;
    Schedule schedule = Schedule::AllCores;
    unsigned tasks = 1;        // how many threads shared the work

    bool isScalar() const { return value.rows() == 0; }
};

// Text for the Output Details box: the answer, the steps, the total time.
inline std::string describe(const Result& r) {
    char buf[64];
    auto cell = [&buf](double v) {
        if (std::fabs(v) < 0.0005) v = 0.0;  // avoid printing "-0.000"
        std::snprintf(buf, sizeof buf, "%10.3f", v);
        return std::string(buf);
    };

    std::string s = r.title + "\n\n";
    if (r.isScalar()) {
        s += "  " + cell(r.scalar) + "\n";
    } else {
        for (std::size_t i = 0; i < r.value.rows(); ++i) {
            s += "  ";
            for (std::size_t j = 0; j < r.value.cols(); ++j) s += cell(r.value(i, j));
            s += "\n";
        }
    }

    s += "\nSteps:\n";
    for (const Step& st : r.steps) s += "  " + st.text + "\n";

    std::snprintf(buf, sizeof buf, "\nTotal: %.3f ms", r.ms);
    return s + buf + "\n";
}

}  // namespace ml