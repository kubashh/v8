// Copyright 2019 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>
#include "sort.h"

extern "C" {
extern void consoleLog(int num);
}

template <typename T>
class Node {
 public:
  Node(const T& value = 0) : value_(value) {}

  __attribute__((noinline)) bool operator<=(const Node<T>& rhs) const {
    return value_ <= rhs.value_;
  }

  T get_value() const { return value_; }

 private:
  T value_;
};

int main() {
  Node<uint64_t> nodes[] = {
      Node<uint64_t>{0x0833}, Node<uint64_t>{0x4572}, Node<uint64_t>{0x6804},
      Node<uint64_t>{0x3424}, Node<uint64_t>{0x6957}, Node<uint64_t>{0x7826},
      Node<uint64_t>{0x1914}, Node<uint64_t>{0x0032}, Node<uint64_t>{0x7929},
      Node<uint64_t>{0x1259}, Node<uint64_t>{0x4988}, Node<uint64_t>{0x2423},
      Node<uint64_t>{0x1742}, Node<uint64_t>{0x9399}, Node<uint64_t>{0x7512},
      Node<uint64_t>{0x1784}, Node<uint64_t>{0x6314}, Node<uint64_t>{0x0823},
      Node<uint64_t>{0x2897}, Node<uint64_t>{0x6659}, Node<uint64_t>{0x8643},
      Node<uint64_t>{0x7631}, Node<uint64_t>{0x3306}, Node<uint64_t>{0x9215},
      Node<uint64_t>{0x7750}, Node<uint64_t>{0x0449}, Node<uint64_t>{0x1703},
      Node<uint64_t>{0x2181}, Node<uint64_t>{0x4106}, Node<uint64_t>{0x3104},
      Node<uint64_t>{0x8702}, Node<uint64_t>{0x2650}};

  quick_sort<Node<uint64_t>>(nodes, (sizeof(nodes) / sizeof(nodes[0])));
  consoleLog(nodes[0].get_value());

  return nodes[0].get_value();
}
