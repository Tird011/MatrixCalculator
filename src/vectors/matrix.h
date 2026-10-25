#pragma once
#include <cstddef>
#include <vector>

namespace ml {

// Dense row-major matrix. Plain data only: no I/O, no graphics, no threads.
class Matrix {
public:
    Matrix() = default;
    Matrix(std::size_t rows, std::size_t cols) : rows_(rows), cols_(cols), data_(rows * cols, 0.0) {}

    std::size_t rows() const { return rows_; }
    std::size_t cols() const { return cols_; }
    double& operator()(std::size_t r, std::size_t c) { return data_[r * cols_ + c]; }
    double operator()(std::size_t r, std::size_t c) const { return data_[r * cols_ + c]; }

private:
    std::size_t rows_{0};
    std::size_t cols_{0};
    std::vector<double> data_;
};

}  // namespace ml