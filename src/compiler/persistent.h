// Copyright 2017 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_PERSISTENT_H_
#define V8_COMPILER_PERSISTENT_H_

#include <array>
#include <bitset>
#include <tuple>

#include "src/base/functional.h"
#include "src/zone/zone-containers.h"

namespace v8 {
namespace internal {
namespace compiler {

template <class T, int n>
struct array : public std::array<T, n> {
  T& operator[](size_t pos) {
    DCHECK(pos >= 0 && pos < n);
    return std::array<T, n>::operator[](pos);
  }
  const T& operator[](size_t pos) const {
    DCHECK(pos >= 0 && pos < n);
    return std::array<T, n>::operator[](pos);
  }
};

struct FinishedIterator {};

// PersistentMap is a persistent map datastructure based on hash trees (a binary
// tree using the bits of a hash value as addresses).
// Complexity:
// - Copy and assignment: O(1)
// - access: O(log n)
// - update: O(log n) time and space
// - iteration: amortized O(1) per step
// - Zip: O(n) + O(m * log n) where m is the number of elements where the result
//        differs from the first map.
// - equality check: O(n)
template <class Key, class Value, class Hash = base::hash<Key>>
class PersistentMap {
 public:
  using key_type = Key;
  using mapped_type = Value;
  using value_type = std::pair<Key, Value>;

 private:
  static constexpr size_t kHashBits = 32;
  struct hash_t : private std::bitset<kHashBits> {
    using Base = std::bitset<kHashBits>;
    explicit hash_t(size_t hash) : Base(hash) {}
    explicit hash_t(Base hash) : Base(hash) {}

    bool operator[](int pos) const {
      return Base::operator[](kHashBits - pos - 1);
    }

    bool operator<(hash_t other) const {
      static_assert(sizeof(*this) <= sizeof(unsigned long), "");  // NOLINT
      return this->to_ulong() < other.to_ulong();
    }
    bool operator==(hash_t other) const {
      return this->Base::operator==(other);
    }
    bool operator!=(hash_t other) const {
      return this->Base::operator!=(other);
    }
    hash_t operator^(hash_t other) const {
      return hash_t(static_cast<Base>(*this) ^ static_cast<Base>(other));
    }
  };

  struct KeyValue : std::pair<Key, Value> {
    const Key& key() const { return this->first; }
    const Value& value() const { return this->second; }
    template <class K, class V>
    KeyValue(K&& key, V&& value)
        : std::pair<Key, Value>(std::forward<K>(key), std::forward<V>(value)) {}
  };

  struct Map {
    KeyValue key_value;
    int8_t length;
    hash_t key_hash;
    const ZoneMap<Key, Value>* more;
    const Map* first_path;  // Flexible array member.
    const Map*& path(int i) {
      return reinterpret_cast<const Map**>(reinterpret_cast<byte*>(this) +
                                           offsetof(Map, first_path))[i];
    }
    const Map* path(int i) const {
      return reinterpret_cast<const Map**>(
          reinterpret_cast<byte*>(const_cast<Map*>(this)) +
          offsetof(Map, first_path))[i];
    }
  };

  using more_iterator = typename ZoneMap<Key, Value>::const_iterator;

  struct Accessor {
    PersistentMap& map;
    const Key& key;
    PersistentMap& operator=(Value value) const {
      return map = map.Add(key, value);
    }
    operator Value() const { return map.Get(key); }
  };

 public:
  size_t last_depth() const {
    if (map_) {
      return map_->length;
    } else {
      return 0;
    }
  }

  const Value& Get(const Key& key) const {
    hash_t key_hash = hash_t(Hash()(key));
    const Map* map = FindHash(key_hash);
    return GetFromRoot(map, key);
  }

