#ifndef PROTOBUF_TESTS_HOST_MATH_COMPAT_H_
#define PROTOBUF_TESTS_HOST_MATH_COMPAT_H_

#include <cmath>

// The TizenRT MathLimits header uses global C math names. libstdc++ in C++11
// exposes these overloads in std; make them available to the native test build.
using std::isinf;
using std::isnan;

#endif  // PROTOBUF_TESTS_HOST_MATH_COMPAT_H_
