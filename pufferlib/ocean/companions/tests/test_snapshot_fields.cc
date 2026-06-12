// Copyright 2024
// Per-field serialization tests generated off the VisitFields lists
// (snapshot.h). Three reflection visitors:
//
//  - UniqueFiller: assigns every visited field a distinct value from an
//    offset counter (enum-typed fields get distinct VALID values, so the
//    JSON string round-trip is exercised honestly).
//  - FieldDump: flattens a snapshot into an ordered (field path, value
//    string) list.
//  - FieldCounter: counts visited fields per struct; the count assertions
//    are the cross-platform half of the drift guard (the sizeof
//    static_asserts in snapshot.h cover macOS arm64 at compile time).
//
// The round-trip tests serialize a filled snapshot through each format and
// compare dumps: a forgotten read/write path FAILS NAMING THE FIELD (e.g.
// "agents[1].fsm.attack_damage"), not just "snapshots differ".

#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../src/core/annotations.h"
#include "../src/core/cell.h"
#include "../src/core/object.h"
#include "../src/core/snapshot.h"
#include "../src/core/snapshot_json.h"
#include "../src/core/types.h"

using namespace companions;

// =============================================================================
// Test macros
// =============================================================================
#define TEST(name) \
  void name(); \
  struct name##_registrar { \
    name##_registrar() { tests.push_back({#name, name}); } \
  } name##_instance; \
  void name()