  PersistentMap Add(Key key, Value value) const {
    hash_t key_hash = hash_t(Hash()(key));
    array<const Map*, kHashBits> path;
    int length = 0;
    const Map* old = FindHash(key_hash, &path, &length);
    ZoneMap<Key, Value>* more = nullptr;
    if (GetFromRoot(old, key) == value) return *this;
    if (old && !(old->more == nullptr && old->key_value.key() == key)) {
      more = new (zone_->New(sizeof(*more))) ZoneMap<Key, Value>(zone_);
      if (old->more) {
        *more = *old->more;
      } else {
        (*more)[old->key_value.key()] = old->key_value.value();
      }
      (*more)[key] = value;
    }
    Map* map = new (zone_->New(sizeof(Map) + length * sizeof(const Map*)))
        Map{KeyValue(std::move(key), std::move(value)), length, key_hash, more};
    for (int i = 0; i < length; ++i) {
      map->path(i) = path[i];
    }
    return PersistentMap(map, zone_, def_value_);
  }

  Accessor operator[](const Key& key) { return Accessor{*this, key}; }

  const Value& operator[](const Key& key) const { return Get(key); }

  bool operator==(const PersistentMap& other) const {
    if (map_ == other.map_) return true;
    if (def_value_ != other.def_value_) return false;
    for (const auto& triple : Zip(other)) {
      if (std::get<1>(triple) != std::get<2>(triple)) return false;
    }
    return true;
  }

  bool operator!=(const PersistentMap& other) const {
    return !(*this == other);
  }

  class iterator {
   public:
    const value_type operator*() const {
      if (current_->more) {
        return *more_iter_;
      } else {
        return current_->key_value;
      }
    }

    iterator& operator++() {
      if (!current_) {
        // Iterator is past the end.
        return *this;
      }
      if (current_->more) {
        DCHECK(more_iter_ != current_->more->end());
        ++more_iter_;
        if (more_iter_ != current_->more->end()) return *this;
      }
      if (shift_ == 0) {
        *this = end(def_value_);
        return *this;
      }
      --shift_;
      while (current_->key_hash[shift_] == 1 || path_[shift_] == nullptr) {
        if (shift_ == 0) {
          *this = end(def_value_);
          return *this;
        }
        --shift_;
      }
      const Map* first_right_alternative = path_[shift_];
      shift_++;
      current_ = FindLeftmost(first_right_alternative, &shift_, &path_);
      if (current_->more) {
        more_iter_ = current_->more->begin();
      }
      return *this;
    }

    bool operator==(const iterator& other) const {
      if (!current_) return !other.current_;
      if (!other.current_) return false;
      if (current_->key_hash != other.current_->key_hash) {
        return false;
      } else {
        return (**this).first == (*other).first;
      }
    }
    bool operator!=(const iterator& other) const { return !(*this == other); }

    bool operator<(const iterator& other) const {
      if (!current_) return false;
      if (!other.current_) return true;
      if (current_->key_hash < other.current_->key_hash) {
        return true;
      } else if (current_->key_hash == other.current_->key_hash) {
        return (**this).first < (*other).first;
      } else {
        return false;
      }
    }

    bool has_next() const { return current_ != nullptr; }

    const Value& def_value() { return def_value_; }

    static iterator begin(const Map* map, Value def_value) {
      iterator i(def_value);
      i.current_ = FindLeftmost(map, &i.shift_, &i.path_);
      if (i.current_->more) {
        i.more_iter_ = i.current_->more->begin();
      }
      return i;
    }

    static iterator end(Value def_value) { return iterator(def_value); }

   private:
    int shift_;
    more_iterator more_iter_;
    const Map* current_;
    array<const Map*, kHashBits> path_;
    Value def_value_;

    explicit iterator(Value def_value)
        : shift_(0), current_(nullptr), def_value_(def_value) {}
  };

  iterator begin() const {
    if (!map_) return end();
    return iterator::begin(map_, def_value_);
  }
  iterator end() const { return iterator::end(def_value_); }

  class double_iterator {
   public:
    std::tuple<Key, Value, Value> operator*() {
      if (first_current_) {
        auto pair = *first_;
        return std::make_tuple(
            pair.first, pair.second,
            second_current_ ? (*second_).second : second_.def_value());
      } else {
        DCHECK(second_current_);
        auto pair = *second_;
        return std::make_tuple(pair.first, first_.def_value(), pair.second);
      }
    }

    double_iterator& operator++() {
      if (first_current_) ++first_;
      if (second_current_) ++second_;
      return *this = double_iterator(first_, second_);
    }

