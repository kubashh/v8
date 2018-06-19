// Copyright 2012 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/debug/liveedit.h"

#include "src/api.h"
#include "src/assembler-inl.h"
#include "src/ast/ast-traversal-visitor.h"
#include "src/ast/scopes.h"
#include "src/code-stubs.h"
#include "src/compilation-cache.h"
#include "src/compiler.h"
#include "src/debug/debug-interface.h"
#include "src/debug/debug.h"
#include "src/deoptimizer.h"
#include "src/frames-inl.h"
#include "src/global-handles.h"
#include "src/isolate-inl.h"
#include "src/messages.h"
#include "src/objects-inl.h"
#include "src/objects/hash-table-inl.h"
#include "src/parsing/parse-info.h"
#include "src/parsing/parsing.h"
#include "src/source-position-table.h"
#include "src/v8.h"
#include "src/v8memory.h"

namespace v8 {
namespace internal {
namespace {
// A general-purpose comparator between 2 arrays.
class Comparator {
 public:
  // Holds 2 arrays of some elements allowing to compare any pair of
  // element from the first array and element from the second array.
  class Input {
   public:
    virtual int GetLength1() = 0;
    virtual int GetLength2() = 0;
    virtual bool Equals(int index1, int index2) = 0;

   protected:
    virtual ~Input() {}
  };

  // Receives compare result as a series of chunks.
  class Output {
   public:
    // Puts another chunk in result list. Note that technically speaking
    // only 3 arguments actually needed with 4th being derivable.
    virtual void AddChunk(int pos1, int pos2, int len1, int len2) = 0;

   protected:
    virtual ~Output() {}
  };

  // Finds the difference between 2 arrays of elements.
  static void CalculateDifference(Input* input, Output* result_writer);
};

// A simple implementation of dynamic programming algorithm. It solves
// the problem of finding the difference of 2 arrays. It uses a table of results
// of subproblems. Each cell contains a number together with 2-bit flag
// that helps building the chunk list.
class Differencer {
 public:
  explicit Differencer(Comparator::Input* input)
      : input_(input), len1_(input->GetLength1()), len2_(input->GetLength2()) {
    buffer_ = NewArray<int>(len1_ * len2_);
  }
  ~Differencer() {
    DeleteArray(buffer_);
  }

  void Initialize() {
    int array_size = len1_ * len2_;
    for (int i = 0; i < array_size; i++) {
      buffer_[i] = kEmptyCellValue;
    }
  }

  // Makes sure that result for the full problem is calculated and stored
  // in the table together with flags showing a path through subproblems.
  void FillTable() {
    CompareUpToTail(0, 0);
  }

  void SaveResult(Comparator::Output* chunk_writer) {
    ResultWriter writer(chunk_writer);

    int pos1 = 0;
    int pos2 = 0;
    while (true) {
      if (pos1 < len1_) {
        if (pos2 < len2_) {
          Direction dir = get_direction(pos1, pos2);
          switch (dir) {
            case EQ:
              writer.eq();
              pos1++;
              pos2++;
              break;
            case SKIP1:
              writer.skip1(1);
              pos1++;
              break;
            case SKIP2:
            case SKIP_ANY:
              writer.skip2(1);
              pos2++;
              break;
            default:
              UNREACHABLE();
          }
        } else {
          writer.skip1(len1_ - pos1);
          break;
        }
      } else {
        if (len2_ != pos2) {
          writer.skip2(len2_ - pos2);
        }
        break;
      }
    }
    writer.close();
  }

 private:
  Comparator::Input* input_;
  int* buffer_;
  int len1_;
  int len2_;

  enum Direction {
    EQ = 0,
    SKIP1,
    SKIP2,
    SKIP_ANY,

    MAX_DIRECTION_FLAG_VALUE = SKIP_ANY
  };

  // Computes result for a subtask and optionally caches it in the buffer table.
  // All results values are shifted to make space for flags in the lower bits.
  int CompareUpToTail(int pos1, int pos2) {
    if (pos1 < len1_) {
      if (pos2 < len2_) {
        int cached_res = get_value4(pos1, pos2);
        if (cached_res == kEmptyCellValue) {
          Direction dir;
          int res;
          if (input_->Equals(pos1, pos2)) {
            res = CompareUpToTail(pos1 + 1, pos2 + 1);
            dir = EQ;
          } else {
            int res1 = CompareUpToTail(pos1 + 1, pos2) +
                (1 << kDirectionSizeBits);
            int res2 = CompareUpToTail(pos1, pos2 + 1) +
                (1 << kDirectionSizeBits);
            if (res1 == res2) {
              res = res1;
              dir = SKIP_ANY;
            } else if (res1 < res2) {
              res = res1;
              dir = SKIP1;
            } else {
              res = res2;
              dir = SKIP2;
            }
          }
          set_value4_and_dir(pos1, pos2, res, dir);
          cached_res = res;
        }
        return cached_res;
      } else {
        return (len1_ - pos1) << kDirectionSizeBits;
      }
    } else {
      return (len2_ - pos2) << kDirectionSizeBits;
    }
  }

  inline int& get_cell(int i1, int i2) {
    return buffer_[i1 + i2 * len1_];
  }

  // Each cell keeps a value plus direction. Value is multiplied by 4.
  void set_value4_and_dir(int i1, int i2, int value4, Direction dir) {
    DCHECK_EQ(0, value4 & kDirectionMask);
    get_cell(i1, i2) = value4 | dir;
  }

  int get_value4(int i1, int i2) {
    return get_cell(i1, i2) & (kMaxUInt32 ^ kDirectionMask);
  }
  Direction get_direction(int i1, int i2) {
    return static_cast<Direction>(get_cell(i1, i2) & kDirectionMask);
  }

  static const int kDirectionSizeBits = 2;
  static const int kDirectionMask = (1 << kDirectionSizeBits) - 1;
  static const int kEmptyCellValue = ~0u << kDirectionSizeBits;

