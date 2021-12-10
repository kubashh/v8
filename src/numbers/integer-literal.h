// Copyright 2021 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_NUMBERS_INTEGER_LITERAL_H_
#define V8_NUMBERS_INTEGER_LITERAL_H_

#include "src/base/optional.h"
#include "src/bigint/bigint.h"
#include "src/common/globals.h"

namespace v8 {
namespace internal {

template <typename T, size_t Shift, bool IsSafe>
struct SafeRightShifter {
  static constexpr T Do(T value) { return value >> Shift; }
};
template <typename T, size_t Shift>
struct SafeRightShifter<T, Shift, false> {
  static constexpr T Do(T value) { return T(0); }
};

template <size_t Shift, typename T>
constexpr T SafeRightShift(T value) {
  return SafeRightShifter < T, Shift, Shift<sizeof(T) * 8>::Do(value);
}

class IntegerLiteral {
 public:
  using digit_t = bigint::digit_t;
  static constexpr int kMaxLength =
      (1 << 30) / (kSystemPointerSize * kBitsPerByte);

  template <typename T,
            typename = typename std::enable_if<std::is_integral<T>::value>>
  explicit IntegerLiteral(T value) : sign_(false) {
    if (value == T(0)) return;
    auto absolute = static_cast<typename std::make_unsigned<T>::type>(value);
    if (std::is_signed<T>::value && value < T(0)) {
      sign_ = true;
      value = (~value) + 1;
    }
    do {
      digits_.push_back(static_cast<digit_t>(absolute));
      absolute = SafeRightShift<sizeof(digit_t) * 8>(absolute);
    } while (absolute != 0);
#ifdef DEBUG
    const T expected_value = *As<T>();
    DCHECK_EQ(expected_value, value);
#endif  // DEBUG
  }

  static inline IntegerLiteral ForLength(int length, bool sign = false) {
    return IntegerLiteral(sign, std::vector<digit_t>(length));
  }

  static inline IntegerLiteral FromInt(int value) {
    static_assert(sizeof(digit_t) >= sizeof(int),
                  "digit_t must be at least the size of an int");
    const digit_t d = static_cast<digit_t>(std::labs(value));
    return IntegerLiteral(value < 0, {d});
  }

  static inline IntegerLiteral FromInt64(int64_t value) {
    int64_t absolute = value;
    if (value < int64_t{0}) {
      absolute = (~absolute) + 1;
    }
    if (sizeof(int64_t) == sizeof(digit_t)) {
      return IntegerLiteral(value < 0, {static_cast<digit_t>(absolute)});
    } else {
      DCHECK_EQ(sizeof(int64_t), 2 * sizeof(digit_t));
      return IntegerLiteral(
          value < 0, {static_cast<digit_t>(absolute),
                      static_cast<digit_t>(absolute >> sizeof(digit_t))});
    }
  }

  inline bool sign() const { return sign_; }
  inline void set_sign(bool sign) { sign_ = sign; }
  inline int length() const { return static_cast<int>(digits_.size()); }
  inline bigint::RWDigits GetRWDigits() {
    return bigint::RWDigits(digits_.data(), static_cast<int>(digits_.size()));
  }
  inline bigint::Digits GetDigits() const {
    return bigint::Digits(const_cast<digit_t*>(digits_.data()),
                          static_cast<int>(digits_.size()));
  }

  inline std::string ToString() const {
    // Special case 0 here, because ToString seems broken for that.
    if (digits_.empty() || (digits_.size() == 1 && digits_[0] == 0)) {
      return "0";
    }
    std::array<char, 22> buffer;
    int len = static_cast<int>(buffer.size());
    bigint::Processor* processor =
        bigint::Processor::New(new bigint::Platform());
    processor->ToString(buffer.data(), &len, GetDigits(), 10, sign());
    // In the rare case the buffer wasn't large enough, rerun with a larger
    // buffer.
    if (len <= 22) {
      processor->Destroy();
      return std::string(buffer.begin(), buffer.begin() + len);
    } else {
      int large_len = len;
      auto large_buffer = std::make_unique<char[]>(large_len);
      processor->ToString(large_buffer.get(), &large_len, GetDigits(), 10,
                          sign());
      DCHECK_LE(large_len, len);
      processor->Destroy();
      return std::string(large_buffer.get(), large_buffer.get() + large_len);
    }
  }

  template <typename T,
            typename = typename std::enable_if<std::is_integral<T>::value>>
  inline bool RepresentableAs() const {
    if (digits_.empty()) return true;

    // Find first non-0 bit.
    size_t msd_index = digits_.size() - 1;
    for (; msd_index > 0 && digits_[msd_index] == 0; --msd_index) {
    }
    size_t required_bits = msd_index * sizeof(digit_t);
    digit_t d = digits_[msd_index];
    static_assert(std::is_unsigned<digit_t>::value,
                  "Algorithm requires logical right shift");
    while (d != 0) {
      d = d >> 1;
      ++required_bits;
    }

    if (std::is_unsigned<T>::value) {
      // Negative values cannot fit into unsigned types.
      if (sign_) return false;
      return required_bits <= sizeof(digit_t) * kBitsPerByte;
    } else {
      if (required_bits < sizeof(digit_t) * kBitsPerByte) return true;
      // The only other case where this value fits into T is when value is
      // exactly std::numeric_limits<T>::min().
      return (required_bits == sizeof(digit_t) * kBitsPerByte) && sign_ &&
             IsPowerOfTwo();
    }
  }