    double_iterator(iterator first, iterator second)
        : first_(first), second_(second) {
      if (first_ == second_) {
        first_current_ = second_current_ = true;
      } else if (first_ < second_) {
        first_current_ = true;
        second_current_ = false;
      } else {
        first_current_ = false;
        second_current_ = true;
      }
    }

    bool operator!=(const double_iterator& other) {
      return first_ != other.first_ || second_ != other.second_;
    }

    bool has_next() const { return first_.has_next() || second_.has_next(); }

   private:
    iterator first_;
    iterator second_;
    bool first_current_;
    bool second_current_;
  };

  struct Zipper {
    PersistentMap a;
    PersistentMap b;

    double_iterator begin() { return double_iterator(a.begin(), b.begin()); }

    double_iterator end() { return double_iterator(a.end(), b.end()); }
  };

  Zipper Zip(const PersistentMap& other) const { return {*this, other}; }

  PersistentMap Join(const PersistentMap& other) const {
    PersistentMap result = *this;
    result.def_value_ = def_value_.Join(other.def_value_);
    for (auto triple : this->Zip(other)) {
      result[std::get<0>(triple)] =
          std::get<1>(triple).Join(std::get<2>(triple));
    }
    return result;
  }

  explicit PersistentMap(Zone* zone, Value def_value = Value())
      : PersistentMap(nullptr, zone, def_value) {}

 private:
  const Map* FindHash(hash_t hash, array<const Map*, kHashBits>* path,
                      int* length) const {
    const Map* map = map_;
    int shift = 0;
    while (map && hash != map->key_hash) {
      int map_length = map->length;
      while ((hash ^ map->key_hash)[shift] == 0) {
        (*path)[shift] = shift < map_length ? map->path(shift) : nullptr;
        ++shift;
      }
      (*path)[shift] = map;
      map = shift < map->length ? map->path(shift) : nullptr;
      ++shift;
    }
    if (map) {
      while (shift < map->length) {
        (*path)[shift] = map->path(shift);
        ++shift;
      }
    }
    *length = shift;
    return map;
  }

  const Map* FindHash(hash_t hash) const {
    const Map* map = map_;
    int shift = 0;
    while (map && hash != map->key_hash) {
      while ((hash ^ map->key_hash)[shift] == 0) {
        ++shift;
      }
      map = shift < map->length ? map->path(shift) : nullptr;
      ++shift;
    }
    return map;
  }

  const Value& GetFromRoot(const Map* map, const Key& key) const {
    if (!map) {
      return def_value_;
    }
    if (map->more) {
      auto it = map->more->find(key);
      if (it == map->more->end())
        return def_value_;
      else
        return it->second;
    } else {
      if (key == map->key_value.key()) {
        return map->key_value.value();
      } else {
        return def_value_;
      }
    }
  }

  static const Map* MapChild(const Map* map, int shift, bool bit) {
    if (map->key_hash[shift] == bit) {
      return map;
    } else if (shift < map->length) {
      return map->path(shift);
    } else {
      return nullptr;
    }
  }

  static const Map* FindLeftmost(const Map* start, int* shift,
                                 array<const Map*, kHashBits>* path) {
    const Map* current = start;
    while (*shift < current->length) {
      if (const Map* child = MapChild(current, *shift, 0)) {
        (*path)[*shift] = MapChild(current, *shift, 1);
        current = child;
        ++*shift;
      } else if (const Map* child = MapChild(current, *shift, 1)) {
        (*path)[*shift] = MapChild(current, *shift, 0);
        current = child;
        ++*shift;
      } else {
        UNREACHABLE();
      }
    }
    return current;
  }

  PersistentMap(const Map* map, Zone* zone, Value def_value)
      : map_(map), def_value_(def_value), zone_(zone) {}

  const Map* map_;
  Value def_value_;
  Zone* zone_;
};

template <class Key, class Value, class Hash>
std::ostream& operator<<(std::ostream& os,
                         const PersistentMap<Key, Value, Hash>& map) {
  os << "{";
  bool first = true;
  for (auto pair : map) {
    if (!first) os << ", ";
    first = false;
    os << pair.first << ": " << pair.second;
  }
  return os << "}";
}

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_PERSISTENT_H_