  // This method only holds static assert statement (unfortunately you cannot
  // place one in class scope).
  void StaticAssertHolder() {
    STATIC_ASSERT(MAX_DIRECTION_FLAG_VALUE < (1 << kDirectionSizeBits));
  }

  class ResultWriter {
   public:
    explicit ResultWriter(Comparator::Output* chunk_writer)
        : chunk_writer_(chunk_writer), pos1_(0), pos2_(0),
          pos1_begin_(-1), pos2_begin_(-1), has_open_chunk_(false) {
    }
    void eq() {
      FlushChunk();
      pos1_++;
      pos2_++;
    }
    void skip1(int len1) {
      StartChunk();
      pos1_ += len1;
    }
    void skip2(int len2) {
      StartChunk();
      pos2_ += len2;
    }
    void close() {
      FlushChunk();
    }

   private:
    Comparator::Output* chunk_writer_;
    int pos1_;
    int pos2_;
    int pos1_begin_;
    int pos2_begin_;
    bool has_open_chunk_;

    void StartChunk() {
      if (!has_open_chunk_) {
        pos1_begin_ = pos1_;
        pos2_begin_ = pos2_;
        has_open_chunk_ = true;
      }
    }

    void FlushChunk() {
      if (has_open_chunk_) {
        chunk_writer_->AddChunk(pos1_begin_, pos2_begin_,
                                pos1_ - pos1_begin_, pos2_ - pos2_begin_);
        has_open_chunk_ = false;
      }
    }
  };
};

void Comparator::CalculateDifference(Comparator::Input* input,
                                     Comparator::Output* result_writer) {
  Differencer differencer(input);
  differencer.Initialize();
  differencer.FillTable();
  differencer.SaveResult(result_writer);
}

static bool CompareSubstrings(Handle<String> s1, int pos1,
                              Handle<String> s2, int pos2, int len) {
  for (int i = 0; i < len; i++) {
    if (s1->Get(i + pos1) != s2->Get(i + pos2)) {
      return false;
    }
  }
  return true;
}

// Additional to Input interface. Lets switch Input range to subrange.
// More elegant way would be to wrap one Input as another Input object
// and translate positions there, but that would cost us additional virtual
// call per comparison.
class SubrangableInput : public Comparator::Input {
 public:
  virtual void SetSubrange1(int offset, int len) = 0;
  virtual void SetSubrange2(int offset, int len) = 0;
};


class SubrangableOutput : public Comparator::Output {
 public:
  virtual void SetSubrange1(int offset, int len) = 0;
  virtual void SetSubrange2(int offset, int len) = 0;
};

int min(int a, int b) { return a < b ? a : b; }

// Finds common prefix and suffix in input. This parts shouldn't take space in
// linear programming table. Enable subranging in input and output.
void NarrowDownInput(SubrangableInput* input, SubrangableOutput* output) {
  const int len1 = input->GetLength1();
  const int len2 = input->GetLength2();

  int common_prefix_len;
  int common_suffix_len;

  {
    common_prefix_len = 0;
    int prefix_limit = min(len1, len2);
    while (common_prefix_len < prefix_limit &&
        input->Equals(common_prefix_len, common_prefix_len)) {
      common_prefix_len++;
    }

    common_suffix_len = 0;
    int suffix_limit = min(len1 - common_prefix_len, len2 - common_prefix_len);

    while (common_suffix_len < suffix_limit &&
        input->Equals(len1 - common_suffix_len - 1,
        len2 - common_suffix_len - 1)) {
      common_suffix_len++;
    }
  }

  if (common_prefix_len > 0 || common_suffix_len > 0) {
    int new_len1 = len1 - common_suffix_len - common_prefix_len;
    int new_len2 = len2 - common_suffix_len - common_prefix_len;

    input->SetSubrange1(common_prefix_len, new_len1);
    input->SetSubrange2(common_prefix_len, new_len2);

    output->SetSubrange1(common_prefix_len, new_len1);
    output->SetSubrange2(common_prefix_len, new_len2);
  }
}

class CompareOutputVectorWrite {
 public:
  explicit CompareOutputVectorWrite(Isolate*) {}
  void WriteChunk(int pos1, int pos2, int len1, int len2) {
    output_.push_back(
        SourceChangeRange({pos1, pos1 + len1, pos2, pos2 + len2}));
  }
  std::vector<SourceChangeRange> GetVector() { return output_; }

 private:
  std::vector<SourceChangeRange> output_;
};

// Represents 2 strings as 2 arrays of tokens.
// TODO(LiveEdit): Currently it's actually an array of charactres.
//     Make array of tokens instead.
class TokensCompareInput : public Comparator::Input {
 public:
  TokensCompareInput(Handle<String> s1, int offset1, int len1,
                       Handle<String> s2, int offset2, int len2)
      : s1_(s1), offset1_(offset1), len1_(len1),
        s2_(s2), offset2_(offset2), len2_(len2) {
  }
  virtual int GetLength1() {
    return len1_;
  }
  virtual int GetLength2() {
    return len2_;
  }
  bool Equals(int index1, int index2) {
    return s1_->Get(offset1_ + index1) == s2_->Get(offset2_ + index2);
  }

 private:
  Handle<String> s1_;
  int offset1_;
  int len1_;
  Handle<String> s2_;
  int offset2_;
  int len2_;
};

// Stores compare result in JSArray. Converts substring positions
// to absolute positions.
class TokensCompareOutput : public Comparator::Output {
 public:
  TokensCompareOutput(CompareOutputVectorWrite* array_writer, int offset1,
                      int offset2)
      : array_writer_(array_writer), offset1_(offset1), offset2_(offset2) {}

  void AddChunk(int pos1, int pos2, int len1, int len2) {
    array_writer_->WriteChunk(pos1 + offset1_, pos2 + offset2_, len1, len2);
  }

