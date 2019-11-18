// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

template <typename T>
void swap(T& t1, T& t2) {
  T temp = t1;
  t1 = t2;
  t2 = temp;
}

template <typename T>
struct less {
  bool operator()(const T& __x, const T& __y) const { return __x <= __y; }
};

template <typename T, typename compare = less<T>>
int64_t partition(T input[], int64_t l_idx, int64_t r_idx,
                  compare comp = compare()) {
  const T& pivot = input[r_idx];  // pivot
  int64_t i = (l_idx - 1);        // Index of smaller element

  for (int64_t j = l_idx; j <= r_idx - 1; j++) {
    // If current element is smaller than or equal to pivot
    if (comp(input[j], pivot)) {
      i++;  // increment index of smaller element
      swap(input[i], input[j]);
    }
  }
  swap(input[i + 1], input[r_idx]);
  return (i + 1);
}

template <typename T, typename compare = less<T>>
void q_sort(T input[], int64_t l_idx, int64_t r_idx, compare comp = compare()) {
  if (l_idx >= r_idx) return;

  int64_t pi = partition(input, l_idx, r_idx, comp);

  q_sort(input, l_idx, pi - 1);
  q_sort(input, pi + 1, r_idx);
}

template <typename T, typename compare = less<T>>
void quick_sort(T array[], int64_t N, compare comp = compare()) {
  q_sort(array, 0, N - 1, comp);
}