#define ASSERT_TRUE(cond) \
  if (!(cond)) { \
    std::ostringstream oss; \
    oss << "ASSERT_TRUE failed: " << #cond << " at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

#define ASSERT_EQ(a, b) \
  if ((a) != (b)) { \
    std::ostringstream oss; \
    oss << "ASSERT_EQ failed: " << #a << " (" << (a) << ") != " << #b \
        << " (" << (b) << ") at " << __FILE__ << ":" << __LINE__; \
    throw std::runtime_error(oss.str()); \
  }

struct TestEntry {
  std::string name;
  void (*func)();
};
std::vector<TestEntry> tests;

// =============================================================================
// FieldCounter - counts direct fields of one struct (no recursion)
// =============================================================================
struct FieldCounter {
  int count = 0;
  template <typename... Args>
  void operator()(Args&&...) {
    ++count;
  }
};

template <class T>
int CountFields() {
  T instance{};
  FieldCounter counter;
  T::VisitFields(instance, counter);
  return counter.count;
}

// =============================================================================
// UniqueFiller - distinct value per field, valid for both wire formats
// =============================================================================
struct UniqueFiller {
  int counter = 1000;

  int Next() { return counter++; }

  // Scalars and nested visitable structs.
  template <class T>
  void operator()(const char* name, T& value, bool = false) {
    if constexpr (kHasVisitFields<T>) {
      T::VisitFields(value, *this);
    } else if constexpr (std::is_same_v<T, bool>) {
      value = (Next() % 2) == 0;
    } else if constexpr (std::is_same_v<T, std::string>) {
      value = "str_" + std::to_string(Next());
    } else {
      (void)name;
      value = static_cast<T>(Next());
    }
  }

  void operator()(const char*, Position& p, bool = false) {
    p.row = Next();
    p.col = Next();
  }

  // Enum-typed fields: distinct VALID values (JSON round-trips via string
  // names; an out-of-range value would not survive and would mask bugs).
  void operator()(const char*, FSMStateType& v, bool = false) {
    static const FSMStateType kValues[] = {
        FSMStateType::Patrol,    FSMStateType::Aggro,
        FSMStateType::Telegraph, FSMStateType::Attack,
        FSMStateType::Recovery,  FSMStateType::ReturnToPatrol};
    v = kValues[Next() % 6];
  }
  void operator()(const char*, TargetFilter& v, bool = false) {
    v = static_cast<TargetFilter>(Next() % 4);
  }
  void operator()(const char*, SemanticTag& v, bool = false) {
    v = static_cast<SemanticTag>(Next() % static_cast<int>(SemanticTag::_Count));
  }
  template <class IntT>
  void operator()(const char*, EnumFieldRef<ObjectType, IntT> f, bool = false) {
    f.value = Next() % 7;  // Object..NPCCompanion all round-trip
  }
  template <class IntT>
  void operator()(const char*, EnumFieldRef<Faction, IntT> f, bool = false) {
    f.value = Next() % 3;
  }
  template <class IntT>
  void operator()(const char*, EnumFieldRef<Direction, IntT> f, bool = false) {
    f.value = Next() % 4;
  }
  template <class IntT>
  void operator()(const char*, EnumFieldRef<ActorColor, IntT> f, bool = false) {
    f.value = Next() % 4;
  }
  template <class IntT>
  void operator()(const char*, EnumFieldRef<StatusType, IntT> f, bool = false) {
    f.value = Next() % 4;
  }

  template <class BoolT, class FsmT>
  void operator()(const char* name, GuardedFsmRef<BoolT, FsmT> f, bool = false) {
    f.has = true;  // fill FSM so its fields are exercised
    FSMSnapshot::VisitFields(f.fsm, *this);
    (void)name;
  }

  // Vectors: two distinct elements each.
  template <class T>
  void operator()(const char* name, std::vector<T>& vec, bool = false) {
    vec.clear();
    vec.resize(2);
    for (T& item : vec) {
      (*this)(name, item);
    }
  }

  // Cells and annotations have format constraints (grid-shaped array, valid
  // enum strings, conditional pos/agent_id) - filled manually by the test.
  void operator()(const char*, std::vector<CellSnapshot>&, bool = false) {}
  void operator()(const char*, std::vector<AnnotationSnapshot>&, bool = false) {}
  void operator()(const char*,
                  std::vector<std::pair<std::string, std::string>>& params,
                  bool = false) {
    // Alphabetical keys: JSON objects re-emit keys sorted, so non-sorted
    // insertion order would not round-trip identically.
    params.clear();
    params.emplace_back("ka", "v" + std::to_string(Next()));
    params.emplace_back("kb", "v" + std::to_string(Next()));
  }
};

// =============================================================================
// FieldDump - flatten into ordered (path, value) pairs
// =============================================================================
struct FieldDump {
  std::string prefix;
  std::vector<std::pair<std::string, std::string>>* out;

  void Add(const std::string& name, const std::string& value) {
    out->push_back({prefix + name, value});
  }

  template <class T>
  void operator()(const char* name, const T& value, bool = false) {
    if constexpr (kHasVisitFields<T>) {
      FieldDump sub{prefix + name + ".", out};
      T::VisitFields(value, sub);
    } else if constexpr (std::is_same_v<T, std::string>) {
      Add(name, value);
    } else {
      std::ostringstream oss;
      oss << +value;  // promote (u)int8 to printable int
      Add(name, oss.str());
    }
  }

  void operator()(const char* name, const Position& p, bool = false) {
    Add(name, "(" + std::to_string(p.row) + "," + std::to_string(p.col) + ")");
  }
  void operator()(const char* name, const FSMStateType& v, bool = false) {
    Add(name, std::to_string(static_cast<int>(v)));
  }
  void operator()(const char* name, const TargetFilter& v, bool = false) {
    Add(name, std::to_string(static_cast<int>(v)));
  }
  void operator()(const char* name, const SemanticTag& v, bool = false) {
    Add(name, std::to_string(static_cast<int>(v)));
  }
  template <class E, class IntT>
  void operator()(const char* name, EnumFieldRef<E, IntT> f, bool = false) {
    Add(name, std::to_string(f.value));
  }

  template <class BoolT, class FsmT>
  void operator()(const char* name, GuardedFsmRef<BoolT, FsmT> f, bool = false) {
    Add(std::string(name) + ".has_fsm", f.has ? "1" : "0");
    if (f.has) {
      // Skip FSM contents when absent: they are not serialized.
      FieldDump sub{prefix + name + ".", out};
      FSMSnapshot::VisitFields(f.fsm, sub);
    }
  }

  template <class T>
  void operator()(const char* name, const std::vector<T>& vec, bool = false) {
    Add(std::string(name) + ".size", std::to_string(vec.size()));
    for (size_t i = 0; i < vec.size(); ++i) {
      FieldDump sub{prefix + name + "[" + std::to_string(i) + "]", out};
      if constexpr (kHasVisitFields<T>) {
        sub.prefix += ".";
        T::VisitFields(vec[i], sub);
      } else {
        sub.Add("", [&] {
          std::ostringstream oss;
          if constexpr (std::is_same_v<T, Position>) {
            oss << "(" << vec[i].row << "," << vec[i].col << ")";
          } else if constexpr (std::is_same_v<T, std::string>) {
            oss << vec[i];
          } else {
            oss << +vec[i];
          }
          return oss.str();
        }());
      }
    }
  }

  void operator()(const char* name, const std::vector<CellSnapshot>& cells,
                  bool = false) {
    Add(std::string(name) + ".size", std::to_string(cells.size()));
    for (size_t i = 0; i < cells.size(); ++i) {
      Add(std::string(name) + "[" + std::to_string(i) + "]",
          std::to_string(static_cast<int>(cells[i].kind)) + "/" +
              std::to_string(static_cast<int>(cells[i].origin)));
    }
  }

  void operator()(const char* name,
                  const std::vector<std::pair<std::string, std::string>>& params,
                  bool = false) {
    Add(std::string(name) + ".size", std::to_string(params.size()));
    for (size_t i = 0; i < params.size(); ++i) {
      Add(std::string(name) + "[" + std::to_string(i) + "]",
          params[i].first + "=" + params[i].second);
    }
  }
};

std::vector<std::pair<std::string, std::string>> Dump(const Snapshot& s) {
  std::vector<std::pair<std::string, std::string>> out;
  FieldDump dump{"", &out};
  Snapshot::VisitFields(s, dump);
  return out;
}

// Compares dumps and names the FIRST DIFFERING FIELD on failure.
void AssertDumpsEqual(const std::vector<std::pair<std::string, std::string>>& a,
                      const std::vector<std::pair<std::string, std::string>>& b,
                      const char* format_name) {
  size_t n = a.size() < b.size() ? a.size() : b.size();
  for (size_t i = 0; i < n; ++i) {
    if (a[i].first != b[i].first || a[i].second != b[i].second) {
      std::ostringstream oss;
      oss << format_name << " round-trip lost field '" << a[i].first
          << "': original=" << a[i].second << ", restored field '"
          << b[i].first << "'=" << b[i].second;
      throw std::runtime_error(oss.str());
    }
  }
  if (a.size() != b.size()) {
    std::ostringstream oss;
    oss << format_name << " round-trip changed field count: original "
        << a.size() << " fields, restored " << b.size()
        << "; first extra field: '"
        << (a.size() > b.size() ? a[n].first : b[n].first) << "'";
    throw std::runtime_error(oss.str());
  }
}

// =============================================================================
// Filled snapshot: filler for everything; cells/annotations/has_fsm corners
// adjusted manually to satisfy format constraints.
// =============================================================================
Snapshot MakeFilledSnapshot() {
  Snapshot s;
  UniqueFiller filler;
  Snapshot::VisitFields(s, filler);

  // Grid must be consistent (JSON reconstructs cells from rows*cols).
  s.rows = 2;
  s.cols = 3;
  s.cells.resize(6);
  for (int i = 0; i < 6; ++i) {
    s.cells[i].kind = static_cast<CellKind>(i % 4);
    s.cells[i].origin = static_cast<CellOrigin>((i + 1) % 4);
  }

  // One agent WITHOUT an FSM block (the null branch of "fsm").
  ASSERT_TRUE(s.agents.size() == 2u);
  s.agents[1].has_fsm = false;

  // Annotations: conditional shape needs exactly one of pos/agent_id (the
  // unused half stays default; JSON does not carry it).
  AnnotationSnapshot cell_ann;
  cell_ann.target_type = 0;
  cell_ann.pos = {1, 2};
  cell_ann.agent_id = kInvalidObjectId;
  cell_ann.tag = SemanticTag::SynchroGoal;
  cell_ann.params = {{"alpha", "a1"}, {"beta", "b2"}};
  cell_ann.owner_lens_id = 11;
  s.annotations.push_back(cell_ann);

  AnnotationSnapshot agent_ann;
  agent_ann.target_type = 1;
  agent_ann.pos = {-1, -1};
  agent_ann.agent_id = 42;
  agent_ann.tag = SemanticTag::HtnName;
  agent_ann.params = {{"name", "warden"}};
  agent_ann.owner_lens_id = -1;
  s.annotations.push_back(agent_ann);

  return s;
}

// =============================================================================
// Tests
// =============================================================================

// Cross-platform drift guard: exact field count per struct. If this fails
// you added/removed a field in VisitFields — update the golden fixtures
// (under a version bump if the wire format changed) and this count.
TEST(TestFieldCounts) {
  ASSERT_EQ(CountFields<StatusSnapshot>(), 2);
  ASSERT_EQ(CountFields<FSMSnapshot>(), 15);
  ASSERT_EQ(CountFields<AgentSnapshot>(), 15);
  ASSERT_EQ(CountFields<EffectSnapshot>(), 10);
  ASSERT_EQ(CountFields<AnnotationSnapshot>(), 6);
  ASSERT_EQ(CountFields<Snapshot>(), 12);
}

// The filler must give counter-valued leaves distinct values; a collision
// would let a "swapped fields" bug pass the round-trip tests undetected.
// (Bools, enum-range values and vector sizes legitimately repeat; counter
// values are 4-digit numbers in [1000, 1999].)
TEST(TestFillerProducesDistinctValues) {
  Snapshot s = MakeFilledSnapshot();
  auto dump = Dump(s);
  ASSERT_TRUE(dump.size() > 100u);  // deeply populated

  auto is_counter_valued = [](const std::string& v) {
    return v.size() == 4 && v[0] == '1' &&
           v.find_first_not_of("0123456789") == std::string::npos;
  };
  for (size_t i = 0; i < dump.size(); ++i) {
    if (!is_counter_valued(dump[i].second)) continue;
    for (size_t j = i + 1; j < dump.size(); ++j) {
      if (dump[i].second == dump[j].second) {
        throw std::runtime_error("Filler collision on fields '" +
                                 dump[i].first + "' and '" + dump[j].first +
                                 "' (value " + dump[i].second + ")");
      }
    }
  }
}

TEST(TestBinaryRoundTripPerField) {
  Snapshot original = MakeFilledSnapshot();
  Snapshot restored = Snapshot::Deserialize(original.Serialize());
  AssertDumpsEqual(Dump(original), Dump(restored), "binary");
}

TEST(TestJsonRoundTripPerField) {
  Snapshot original = MakeFilledSnapshot();
  Snapshot restored = SnapshotFromJson(SnapshotToJson(original));
  AssertDumpsEqual(Dump(original), Dump(restored), "JSON");
}

TEST(TestCrossFormatRoundTripPerField) {
  Snapshot original = MakeFilledSnapshot();
  // binary -> snapshot -> JSON -> snapshot: both serializers must preserve
  // the same field set.
  Snapshot via_bin = Snapshot::Deserialize(original.Serialize());
  Snapshot via_json = SnapshotFromJson(SnapshotToJson(via_bin));
  AssertDumpsEqual(Dump(original), Dump(via_json), "binary->JSON");
}

// =============================================================================
// Main
// =============================================================================

int main() {
  std::cout << "Running " << tests.size() << " snapshot field tests...\n\n";

  int passed = 0;
  int failed = 0;
  for (const auto& test : tests) {
    std::cout << "  " << test.name << "... ";
    try {
      test.func();
      std::cout << "PASSED\n";
      passed++;
    } catch (const std::exception& e) {
      std::cout << "FAILED\n    " << e.what() << "\n";
      failed++;
    }
  }

  std::cout << "\nResults: " << passed << " passed, " << failed << " failed\n";
  return failed > 0 ? 1 : 0;
}