  template <typename T,
            typename = typename std::enable_if<std::is_integral<T>::value>>
  inline base::Optional<T> As() const {
    if (!RepresentableAs<T>()) return base::nullopt;
    T value = 0;
    if (digits_.empty()) return value;
    for (size_t i = 0; i < digits_.size(); ++i) {
      value = value | (static_cast<T>(digits_[i])
                       << (sizeof(digit_t) * kBitsPerByte * i));
    }
    if (sign_) {
      value = (~value + 1);
    }
    return value;
  }

  inline bool IsZero() const {
    return std::all_of(digits_.begin(), digits_.end(),
                       [](digit_t d) { return d == 0; });
  }

  inline bool IsPowerOfTwo() const {
    if (digits_.empty()) return false;
    size_t i = 0;
    for (; i < digits_.size() && digits_[i] == 0; ++i) {
    }
    if (i == digits_.size()) return false;
    const digit_t d = digits_[i++];
    DCHECK_GT(d, 0);
    if ((d & (d - 1)) != 0) return false;
    for (; i < digits_.size(); ++i) {
      if (digits_[i] != 0) return false;
    }
    return true;
  }

 private:
  IntegerLiteral(bool sign, std::vector<digit_t> digits)
      : sign_(sign), digits_(std::move(digits)) {}

  bool sign_;
  std::vector<digit_t> digits_;
};

inline std::ostream& operator<<(std::ostream& stream,
                                const IntegerLiteral& literal) {
  return stream << literal.ToString();
}

inline bool operator==(const IntegerLiteral& lhs, const IntegerLiteral& rhs) {
  int diff = bigint::Compare(lhs.GetDigits(), rhs.GetDigits());
  if (diff != 0) return false;
  return lhs.IsZero() || lhs.sign() == rhs.sign();
}

inline bool operator!=(const IntegerLiteral& lhs, const IntegerLiteral& rhs) {
  return !operator==(lhs, rhs);
}

inline IntegerLiteral operator|(const IntegerLiteral& lhs,
                                const IntegerLiteral& rhs) {
  int result_length = bigint::BitwiseOrResultLength(lhs.length(), rhs.length());
  auto result =
      IntegerLiteral::ForLength(result_length, lhs.sign() || rhs.sign());
  if (lhs.sign()) {
    if (rhs.sign()) {
      bigint::BitwiseOr_NegNeg(result.GetRWDigits(), lhs.GetDigits(),
                               rhs.GetDigits());
    } else {
      bigint::BitwiseOr_PosNeg(result.GetRWDigits(), rhs.GetDigits(),
                               lhs.GetDigits());
    }
  } else {
    if (rhs.sign()) {
      bigint::BitwiseOr_PosNeg(result.GetRWDigits(), lhs.GetDigits(),
                               rhs.GetDigits());
    } else {
      bigint::BitwiseOr_PosPos(result.GetRWDigits(), lhs.GetDigits(),
                               rhs.GetDigits());
    }
  }
  return result;
}

inline IntegerLiteral RightShiftByAbsolute(const IntegerLiteral& lhs,
                                           IntegerLiteral::digit_t rhs) {
  bigint::RightShiftState state;
  const int result_length =
      bigint::RightShift_ResultLength(lhs.GetDigits(), lhs.sign(), rhs, &state);
  if (result_length <= 0) return IntegerLiteral(lhs.sign() ? -1 : 0);
  auto result = IntegerLiteral::ForLength(result_length, lhs.sign());
  bigint::RightShift(result.GetRWDigits(), lhs.GetDigits(), rhs, state);
  return result;
}

inline IntegerLiteral LeftShiftByAbsolute(const IntegerLiteral& lhs,
                                          IntegerLiteral::digit_t rhs) {
  const int result_length =
      bigint::LeftShift_ResultLength(lhs.length(), lhs.GetDigits().msd(), rhs);
  DCHECK_LE(result_length, IntegerLiteral::kMaxLength);
  auto result = IntegerLiteral::ForLength(result_length, lhs.sign());
  bigint::LeftShift(result.GetRWDigits(), lhs.GetDigits(), rhs);
  return result;
}

inline IntegerLiteral operator<<(const IntegerLiteral& lhs,
                                 const IntegerLiteral& rhs) {
  if (lhs.IsZero() || rhs.IsZero()) return lhs;
  DCHECK_EQ(rhs.length(), 1);
  if (rhs.sign()) return RightShiftByAbsolute(lhs, rhs.GetDigits()[0]);
  return LeftShiftByAbsolute(lhs, rhs.GetDigits()[0]);
}

inline IntegerLiteral operator+(const IntegerLiteral& lhs,
                                const IntegerLiteral& rhs) {
  const int result_length = bigint::AddSignedResultLength(
      lhs.length(), rhs.length(), lhs.sign() == rhs.sign());
  auto result = IntegerLiteral::ForLength(result_length);
  bool result_sign = bigint::AddSigned(result.GetRWDigits(), lhs.GetDigits(),
                                       lhs.sign(), rhs.GetDigits(), rhs.sign());
  result.set_sign(result_sign);
  return result;
}

}  // namespace internal
}  // namespace v8
#endif  // V8_NUMBERS_INTEGER_LITERAL_H_
