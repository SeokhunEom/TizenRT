#include <cassert>
#include <limits>

#include <google/protobuf/util/internal/datapiece.h>

using google::protobuf::int32;
using google::protobuf::int64;
using google::protobuf::uint32;
using google::protobuf::uint64;
using google::protobuf::util::converter::DataPiece;

template <typename Integer>
void CheckFloatingConversion(Integer value) {
  const DataPiece piece(value);
  const auto as_double = piece.ToDouble();
  const auto as_float = piece.ToFloat();
  assert(as_double.ok());
  assert(as_float.ok());
  assert(as_double.ValueOrDie() == static_cast<double>(value));
  assert(as_float.ValueOrDie() == static_cast<float>(value));
}

int main() {
  // Conversion validation originally compares in the common arithmetic type.
  // Rounded integer-to-float values remain accepted, including values that
  // round outside the original integer type's range. No back-cast is valid.
  CheckFloatingConversion(std::numeric_limits<int64>::max());
  CheckFloatingConversion(std::numeric_limits<int64>::min());
  CheckFloatingConversion(std::numeric_limits<uint64>::max());
  CheckFloatingConversion(std::numeric_limits<int32>::max());
  CheckFloatingConversion(std::numeric_limits<uint32>::max());
  CheckFloatingConversion(int64(9007199254740993LL));
  CheckFloatingConversion(int32(16777217));
  CheckFloatingConversion(int64(0));

  // Sign checks must still reject signed/unsigned wraparound, while allowing
  // exact conversions at the integer boundaries.
  assert(!DataPiece(int64(-1)).ToUint64().ok());
  assert(!DataPiece(std::numeric_limits<int64>::min()).ToUint64().ok());
  assert(!DataPiece(std::numeric_limits<uint64>::max()).ToInt64().ok());
  assert(!DataPiece(std::numeric_limits<int64>::max()).ToInt32().ok());
  const auto maximum = DataPiece(std::numeric_limits<int64>::max()).ToUint64();
  assert(maximum.ok());
  assert(maximum.ValueOrDie() == uint64(std::numeric_limits<int64>::max()));
  const auto unsigned32 = DataPiece(std::numeric_limits<uint32>::max()).ToInt64();
  assert(unsigned32.ok());
  assert(unsigned32.ValueOrDie() == int64(std::numeric_limits<uint32>::max()));

  // Floating-to-integer tests stay within the destination range, so UBSan
  // detects regressions in validation without exercising undefined inputs.
  assert(!DataPiece(1.5).ToInt32().ok());
  assert(!DataPiece(-1.5f).ToInt32().ok());
  const auto positive = DataPiece(42.0).ToUint64();
  const auto negative = DataPiece(-42.0f).ToInt64();
  const auto zero = DataPiece(-0.0).ToInt32();
  assert(positive.ok() && positive.ValueOrDie() == 42);
  assert(negative.ok() && negative.ValueOrDie() == -42);
  assert(zero.ok() && zero.ValueOrDie() == 0);
}