 private:
  CompareOutputVectorWrite* array_writer_;
  int offset1_;
  int offset2_;
};

// Wraps raw n-elements line_ends array as a list of n+1 lines. The last line
// never has terminating new line character.
class LineEndsWrapper {
 public:
  explicit LineEndsWrapper(Handle<String> string)
      : ends_array_(String::CalculateLineEnds(string, false)),
        string_len_(string->length()) {
  }
  int length() {
    return ends_array_->length() + 1;
  }
  // Returns start for any line including start of the imaginary line after
  // the last line.
  int GetLineStart(int index) {
    if (index == 0) {
      return 0;
    } else {
      return GetLineEnd(index - 1);
    }
  }
  int GetLineEnd(int index) {
    if (index == ends_array_->length()) {
      // End of the last line is always an end of the whole string.
      // If the string ends with a new line character, the last line is an
      // empty string after this character.
      return string_len_;
    } else {
      return GetPosAfterNewLine(index);
    }
  }

 private:
  Handle<FixedArray> ends_array_;
  int string_len_;

  int GetPosAfterNewLine(int index) {
    return Smi::ToInt(ends_array_->get(index)) + 1;
  }
};

// Represents 2 strings as 2 arrays of lines.
class LineArrayCompareInput : public SubrangableInput {
 public:
  LineArrayCompareInput(Handle<String> s1, Handle<String> s2,
                        LineEndsWrapper line_ends1, LineEndsWrapper line_ends2)
      : s1_(s1), s2_(s2), line_ends1_(line_ends1),
        line_ends2_(line_ends2),
        subrange_offset1_(0), subrange_offset2_(0),
        subrange_len1_(line_ends1_.length()),
        subrange_len2_(line_ends2_.length()) {
  }
  int GetLength1() {
    return subrange_len1_;
  }
  int GetLength2() {
    return subrange_len2_;
  }
  bool Equals(int index1, int index2) {
    index1 += subrange_offset1_;
    index2 += subrange_offset2_;

    int line_start1 = line_ends1_.GetLineStart(index1);
    int line_start2 = line_ends2_.GetLineStart(index2);
    int line_end1 = line_ends1_.GetLineEnd(index1);
    int line_end2 = line_ends2_.GetLineEnd(index2);
    int len1 = line_end1 - line_start1;
    int len2 = line_end2 - line_start2;
    if (len1 != len2) {
      return false;
    }
    return CompareSubstrings(s1_, line_start1, s2_, line_start2,
                             len1);
  }
  void SetSubrange1(int offset, int len) {
    subrange_offset1_ = offset;
    subrange_len1_ = len;
  }
  void SetSubrange2(int offset, int len) {
    subrange_offset2_ = offset;
    subrange_len2_ = len;
  }

 private:
  Handle<String> s1_;
  Handle<String> s2_;
  LineEndsWrapper line_ends1_;
  LineEndsWrapper line_ends2_;
  int subrange_offset1_;
  int subrange_offset2_;
  int subrange_len1_;
  int subrange_len2_;
};


// Stores compare result in JSArray. For each chunk tries to conduct
// a fine-grained nested diff token-wise.
class TokenizingLineArrayCompareOutput : public SubrangableOutput {
 public:
  TokenizingLineArrayCompareOutput(LineEndsWrapper line_ends1,
                                   LineEndsWrapper line_ends2,
                                   Handle<String> s1, Handle<String> s2)
      : array_writer_(s1->GetIsolate()),
        line_ends1_(line_ends1), line_ends2_(line_ends2), s1_(s1), s2_(s2),
        subrange_offset1_(0), subrange_offset2_(0) {
  }

  void AddChunk(int line_pos1, int line_pos2, int line_len1, int line_len2) {
    line_pos1 += subrange_offset1_;
    line_pos2 += subrange_offset2_;

    int char_pos1 = line_ends1_.GetLineStart(line_pos1);
    int char_pos2 = line_ends2_.GetLineStart(line_pos2);
    int char_len1 = line_ends1_.GetLineStart(line_pos1 + line_len1) - char_pos1;
    int char_len2 = line_ends2_.GetLineStart(line_pos2 + line_len2) - char_pos2;

    if (char_len1 < CHUNK_LEN_LIMIT && char_len2 < CHUNK_LEN_LIMIT) {
      // Chunk is small enough to conduct a nested token-level diff.
      HandleScope subTaskScope(s1_->GetIsolate());

      TokensCompareInput tokens_input(s1_, char_pos1, char_len1,
                                      s2_, char_pos2, char_len2);
      TokensCompareOutput tokens_output(&array_writer_, char_pos1, char_pos2);

      Comparator::CalculateDifference(&tokens_input, &tokens_output);
    } else {
      array_writer_.WriteChunk(char_pos1, char_pos2, char_len1, char_len2);
    }
  }
  void SetSubrange1(int offset, int len) {
    subrange_offset1_ = offset;
  }
  void SetSubrange2(int offset, int len) {
    subrange_offset2_ = offset;
  }
  std::vector<SourceChangeRange> GetVector() {
    return array_writer_.GetVector();
  }

 private:
  static const int CHUNK_LEN_LIMIT = 800;

  CompareOutputVectorWrite array_writer_;
  LineEndsWrapper line_ends1_;
  LineEndsWrapper line_ends2_;
  Handle<String> s1_;
  Handle<String> s2_;
  int subrange_offset1_;
  int subrange_offset2_;
};

std::vector<SourceChangeRange> CompareSources(Handle<String> s1,
                                              Handle<String> s2) {
  s1 = String::Flatten(s1);
  s2 = String::Flatten(s2);

  LineEndsWrapper line_ends1(s1);
  LineEndsWrapper line_ends2(s2);

  LineArrayCompareInput input(s1, s2, line_ends1, line_ends2);
  TokenizingLineArrayCompareOutput output(line_ends1, line_ends2, s1, s2);

  NarrowDownInput(&input, &output);

  Comparator::CalculateDifference(&input, &output);

  return output.GetVector();
}

struct SourcePositionEvent {
  // Should be sorted by precedence, first has maximum precedence.
  enum Type { DIFF_ENDS, LITERAL_ENDS, LITERAL_STARTS, DIFF_STARTS };

