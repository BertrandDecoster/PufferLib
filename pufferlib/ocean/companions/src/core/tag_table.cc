// Copyright 2024
#include "tag_table.h"

#include <algorithm>

namespace companions {

TagId TagTable::Intern(const std::string& name) {
  TagId id = Find(name);
  if (id != kInvalidTag) return id;
  names_.push_back(name);
  return static_cast<TagId>(names_.size() - 1);
}

TagId TagTable::Find(const std::string& name) const {
  auto it = std::find(names_.begin(), names_.end(), name);
  return it == names_.end() ? kInvalidTag
                            : static_cast<TagId>(it - names_.begin());
}

const std::string& TagTable::Name(TagId id) const {
  static const std::string kEmpty;
  if (id < 0 || id >= Size()) return kEmpty;
  return names_[static_cast<size_t>(id)];
}

}  // namespace companions
