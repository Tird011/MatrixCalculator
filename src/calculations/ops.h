#pragma once
#include "vectors/result.h"

// Every function returns a Result: the answer plus why it is right, worked numbers, and timing.
// Wrong shapes throw std::invalid_argument; inverse of a singular matrix throws std::domain_error.
// `name` is the letter used in titles and explanations ('A' or 'B').
namespace ml {

Result add(const Matrix& a, const Matrix& b);
Result subtract(const Matrix& a, const Matrix& b);
Result transpose(const Matrix& a, char name = 'A');
Result multiply(const Matrix& a, const Matrix& b, Schedule schedule);
Result determinant(const Matrix& a, char name = 'A');  // scalar answer
Result inverse(const Matrix& a, char name = 'A');

// Times A x B on one thread and on `schedule`, averaged over many runs (tiny matrices need many
// runs for a stable number). Does the same work as multiply(), so it is a fair comparison.
Comparison compareMultiply(const Matrix& a, const Matrix& b, Schedule schedule);

}  // namespace ml