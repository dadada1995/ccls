/* Copyright 2017-2018 ccls Authors

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

  http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/

#pragma once

#include <optional>
#include <string_view>

#include <iterator>
#include <string>
#include <vector>

namespace llvm {
class StringRef;
}

namespace ccls {
uint64_t HashUsr(std::string_view s);
uint64_t HashUsr(llvm::StringRef s);

// Returns true if |value| starts/ends with |start| or |ending|.
bool StartsWith(std::string_view value, std::string_view start);
bool EndsWith(std::string_view value, std::string_view ending);
bool EndsWithAny(std::string_view s, const std::vector<std::string> &ss);
bool FindAnyPartial(const std::string &value,
                    const std::vector<std::string> &values);

std::vector<std::string> SplitString(const std::string &str,
                                     const std::string &delimiter);

std::string LowerPathIfInsensitive(const std::string &path);

// Ensures that |path| ends in a slash.
void EnsureEndsInSlash(std::string &path);

// Converts a file path to one that can be used as filename.
// e.g. foo/bar.c => foo_bar.c
std::string EscapeFileName(std::string path);

std::string ResolveIfRelative(const std::string &directory,
                              const std::string &path);

std::optional<int64_t> LastWriteTime(const std::string &path);
std::optional<std::string> ReadContent(const std::string &filename);
void WriteToFile(const std::string &filename, const std::string &content);

int ReverseSubseqMatch(std::string_view pat, std::string_view text,
                       int case_sensitivity);

// http://stackoverflow.com/a/38140932
//
//  struct SomeHashKey {
//    std::string key1;
//    std::string key2;
//    bool key3;
//  };
//  MAKE_HASHABLE(SomeHashKey, t.key1, t.key2, t.key3)

inline void hash_combine(std::size_t &seed) {}

template <typename T, typename... Rest>
inline void hash_combine(std::size_t &seed, const T &v, Rest... rest) {
  std::hash<T> hasher;
  seed ^= hasher(v) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
  hash_combine(seed, rest...);
}

#define MAKE_HASHABLE(type, ...)                                               \
  namespace std {                                                              \
  template <> struct hash<type> {                                              \
    std::size_t operator()(const type &t) const {                              \
      std::size_t ret = 0;                                                     \
      ccls::hash_combine(ret, __VA_ARGS__);                                          \
      return ret;                                                              \
    }                                                                          \
  };                                                                           \
  }

std::string GetDefaultResourceDirectory();
} // namespace ccls
