// Matrix calculator CLI prototype (C++17).
// Build: g++ -std=c++17 -O2 -pthread matrix_calculator.cpp -o matrix_calc //reminder lang
//test prototypev2
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

// ---------- Core: flat row-major matrix (no UI code in here, so a raylib view can reuse it) ----------
class Matrix {
public:
    Matrix(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    double& operator()(std::size_t r, std::size_t c) { return data_[r * cols_ + c]; }
    double operator()(std::size_t r, std::size_t c) const { return data_[r * cols_ + c]; }

private:
    std::size_t rows_, cols_;
    std::vector<double> data_;
};

Matrix add(const Matrix& a, const Matrix& b, double sign = 1.0) {  // sign = -1 gives subtraction
    if (a.rows() != b.rows() || a.cols() != b.cols())
        throw std::invalid_argument("add/subtract needs identical dimensions");
    Matrix out(a.rows(), a.cols());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(i, j) = a(i, j) + sign * b(i, j);
    return out;
}

Matrix transpose(const Matrix& a) {
    Matrix out(a.cols(), a.rows());
    for (std::size_t i = 0; i < a.rows(); ++i)
        for (std::size_t j = 0; j < a.cols(); ++j) out(j, i) = a(i, j);
    return out;
}

// Rows of the result are independent, so threads each take a row range and never touch the same
// memory: no locks needed. threads == 1 is the sequential baseline.
Matrix multiply(const Matrix& a, const Matrix& b, unsigned threads = 1) {
    if (a.cols() != b.rows()) throw std::invalid_argument("multiply needs cols(A) == rows(B)");
    Matrix out(a.rows(), b.cols());

    auto work = [&](std::size_t first, std::size_t last) {
        for (std::size_t i = first; i < last; ++i)
            for (std::size_t k = 0; k < a.cols(); ++k) {  // i-k-j order is cache-friendly
                const double aik = a(i, k);
                for (std::size_t j = 0; j < b.cols(); ++j) out(i, j) += aik * b(k, j);
            }
    };

    threads = std::max(1u, std::min<unsigned>(threads, static_cast<unsigned>(a.rows())));
    if (threads == 1) { work(0, a.rows()); return out; }

    std::vector<std::thread> pool;
    const std::size_t chunk = (a.rows() + threads - 1) / threads;
    for (unsigned t = 0; t < threads; ++t) {
        const std::size_t first = t * chunk, last = std::min(a.rows(), first + chunk);
        if (first < last) pool.emplace_back(work, first, last);
    }
    for (auto& th : pool) th.join();
    return out;
}

// Gaussian elimination with partial pivoting: works for any N, not just 2x2/3x3.
double determinant(Matrix m) {
    if (m.rows() != m.cols()) throw std::invalid_argument("determinant needs a square matrix");
    const std::size_t n = m.rows();
    double det = 1.0;
    for (std::size_t c = 0; c < n; ++c) {
        std::size_t pivot = c;
        for (std::size_t r = c + 1; r < n; ++r)
            if (std::abs(m(r, c)) > std::abs(m(pivot, c))) pivot = r;
        if (std::abs(m(pivot, c)) < 1e-12) return 0.0;
        if (pivot != c) {
            for (std::size_t j = 0; j < n; ++j) std::swap(m(c, j), m(pivot, j));
            det = -det;
        }
        det *= m(c, c);
        for (std::size_t r = c + 1; r < n; ++r) {
            const double f = m(r, c) / m(c, c);
            for (std::size_t j = c; j < n; ++j) m(r, j) -= f * m(c, j);
        }
    }
    return det;
}

// ---------- CLI helpers ----------
template <typename T>
T readValue(const std::string& prompt, T lo = std::numeric_limits<T>::lowest(),
            T hi = std::numeric_limits<T>::max()) {
    for (;;) {
        std::cout << prompt;
        T v{};
        if (std::cin >> v && v >= lo && v <= hi) return v;
        if (std::cin.eof()) throw std::runtime_error("input closed");
        std::cin.clear();
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::cout << "  Invalid input, try again.\n";
    }
}

constexpr int max_dim = 2000;

Matrix readMatrix(const std::string& name) {
    const auto r = readValue<int>("Rows of " + name + ": ", 1, max_dim);
    const auto c = readValue<int>("Cols of " + name + ": ", 1, max_dim);
    Matrix m(r, c);
    std::cout << "Enter " << r << "x" << c << " values for " << name << ", row by row:\n";
    for (int i = 0; i < r; ++i)
        for (int j = 0; j < c; ++j)
            m(i, j) = readValue<double>(name + "[" + std::to_string(i + 1) + "][" + std::to_string(j + 1) + "]: ");
    return m;
}

void print(const Matrix& m) {
    if (m.rows() > 10 || m.cols() > 10) {  // don't flood the terminal
        std::cout << "Result is " << m.rows() << "x" << m.cols() << " (too big to print)\n";
        return;
    }
    std::cout << "\nResult:\n" << std::fixed << std::setprecision(2);
    for (std::size_t i = 0; i < m.rows(); ++i) {
        for (std::size_t j = 0; j < m.cols(); ++j) std::cout << std::setw(10) << m(i, j);
        std::cout << '\n';
    }
    std::cout << '\n';
}

using Clock = std::chrono::steady_clock;
double msSince(Clock::time_point t0) {
    return std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
}

// Same multiply run sequentially and in parallel: this is the demo of parallel computing.
void benchmark() {
    const auto n = readValue<int>("Matrix size N (NxN, e.g. 500): ", 2, max_dim);
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    std::mt19937 rng(42);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    Matrix a(n, n), b(n, n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) { a(i, j) = dist(rng); b(i, j) = dist(rng); }

    auto t0 = Clock::now();
    const Matrix seq = multiply(a, b, 1);
    const double tSeq = msSince(t0);
    t0 = Clock::now();
    const Matrix par = multiply(a, b, hw);
    const double tPar = msSince(t0);

    std::cout << std::fixed << std::setprecision(2)
              << "\nSequential (1 thread):  " << tSeq << " ms\n"
              << "Parallel   (" << hw << " threads): " << tPar << " ms\n"
              << "Speedup: " << tSeq / tPar << "x\n"
              << "Results match: " << (seq(n - 1, n - 1) == par(n - 1, n - 1) ? "yes" : "NO") << "\n\n";
}

void menu() {
    std::cout << "====================================\n"
                 "          MATRIX CALCULATOR\n"
                 "====================================\n"
                 "1. Add          (A + B)\n"
                 "2. Subtract     (A - B)\n"
                 "3. Multiply     (A * B, parallel)\n"
                 "4. Transpose    (A^T)\n"
                 "5. Determinant  (|A|, any NxN)\n"
                 "6. Benchmark    (sequential vs parallel)\n"
                 "7. Exit\n"
                 "====================================\n";
}

int main() {
    try {
        for (;;) {
            menu();
            const auto choice = readValue<int>("Enter choice: ");
            try {
                switch (choice) {
                    case 1: { auto a = readMatrix("A"); auto b = readMatrix("B"); print(add(a, b)); break; }
                    case 2: { auto a = readMatrix("A"); auto b = readMatrix("B"); print(add(a, b, -1.0)); break; }
                    case 3: {
                        auto a = readMatrix("A"); auto b = readMatrix("B");
                        const auto t0 = Clock::now();
                        print(multiply(a, b, std::max(1u, std::thread::hardware_concurrency())));
                        std::cout << "Took " << msSince(t0) << " ms\n\n";
                        break;
                    }
                    case 4: print(transpose(readMatrix("A"))); break;
                    case 5: std::cout << "\n|A| = " << determinant(readMatrix("A")) << "\n\n"; break;
                    case 6: benchmark(); break;
                    case 7: std::cout << "Goodbye!\n"; return 0;
                    default: std::cout << "Choose 1-7.\n\n";
                }
            } catch (const std::invalid_argument& e) {  // dimension mismatch etc: report, keep running
                std::cout << "\nError: " << e.what() << "\n\n";
            }
        }
    } catch (const std::exception& e) {  // closed stdin
        std::cout << "\n" << e.what() << ". Exiting.\n";
    }
}