  int position;
  Type type;

  union {
    FunctionLiteral* literal;
    int pos_diff;
  };

  SourcePositionEvent(FunctionLiteral* literal, bool is_start)
      : position(is_start ? literal->start_position()
                          : literal->end_position()),
        type(is_start ? LITERAL_STARTS : LITERAL_ENDS),
        literal(literal) {}
  SourcePositionEvent(const SourceChangeRange& change, bool is_start)
      : position(is_start ? change.start_position : change.end_position),
        type(is_start ? DIFF_STARTS : DIFF_ENDS),
        pos_diff((change.new_end_position - change.new_start_position) -
                 (change.end_position - change.start_position)) {}

  static bool LessThen(const SourcePositionEvent& a,
                       const SourcePositionEvent& b) {
    if (a.position != b.position) return a.position < b.position;
    if (a.type != b.type) return a.type > b.type;
    if (a.type == LITERAL_ENDS) {
      return a.literal->start_position() > b.literal->start_position();
    }
    return false;
  }
};
}  // namespace

void LiveEdit::InitializeThreadLocal(Debug* debug) {
  debug->thread_local_.restart_fp_ = 0;
}

void LiveEdit::CalculateFunctionLiteralChanges(
    const std::vector<FunctionLiteral*>& literals,
    const std::vector<SourceChangeRange>& source_changes,
    FunctionLiteralChanges* result) {
  std::vector<SourcePositionEvent> events;
  events.reserve(literals.size() * 2 + source_changes.size() * 2);
  for (FunctionLiteral* literal : literals) {
    events.emplace_back(literal, true);
    events.emplace_back(literal, false);
  }
  for (const SourceChangeRange& source_change : source_changes) {
    events.emplace_back(source_change, true);
    events.emplace_back(source_change, false);
  }
  std::sort(events.begin(), events.end(), SourcePositionEvent::LessThen);
  bool inside_diff = false;
  int pos_diff = 0;
  std::stack<std::pair<FunctionLiteral*, FunctionLiteralChange>> literal_stack;
  for (const SourcePositionEvent& event : events) {
    switch (event.type) {
      case SourcePositionEvent::DIFF_ENDS:
        DCHECK(inside_diff);
        inside_diff = false;
        break;
      case SourcePositionEvent::LITERAL_ENDS: {
        DCHECK_EQ(literal_stack.top().first, event.literal);
        FunctionLiteralChange& change = literal_stack.top().second;
        change.new_end_position =
            inside_diff ? kNoSourcePosition
                        : event.literal->end_position() + pos_diff;
        result->insert(literal_stack.top());
        literal_stack.pop();
        break;
      }
      case SourcePositionEvent::LITERAL_STARTS:
        literal_stack.push(std::make_pair(
            event.literal,
            FunctionLiteralChange(
                inside_diff ? kNoSourcePosition
                            : event.literal->start_position() + pos_diff,
                literal_stack.empty() ? nullptr : literal_stack.top().first)));
        break;
      case SourcePositionEvent::DIFF_STARTS:
        DCHECK(!inside_diff);
        inside_diff = true;
        if (!literal_stack.empty()) {
          literal_stack.top().second.has_changes = true;
        }
        pos_diff += event.pos_diff;
        break;
    }
  }
}

namespace {
bool HasChangedScope(FunctionLiteral* a, FunctionLiteral* b) {
  Scope* scope_a = a->scope()->outer_scope();
  Scope* scope_b = b->scope()->outer_scope();
  while (scope_a && scope_b) {
    std::unordered_map<int, Handle<String>> vars;
    for (Variable* var : *scope_a->locals()) {
      if (!var->IsContextSlot()) continue;
      vars[var->index()] = var->name();
    }
    for (Variable* var : *scope_b->locals()) {
      if (!var->IsContextSlot()) continue;
      auto it = vars.find(var->index());
      if (it == vars.end()) return true;
      if (*it->second != *var->name()) return true;
    }
    scope_a = scope_a->outer_scope();
    scope_b = scope_b->outer_scope();
  }
  return scope_a != scope_b;
}

enum ChangeState { UNCHANGED, MOVED, SOURCE_CHANGED, CHANGED, DAMAGED };
}  // anonymous namespace

void LiveEdit::MapLiterals(const FunctionLiteralChanges& changes,
                           const std::vector<FunctionLiteral*>& new_literals,
                           LiteralMap* changed, LiteralMap* source_changed,
                           LiteralMap* moved) {
  std::unordered_map<int, std::unordered_map<int, FunctionLiteral*>>
      source_position_to_new_literal;
  for (FunctionLiteral* literal : new_literals) {
    DCHECK(literal->start_position() != kNoSourcePosition);
    DCHECK(literal->end_position() != kNoSourcePosition);
    source_position_to_new_literal[literal->start_position()]
                                  [literal->end_position()] = literal;
  }
  LiteralMap mappings;
  std::unordered_map<FunctionLiteral*, ChangeState> change_state;
  for (const auto& change_pair : changes) {
    FunctionLiteral* literal = change_pair.first;
    const FunctionLiteralChange& change = change_pair.second;
    auto new_literals_for_start_position_it =
        source_position_to_new_literal.find(change.new_start_position);
    if (new_literals_for_start_position_it ==
        source_position_to_new_literal.end()) {
      change_state[literal] = ChangeState::DAMAGED;
      continue;
    }
    const auto& new_literals_for_start_position_map =
        new_literals_for_start_position_it->second;
    auto new_literal_for_source_position_it =
        new_literals_for_start_position_map.find(change.new_end_position);
    if (new_literal_for_source_position_it ==
        new_literals_for_start_position_map.end()) {
      change_state[literal] = ChangeState::DAMAGED;
      continue;
    }
    FunctionLiteral* new_literal = new_literal_for_source_position_it->second;
    mappings[literal] = new_literal;
    if (HasChangedScope(literal, new_literal) ||
        literal->kind() != new_literal->kind()) {
      change_state[literal] = ChangeState::DAMAGED;
    } else if (change.has_changes) {
      change_state[literal] = ChangeState::CHANGED;
    } else if (literal->start_position() != new_literal->start_position() ||
               literal->end_position() != new_literal->end_position()) {
      change_state[literal] = ChangeState::MOVED;
    } else {
      change_state[literal] = ChangeState::UNCHANGED;
    }
  }

  std::unordered_map<FunctionLiteral*, FunctionLiteral*> outer_literal;
  for (const auto& change_pair : changes) {
    outer_literal[change_pair.first] = change_pair.second.outer_literal;
  }
  for (const auto& state : change_state) {
    if (state.second != ChangeState::DAMAGED &&
        state.second != ChangeState::CHANGED) {
      continue;
    }
    FunctionLiteral* outer = outer_literal[state.first];
    ChangeState inner_state = state.second;
    while (outer) {
      if (change_state[outer] >= inner_state) break;
      if (inner_state == ChangeState::DAMAGED) {
        change_state[outer] = ChangeState::CHANGED;
      } else if (inner_state == ChangeState::CHANGED) {
        if (change_state[outer] < ChangeState::SOURCE_CHANGED) {
          change_state[outer] = ChangeState::SOURCE_CHANGED;
        }
      } else {
        break;
      }
      inner_state = change_state[outer];
      outer = outer_literal[outer];
    }
  }

  for (const auto& mapping : mappings) {
    if (change_state[mapping.first] == ChangeState::DAMAGED) {
      continue;
    } else if (change_state[mapping.first] == ChangeState::UNCHANGED) {
      (*source_changed)[mapping.first] = mapping.second;
    } else if (change_state[mapping.first] == ChangeState::MOVED) {
      (*moved)[mapping.first] = mapping.second;
    } else if (change_state[mapping.first] == ChangeState::SOURCE_CHANGED) {
      (*source_changed)[mapping.first] = mapping.second;
    } else if (change_state[mapping.first] == ChangeState::CHANGED) {
      (*changed)[mapping.first] = mapping.second;
    }
  }
}

namespace {
Handle<Script> MakeScriptCopy(Handle<Script> original_script,
                              Handle<String> source) {
  Handle<Script> script =
      original_script->GetIsolate()->factory()->NewScript(source);
  script->set_name(original_script->name());
  script->set_line_offset(original_script->line_offset());
  script->set_column_offset(original_script->column_offset());
  script->set_context_data(original_script->context_data());
  script->set_type(original_script->type());
  script->set_eval_from_shared_or_wrapped_arguments(
      original_script->eval_from_shared_or_wrapped_arguments());
  script->set_eval_from_position(original_script->eval_from_position());
  script->set_flags(original_script->flags());
  script->set_compilation_state(Script::COMPILATION_STATE_INITIAL);
  script->set_host_defined_options(original_script->host_defined_options());
  return script;
}

bool CompileScript(Isolate* isolate, ParseInfo* parse_info, bool parse_only,
                   debug::LiveEditResult* result) {
  v8::TryCatch try_catch(reinterpret_cast<v8::Isolate*>(isolate));
  Handle<SharedFunctionInfo> shared;
  bool success = false;
  if (!parse_only) {
    success =
        Compiler::CompileForLiveEdit(parse_info, isolate).ToHandle(&shared);
  } else {
    success = parsing::ParseProgram(parse_info, isolate);
    if (success) {
      success = Compiler::Analyze(parse_info);
      parse_info->ast_value_factory()->Internalize(isolate);
    }
  }
  if (!success) {
    isolate->OptionalRescheduleException(false);
    DCHECK(try_catch.HasCaught());
    result->message = try_catch.Message()->Get();
    auto self = Utils::OpenHandle(*try_catch.Message());
    auto msg = i::Handle<i::JSMessageObject>::cast(self);
    result->line_number = msg->GetLineNumber();
    result->column_number = msg->GetColumnNumber();
    result->status = debug::LiveEditResult::COMPILE_ERROR;
    return false;
  }
  return true;
}

class CollectFunctionLiterals final
    : public AstTraversalVisitor<CollectFunctionLiterals> {
 public:
  CollectFunctionLiterals(Isolate* isolate, AstNode* root)
      : AstTraversalVisitor<CollectFunctionLiterals>(isolate, root) {
    CHECK(root);
  }
  void VisitFunctionLiteral(FunctionLiteral* lit) {
    AstTraversalVisitor::VisitFunctionLiteral(lit);
    literals_->push_back(lit);
  }
  void Run(std::vector<FunctionLiteral*>* literals) {
    literals_ = literals;
    AstTraversalVisitor::Run();
    literals_ = nullptr;
  }

 private:
  std::vector<FunctionLiteral*>* literals_;
};

class CompileScriptHelper {
 public:
  explicit CompileScriptHelper(bool parse_only, Handle<Script> script)
      : isolate_(script->GetIsolate()),
        parse_info_(script->GetIsolate(), script),
        parse_only_(parse_only) {
    parse_info_.set_eager();
  }

