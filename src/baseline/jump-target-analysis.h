// Copyright 2018 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_BASELINE_JUMP_TARGET_ANALYSIS_H_
#define V8_BASELINE_JUMP_TARGET_ANALYSIS_H_

#include "src/base/macros.h"
#include "src/code-stub-assembler.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {

class BytecodeArray;

namespace interpreter {

class BytecodeArrayIterator;

}  // namespace interpreter

namespace baseline {

class JumpTargetAnalysis final {
 public:
  typedef CodeStubAssembler::Label Label;
  typedef CodeStubAssembler::Variable Variable;

  class Iterator final {
   public:
    explicit Iterator(ZoneMap<int, Label>* labels)
        : iterator_(labels->begin()), end_(labels->end()) {}

    void Next() {
      if (!Done()) {
        iterator_++;
      }
    }
    bool Done() { return iterator_ == end_; }

    int target_offset() {
      if (Done()) {
        return -1;
      } else {
        return iterator_->first;
      }
    }

    Label* label() {
      DCHECK(!Done());
      return &iterator_->second;
    }

   private:
    ZoneMap<int, Label>::iterator iterator_;
    ZoneMap<int, Label>::iterator end_;
  };

  JumpTargetAnalysis(Zone* zone, Handle<BytecodeArray> bytecode);

  // Analyse bytecode for jump targets.
  void Analyse(CodeStubAssembler* assembler,
               const compiler::CodeAssemblerVariableList& merged_variables);

  Label* LabelForTarget(int jump_target);

  Iterator GetIterator() { return Iterator(&labels_); }

 private:
  Handle<BytecodeArray> bytecode_array() const { return bytecode_array_; }

  Handle<BytecodeArray> bytecode_array_;
  ZoneMap<int, Label> labels_;
};

}  // namespace baseline
}  // namespace internal
}  // namespace v8

#endif  // V8_BASELINE_JUMP_TARGET_ANALYSIS_H_
