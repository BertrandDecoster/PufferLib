// Copyright 2024
// TagTable - per-env interning of opaque tag names ("burning", "wet", ...).
// The env gives tags no meaning; ids are stable for an env's lifetime only,
// so anything persisted (snapshots, the C API's callers) uses names.

#ifndef COMPANIONS_CORE_TAG_TABLE_H_
#define COMPANIONS_CORE_TAG_TABLE_H_

#include <string>
#include <vector>

#include "types.h"

namespace companions {

class TagTable {
 public:
  // Id of `name`, registering it on first use.
  TagId Intern(const std::string& name);
  // Id of `name`, or kInvalidTag if it was never interned.
  TagId Find(const std::string& name) const;
  // Name of `id`, or "" if unknown.
  const std::string& Name(TagId id) const;
  int Size() const { return static_cast<int>(names_.size()); }

 private:
  std::vector<std::string> names_;
};

}  // namespace companions

#endif  // COMPANIONS_CORE_TAG_TABLE_H_