  bool GetLiterals(std::vector<FunctionLiteral*>* literals,
                   debug::LiveEditResult* result) {
    if (!CompileScript(isolate_, &parse_info_, parse_only_, result))
      return false;
    CollectFunctionLiterals visitor(isolate_, parse_info_.literal());
    visitor.Run(literals);
    return true;
  }

 private:
  Isolate* isolate_;
  ParseInfo parse_info_;
  bool parse_only_;
};

void TranslateSourcePositionTable(
    Handle<BytecodeArray> code, const std::vector<SourceChangeRange>& changes) {
  Isolate* isolate = code->GetIsolate();
  SourcePositionTableBuilder builder;

  Handle<ByteArray> source_position_table(code->SourcePositionTable());
  for (SourcePositionTableIterator iterator(*source_position_table);
       !iterator.done(); iterator.Advance()) {
    SourcePosition position = iterator.source_position();
    position.SetScriptOffset(
        LiveEdit::TranslatePosition(changes, position.ScriptOffset()));
    builder.AddPosition(iterator.code_offset(), position,
                        iterator.is_statement());
  }

  Handle<ByteArray> new_source_position_table(
      builder.ToSourcePositionTable(isolate));
  code->set_source_position_table(*new_source_position_table);
  LOG_CODE_EVENT(isolate,
                 CodeLinePosInfoRecordEvent(code->GetFirstBytecodeAddress(),
                                            *new_source_position_table));
}

struct ChangedData {
  Handle<SharedFunctionInfo> shared;
  Handle<SharedFunctionInfo> new_shared;
  std::vector<Handle<JSFunction>> js_functions;
};

struct FunctionData {
  FunctionData(FunctionLiteral* literal, bool should_restart)
      : literal(literal),
        stack_position(NOT_ON_STACK),
        should_restart(should_restart) {}

