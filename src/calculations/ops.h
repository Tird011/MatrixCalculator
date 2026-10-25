#pragma once
#include "vectors/result.h"

// Every function returns a Result (answer + steps + timing).
// Wrong shapes throw std::invalid_argument; inverse of a singular matrix throws std::domain_error.
namespace ml {

Result add(const Matrix& a, const Matrix& b);
Result subtract(const Matrix& a, const Matrix& b);
Result transpose(const Matrix& a);
Result multiply(const Matrix& a, const Matrix& b, Schedule schedule);
Result determinant(const Matrix& a);  // scalar answer
Result inverse(const Matrix& a);

}  // namespace ml