  FunctionLiteral* literal;
  MaybeHandle<SharedFunctionInfo> shared;
  std::vector<Handle<JSFunction>> js_functions;
  std::vector<Handle<JSGeneratorObject>> running_generators;
  enum StackPosition {
    NOT_ON_STACK,
    ABOVE_BREAK_FRAME,
    PATCHABLE,
    BELOW_NON_DROPPABLE_FRAME
  };
  StackPosition stack_position;
  bool should_restart;
};

using FunctionDataMap =
    std::unordered_map<int, std::unordered_map<int, FunctionData>>;

bool FunctionDataEntry(FunctionDataMap* map, int script_id,
                       int function_literal_id, FunctionData** data) {
  auto inner_map_it = map->find(script_id);
  if (inner_map_it == map->end()) return false;
  auto& inner_map = inner_map_it->second;
  auto it = inner_map.find(function_literal_id);
  if (it == inner_map.end()) return false;
  *data = &it->second;
  return true;
}

bool FunctionDataEntryForSharedFunctionInfo(FunctionDataMap* map,
                                            SharedFunctionInfo* sfi,
                                            FunctionData** data) {
  if (!sfi->script()->IsScript() || sfi->function_literal_id() == -1) {
    return false;
  }
  Script* script = Script::cast(sfi->script());
  return FunctionDataEntry(map, script->id(), sfi->function_literal_id(), data);
}

void FillFunctionData(Isolate* isolate, FunctionDataMap* map, Zone* frames_zone,
                      StackFrame** restart_frame) {
  {
    HeapIterator iterator(isolate->heap(), HeapIterator::kFilterUnreachable);
    while (HeapObject* obj = iterator.next()) {
      if (obj->IsSharedFunctionInfo()) {
        SharedFunctionInfo* sfi = SharedFunctionInfo::cast(obj);
        FunctionData* data = nullptr;
        if (!FunctionDataEntryForSharedFunctionInfo(map, sfi, &data)) continue;
        data->shared = handle(sfi, isolate);
      } else if (obj->IsJSFunction()) {
        JSFunction* js_function = JSFunction::cast(obj);
        SharedFunctionInfo* sfi = js_function->shared();
        FunctionData* data = nullptr;
        if (!FunctionDataEntryForSharedFunctionInfo(map, sfi, &data)) continue;
        data->js_functions.emplace_back(js_function, isolate);
      } else if (obj->IsJSGeneratorObject()) {
        JSGeneratorObject* gen = JSGeneratorObject::cast(obj);
        if (gen->is_closed()) continue;
        SharedFunctionInfo* sfi = gen->function()->shared();
        FunctionData* data = nullptr;
        if (!FunctionDataEntryForSharedFunctionInfo(map, sfi, &data)) continue;
        data->running_generators.emplace_back(gen, isolate);
      }
    }
  }
  Vector<StackFrame*> frames = CreateStackMap(isolate, frames_zone);
  FunctionData::StackPosition stack_position =
      isolate->debug()->break_frame_id() == StackFrame::NO_ID
          ? FunctionData::PATCHABLE
          : FunctionData::ABOVE_BREAK_FRAME;
  for (StackFrame* frame : frames) {
    if (stack_position == FunctionData::ABOVE_BREAK_FRAME) {
      if (frame->id() == isolate->debug()->break_frame_id()) {
        stack_position = FunctionData::PATCHABLE;
      }
    }
    if (stack_position == FunctionData::PATCHABLE &&
        (frame->is_exit() || frame->is_builtin_exit())) {
      stack_position = FunctionData::BELOW_NON_DROPPABLE_FRAME;
      continue;
    }
    if (!frame->is_java_script()) continue;
    std::vector<Handle<SharedFunctionInfo>> sfis;
    JavaScriptFrame::cast(frame)->GetFunctions(&sfis);
    for (auto& sfi : sfis) {
      if (stack_position == FunctionData::PATCHABLE &&
          IsResumableFunction(sfi->kind())) {
        stack_position = FunctionData::BELOW_NON_DROPPABLE_FRAME;
      }
      FunctionData* data = nullptr;
      if (!FunctionDataEntryForSharedFunctionInfo(map, *sfi, &data)) continue;
      if (!data->should_restart) continue;
      data->stack_position = stack_position;
      *restart_frame = frame;
    }
  }
}

bool CanPatchScript(const LiveEdit::LiteralMap& changed,
                    const LiveEdit::LiteralMap& moved, int script_id,
                    int new_script_id, FunctionDataMap* function_data_map,
                    debug::LiveEditResult* result) {
  debug::LiveEditResult::Status status = debug::LiveEditResult::OK;
  for (const auto& mapping : changed) {
    FunctionData* data = nullptr;
    FunctionDataEntry(function_data_map, script_id,
                      mapping.first->function_literal_id(), &data);
    FunctionData* new_data = nullptr;
    FunctionDataEntry(function_data_map, new_script_id,
                      mapping.second->function_literal_id(), &new_data);
    Handle<SharedFunctionInfo> sfi;
    if (!data->shared.ToHandle(&sfi)) {
      continue;
    } else if (data->stack_position == FunctionData::ABOVE_BREAK_FRAME) {
      status = debug::LiveEditResult::BLOCKED_BY_FUNCTION_ABOVE_BREAK_FRAME;
    } else if (data->stack_position ==
               FunctionData::BELOW_NON_DROPPABLE_FRAME) {
      status =
          debug::LiveEditResult::BLOCKED_BY_FUNCTION_BELOW_NON_DROPPABLE_FRAME;
    } else if (!data->running_generators.empty()) {
      status = debug::LiveEditResult::BLOCKED_BY_RUNNING_GENERATOR;
    } else if (!new_data->shared.ToHandle(&sfi)) {
      status = debug::LiveEditResult::BLOCKED_BY_ACTIVE_FUNCTION;
    }
    if (status != debug::LiveEditResult::OK) {
      result->status = status;
      return false;
    }
  }
  return true;
}

void UpdatePositions(Handle<SharedFunctionInfo> sfi,
                     const std::vector<SourceChangeRange>& changes) {
  int old_start_position = sfi->StartPosition();
  int new_start_position =
      LiveEdit::TranslatePosition(changes, old_start_position);
  int new_end_position =
      LiveEdit::TranslatePosition(changes, sfi->EndPosition());
  int new_function_token_position =
      LiveEdit::TranslatePosition(changes, sfi->function_token_position());
  sfi->set_raw_start_position(new_start_position);
  sfi->set_raw_end_position(new_end_position);
  sfi->set_function_token_position(new_function_token_position);
  if (sfi->scope_info()->HasPositionInfo()) {
    sfi->scope_info()->SetPositionInfo(new_start_position, new_end_position);
  }
  if (sfi->HasBytecodeArray()) {
    TranslateSourcePositionTable(handle(sfi->GetBytecodeArray()), changes);
  }
}
}  // anonymous namespace

void LiveEdit::PatchScript(Handle<Script> script, Handle<String> new_source,
                           debug::LiveEditResult* result) {
  // TODO(kozyatinskiy): add iterating through archived threads as well.
  Isolate* isolate = script->GetIsolate();

  std::vector<SourceChangeRange> changes;
  LiveEdit::CompareStrings(handle(String::cast(script->source()), isolate),
                           new_source, &changes);
  if (changes.empty()) {
    result->status = debug::LiveEditResult::OK;
    return;
  }

  CompileScriptHelper compile_script_copy(true, script);
  std::vector<FunctionLiteral*> literals;
  if (!compile_script_copy.GetLiterals(&literals, result)) return;

  Handle<Script> new_script = MakeScriptCopy(script, new_source);
  CompileScriptHelper compile_new_script(false, new_script);
  std::vector<FunctionLiteral*> new_literals;
  if (!compile_new_script.GetLiterals(&new_literals, result)) return;
  // TODO(kozyatinskiy): move to Compiler::CompileForLiveEdit and add the test.
  isolate->debug()->OnAfterCompile(new_script, true);

  FunctionLiteralChanges literal_changes;
  LiveEdit::CalculateFunctionLiteralChanges(literals, changes,
                                            &literal_changes);

  LiteralMap changed;
  LiteralMap source_changed;
  LiteralMap moved;
  LiveEdit::MapLiterals(literal_changes, new_literals, &changed,
                        &source_changed, &moved);

  FunctionDataMap function_data_map;
  StackFrame* restart_frame = nullptr;

  for (const auto& mapping : changed) {
    function_data_map[script->id()].emplace(
        mapping.first->function_literal_id(),
        FunctionData{mapping.first, true});
    function_data_map[new_script->id()].emplace(
        mapping.second->function_literal_id(),
        FunctionData{mapping.second, false});
  }
  for (const auto& mapping : source_changed) {
    function_data_map[script->id()].emplace(
        mapping.first->function_literal_id(),
        FunctionData{mapping.first, false});
  }
  for (const auto& mapping : moved) {
    function_data_map[script->id()].emplace(
        mapping.first->function_literal_id(),
        FunctionData{mapping.first, false});
  }
  Zone zone(isolate->allocator(), ZONE_NAME);
  FillFunctionData(isolate, &function_data_map, &zone, &restart_frame);
  if (!CanPatchScript(changed, moved, script->id(), new_script->id(),
                      &function_data_map, result)) {
    return;
  }
  if (restart_frame && restart_frame->is_java_script()) {
    std::vector<Handle<SharedFunctionInfo>> sfis;
    JavaScriptFrame::cast(restart_frame)->GetFunctions(&sfis);
    for (auto& sfi : sfis) {
      FunctionData* data = nullptr;
      if (!FunctionDataEntryForSharedFunctionInfo(&function_data_map, *sfi,
                                                  &data)) {
        continue;
      }
      auto mapping_it = changed.find(data->literal);
      if (mapping_it == changed.end()) continue;
      if (mapping_it->second->scope()->new_target_var()) {
        result->status =
            debug::LiveEditResult::BLOCKED_BY_NEW_TARGET_IN_RESTART_FRAME;
        return;
      }
    }
  }
  for (const auto& mapping : moved) {
    FunctionData* data = nullptr;
    if (!FunctionDataEntry(&function_data_map, script->id(),
                           mapping.first->function_literal_id(), &data)) {
      continue;
    }
    Handle<SharedFunctionInfo> sfi;
    if (!data->shared.ToHandle(&sfi)) continue;
    UpdatePositions(sfi, changes);
    Handle<WeakFixedArray> list =
        handle(new_script->shared_function_infos(), isolate);
    sfi->set_function_literal_id(mapping.second->function_literal_id());
    sfi->set_script(*new_script);
    list->Set(mapping.second->function_literal_id(),
              HeapObjectReference::Weak(*sfi));
    if (sfi->HasPreParsedScopeData()) sfi->ClearPreParsedScopeData();
    if (sfi->HasBreakInfo()) {
      isolate->debug()->RemoveBreakInfoAndMaybeFree(
          handle(sfi->GetDebugInfo()));
    }
  }
  for (const auto& mapping : source_changed) {
    FunctionData* data = nullptr;
    if (!FunctionDataEntry(&function_data_map, script->id(),
                           mapping.first->function_literal_id(), &data)) {
      continue;
    }
    Handle<SharedFunctionInfo> sfi;
    if (!data->shared.ToHandle(&sfi)) continue;
    isolate->debug()->DeoptimizeFunction(sfi);
    UpdatePositions(sfi, changes);
    if (sfi->HasBreakInfo()) {
      isolate->debug()->RemoveBreakInfoAndMaybeFree(
          handle(sfi->GetDebugInfo()));
    }
    Handle<WeakFixedArray> list =
        handle(new_script->shared_function_infos(), isolate);
    sfi->set_function_literal_id(mapping.second->function_literal_id());
    sfi->set_script(*new_script);
    list->Set(mapping.second->function_literal_id(),
              HeapObjectReference::Weak(*sfi));
    if (sfi->HasPreParsedScopeData()) sfi->ClearPreParsedScopeData();
    if (!sfi->HasBytecodeArray()) continue;
    Handle<BytecodeArray> bytecode(sfi->GetBytecodeArray(), isolate);
    Handle<FixedArray> constants(bytecode->constant_pool(), isolate);
    for (int i = 0; i < constants->length(); ++i) {
      if (!constants->get(i)->IsSharedFunctionInfo()) continue;
      Handle<SharedFunctionInfo> sfi_constant(
          SharedFunctionInfo::cast(constants->get(i)), isolate);
      FunctionData* data = nullptr;
      if (!FunctionDataEntryForSharedFunctionInfo(&function_data_map,
                                                  *sfi_constant, &data)) {
        continue;
      }
      auto mapping_it = changed.find(data->literal);
      if (mapping_it == changed.end()) continue;
      FunctionData* repalcement_data = nullptr;
      if (!FunctionDataEntry(&function_data_map, new_script->id(),
                             mapping_it->second->function_literal_id(),
                             &repalcement_data)) {
        continue;
      }
      Handle<SharedFunctionInfo> replacement;
      if (!repalcement_data->shared.ToHandle(&replacement)) continue;
      constants->set(i, *replacement);
    }
    for (auto& js_function : data->js_functions) {
      js_function->set_feedback_cell(*isolate->factory()->many_closures_cell());
      if (!js_function->is_compiled()) continue;
      JSFunction::EnsureFeedbackVector(js_function);
    }
  }
  for (const auto& mapping : changed) {
    FunctionData* data = nullptr;
    if (!FunctionDataEntry(&function_data_map, script->id(),
                           mapping.first->function_literal_id(), &data)) {
      continue;
    }
    Handle<SharedFunctionInfo> sfi;
    if (!data->shared.ToHandle(&sfi)) continue;
    FunctionData* new_data = nullptr;
    if (!FunctionDataEntry(&function_data_map, new_script->id(),
                           mapping.second->function_literal_id(), &new_data)) {
      continue;
    }
    Handle<SharedFunctionInfo> new_sfi;
    if (!new_data->shared.ToHandle(&new_sfi)) continue;
    isolate->debug()->DeoptimizeFunction(sfi);
    isolate->compilation_cache()->Remove(sfi);
    for (auto& js_function : data->js_functions) {
      js_function->set_shared(*new_sfi);
      js_function->set_feedback_cell(*isolate->factory()->many_closures_cell());
      if (!js_function->is_compiled()) continue;
      JSFunction::EnsureFeedbackVector(js_function);
    }
  }
  if (restart_frame) {
    result->stack_changed = true;
    isolate->debug()->ScheduleFrameRestart(restart_frame);
  }
  result->status = debug::LiveEditResult::OK;
}

void LiveEdit::CompareStrings(Handle<String> a, Handle<String> b,
                              std::vector<SourceChangeRange>* changes) {
  *changes = CompareSources(a, b);
}

int LiveEdit::TranslatePosition(const std::vector<SourceChangeRange>& changes,
                                int position) {
  auto it = std::lower_bound(changes.begin(), changes.end(), position,
                             [](const SourceChangeRange& change, int position) {
                               return change.end_position < position;
                             });
  if (it != changes.end() && position == it->end_position) {
    return it->new_end_position;
  }
  if (it == changes.begin()) return position;
  DCHECK(it == changes.end() || position <= it->start_position);
  it = std::prev(it);
  return position + (it->new_end_position - it->end_position);
}

const char* LiveEdit::RestartFrame(JavaScriptFrame* frame) {
  if (!LiveEdit::kFrameDropperSupported) return "Not supported by arch";
  Isolate* isolate = frame->isolate();
  Zone zone(isolate->allocator(), ZONE_NAME);
  Vector<StackFrame*> frames = CreateStackMap(isolate, &zone);
  StackFrame::Id break_frame_id = isolate->debug()->break_frame_id();
  bool break_frame_found = break_frame_id == StackFrame::NO_ID;
  for (StackFrame* current : frames) {
    break_frame_found = break_frame_found || break_frame_id == current->id();
    if (current->fp() == frame->fp()) {
      if (break_frame_found) {
        isolate->debug()->ScheduleFrameRestart(current);
        return nullptr;
      } else {
        return "Frame is below break frame";
      }
    }
    if (!break_frame_found) continue;
    if (current->is_exit() || current->is_builtin_exit()) {
      return "Function is blocked under native code";
    }
    if (!current->is_java_script()) continue;
    std::vector<Handle<SharedFunctionInfo>> shareds;
    JavaScriptFrame::cast(current)->GetFunctions(&shareds);
    for (auto& shared : shareds) {
      if (IsResumableFunction(shared->kind())) {
        return "Function is blocked under a generator activation";
      }
    }
  }
  return "Frame not found";
}
}  // namespace internal
}  // namespace v8
