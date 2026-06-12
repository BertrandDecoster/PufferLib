// Copyright 2024
// Snapshot binary serialization.
//
// The wire format is produced by two generic visitors (BinaryWriter /
// BinaryReader) driven by the single VisitFields list of each snapshot
// struct (snapshot.h). Field order in VisitFields IS the byte layout; the
// golden-fixture test (tests/test_snapshot_golden.cc) locks it.

#include "snapshot.h"

#include <cstring>
#include <stdexcept>

namespace companions {

// =============================================================================
// Snapshot validation helpers
// =============================================================================

bool Snapshot::HasSynchroCells() const {
  for (const AnnotationSnapshot& a : annotations) {
    if (a.tag == SemanticTag::SynchroGoal && a.target_type == 0) return true;
  }
  return false;
}

bool Snapshot::HasTargetCell() const {
  for (const AnnotationSnapshot& a : annotations) {
    if (a.tag == SemanticTag::AggroTarget && a.target_type == 0) return true;
  }
  return false;
}

bool Snapshot::HasPatrolPath() const {
  // Check direct patrol_path field first
  if (!patrol_path.empty()) {
    return true;
  }
  // Fall back to checking FSM agents
  for (const auto& agent : agents) {
    if (agent.has_fsm && !agent.fsm.patrol_path.empty()) {
      return true;
    }
  }
  return false;
}

int Snapshot::CountCells(CellKind kind) const {
  int count = 0;
  for (const auto& cell : cells) {
    if (cell.kind == kind) {
      count++;
    }
  }
  return count;
}

// =============================================================================
// Low-level buffer helpers
// =============================================================================

namespace {

constexpr uint32_t kSnapshotMagic = 0x534E4150;  // "SNAP"
constexpr uint32_t kSnapshotVersion = 2;         // v2 adds annotations

// Write primitive types to buffer
template <typename T>
void WriteValue(std::vector<uint8_t>& buffer, const T& value) {
  const uint8_t* ptr = reinterpret_cast<const uint8_t*>(&value);
  buffer.insert(buffer.end(), ptr, ptr + sizeof(T));
}

// Write string (length-prefixed)
void WriteString(std::vector<uint8_t>& buffer, const std::string& str) {
  uint32_t len = static_cast<uint32_t>(str.size());
  WriteValue(buffer, len);
  buffer.insert(buffer.end(), str.begin(), str.end());
}

// Read primitive types from buffer with bounds checking
template <typename T>
T ReadValue(const uint8_t*& ptr, const uint8_t* end) {
  if (ptr + sizeof(T) > end) {
    throw std::runtime_error("Snapshot buffer underflow: not enough data");
  }
  T value;
  std::memcpy(&value, ptr, sizeof(T));
  ptr += sizeof(T);
  return value;
}

// Read string (length-prefixed) with bounds checking
std::string ReadString(const uint8_t*& ptr, const uint8_t* end) {
  uint32_t len = ReadValue<uint32_t>(ptr, end);
  if (ptr + len > end) {
    throw std::runtime_error("Snapshot buffer underflow: string data truncated");
  }
  std::string str(reinterpret_cast<const char*>(ptr), len);
  ptr += len;
  return str;
}

// Adversarial-allocation guard: every serialized element consumes at least
// one wire byte, so a count larger than the remaining bytes is provably
// corrupt. Rejecting it HERE (before the caller's vec.resize) means a
// ~20-byte forged buffer cannot trigger a multi-GB transient allocation
// inside the host process (companions_load_snapshot runs in Unreal).
uint32_t ReadVectorSize(const uint8_t*& ptr, const uint8_t* end) {
  uint32_t size = ReadValue<uint32_t>(ptr, end);
  if (size > static_cast<size_t>(end - ptr)) {
    throw std::runtime_error("Snapshot buffer corrupt: unreasonable vector size");
  }
  return size;
}

// =============================================================================
// BinaryWriter - visitor producing the v2 wire format
// =============================================================================
// Field names are ignored; only visitation order matters. Per-type width
// policies (enum widths, guarded FSM block, cell pairs) reproduce the
// historical layout exactly.
class BinaryWriter {
 public:
  explicit BinaryWriter(std::vector<uint8_t>& buffer) : buffer_(buffer) {}

  // Scalars (int, bool, uint8_t, uint64_t, ...) and nested visitable structs.
  template <class T>
  void operator()(const char* name, const T& value, bool /*json_optional*/ = false) {
    if constexpr (kHasVisitFields<T>) {
      T::VisitFields(value, *this);
    } else {
      static_assert(std::is_arithmetic_v<T>,
                    "BinaryWriter: add an explicit overload for this type");
      (void)name;
      WriteValue(buffer_, value);
    }
  }

  void operator()(const char*, const std::string& s, bool = false) {
    WriteString(buffer_, s);
  }

  void operator()(const char*, const Position& p, bool = false) {
    WriteValue(buffer_, p.row);
    WriteValue(buffer_, p.col);
  }

  // Enum wire widths (historical layout).
  void operator()(const char*, const FSMStateType& v, bool = false) {
    WriteValue(buffer_, static_cast<uint8_t>(v));
  }
  void operator()(const char*, const TargetFilter& v, bool = false) {
    WriteValue(buffer_, static_cast<int>(v));
  }
  void operator()(const char*, const SemanticTag& v, bool = false) {
    WriteValue(buffer_, static_cast<uint16_t>(v));
  }

  // EnumField: binary keeps the raw int (JSON is where the string lives).
  template <class E, class IntT>
  void operator()(const char*, EnumFieldRef<E, IntT> f, bool = false) {
    WriteValue(buffer_, static_cast<int>(f.value));
  }

  // Guarded FSM block: bool, then the struct only if present.
  template <class BoolT, class FsmT>
  void operator()(const char*, GuardedFsmRef<BoolT, FsmT> f, bool = false) {
    WriteValue(buffer_, static_cast<bool>(f.has));
    if (f.has) {
      FSMSnapshot::VisitFields(f.fsm, *this);
    }
  }

  // Vectors: u32 count + elements (each dispatched through this visitor).
  template <class T>
  void operator()(const char* name, const std::vector<T>& vec, bool = false) {
    WriteValue(buffer_, static_cast<uint32_t>(vec.size()));
    for (const T& item : vec) {
      (*this)(name, item);
    }
  }

  // Cells: flat (kind, origin) int pairs — see CellSnapshot comment in
  // snapshot.h for why this is a dedicated policy.
  void operator()(const char*, const std::vector<CellSnapshot>& cells, bool = false) {
    WriteValue(buffer_, static_cast<uint32_t>(cells.size()));
    for (const auto& cell : cells) {
      WriteValue(buffer_, static_cast<int>(cell.kind));
      WriteValue(buffer_, static_cast<int>(cell.origin));
    }
  }

  // Annotation params: count + (key, value) string pairs.
  void operator()(const char*,
                  const std::vector<std::pair<std::string, std::string>>& params,
                  bool = false) {
    WriteValue(buffer_, static_cast<uint32_t>(params.size()));
    for (const auto& kv : params) {
      WriteString(buffer_, kv.first);
      WriteString(buffer_, kv.second);
    }
  }

 private:
  std::vector<uint8_t>& buffer_;
};

// =============================================================================
// BinaryReader - visitor consuming v1/v2 wire format
// =============================================================================
class BinaryReader {
 public:
  BinaryReader(const uint8_t*& ptr, const uint8_t* end, uint32_t version)
      : ptr_(ptr), end_(end), version_(version) {}

  template <class T>
  void operator()(const char* name, T& value, bool /*json_optional*/ = false) {
    if constexpr (kHasVisitFields<T>) {
      T::VisitFields(value, *this);
    } else {
      static_assert(std::is_arithmetic_v<T>,
                    "BinaryReader: add an explicit overload for this type");
      (void)name;
      value = ReadValue<T>(ptr_, end_);
    }
  }

  void operator()(const char*, std::string& s, bool = false) {
    s = ReadString(ptr_, end_);
  }

  void operator()(const char*, Position& p, bool = false) {
    p.row = ReadValue<int>(ptr_, end_);
    p.col = ReadValue<int>(ptr_, end_);
  }

  void operator()(const char*, FSMStateType& v, bool = false) {
    v = static_cast<FSMStateType>(ReadValue<uint8_t>(ptr_, end_));
  }
  void operator()(const char*, TargetFilter& v, bool = false) {
    v = static_cast<TargetFilter>(ReadValue<int>(ptr_, end_));
  }
  void operator()(const char*, SemanticTag& v, bool = false) {
    v = static_cast<SemanticTag>(ReadValue<uint16_t>(ptr_, end_));
  }

  template <class E, class IntT>
  void operator()(const char*, EnumFieldRef<E, IntT> f, bool = false) {
    f.value = ReadValue<int>(ptr_, end_);
  }

  template <class BoolT, class FsmT>
  void operator()(const char*, GuardedFsmRef<BoolT, FsmT> f, bool = false) {
    f.has = ReadValue<bool>(ptr_, end_);
    if (f.has) {
      FSMSnapshot::VisitFields(f.fsm, *this);
    }
  }

  template <class T>
  void operator()(const char* name, std::vector<T>& vec, bool = false) {
    uint32_t size = ReadVectorSize(ptr_, end_);
    vec.clear();
    vec.resize(size);
    for (uint32_t i = 0; i < size; ++i) {
      (*this)(name, vec[i]);
    }
  }

  void operator()(const char*, std::vector<CellSnapshot>& cells, bool = false) {
    uint32_t size = ReadVectorSize(ptr_, end_);
    cells.clear();
    cells.resize(size);
    for (uint32_t i = 0; i < size; ++i) {
      // For v1 buffers the raw int may be an old-numbering CellKind; it is
      // stored as-is and remapped afterwards by MigrateV1.
      cells[i].kind = static_cast<CellKind>(ReadValue<int>(ptr_, end_));
      cells[i].origin = static_cast<CellOrigin>(ReadValue<int>(ptr_, end_));
    }
  }

  // Annotations block exists only from v2 on; v1 buffers end at patrol_path.
  void operator()(const char* name, std::vector<AnnotationSnapshot>& annotations,
                  bool = false) {
    if (version_ < 2) {
      annotations.clear();
      return;
    }
    uint32_t size = ReadVectorSize(ptr_, end_);
    annotations.clear();
    annotations.resize(size);
    for (uint32_t i = 0; i < size; ++i) {
      AnnotationSnapshot::VisitFields(annotations[i], *this);
    }
    (void)name;
  }

  void operator()(const char*,
                  std::vector<std::pair<std::string, std::string>>& params,
                  bool = false) {
    uint32_t size = ReadVectorSize(ptr_, end_);
    params.clear();
    params.reserve(size);
    for (uint32_t i = 0; i < size; ++i) {
      std::string k = ReadString(ptr_, end_);
      std::string v = ReadString(ptr_, end_);
      params.emplace_back(std::move(k), std::move(v));
    }
  }

 private:
  const uint8_t*& ptr_;
  const uint8_t* end_;
  uint32_t version_;
};

// =============================================================================
// Version migrations
// =============================================================================
// Each MigrateN upgrades an in-memory Snapshot read from a version-N buffer
// to version N+1 semantics. Future versions chain in Deserialize:
//   if (version <= 1) MigrateV1(snap);
//   if (version <= 2) MigrateV2(snap);  // when v3 exists
//
// v1 → v2: the old CellKind enum reserved values 3 (Synchro) and 5 (Target)
// for task-semantic roles that now live in AnnotationStore, and HealArea was
// at int 4 instead of 3. Remap and emit persistent annotations at the
// former-semantic positions so downstream code still sees the task roles.
// v1 buffers also carry no annotations block (BinaryReader leaves it empty).
void MigrateV1(Snapshot& snap) {
  std::vector<AnnotationSnapshot> migrated_annotations;
  for (size_t i = 0; i < snap.cells.size(); ++i) {
    const int kind_int = static_cast<int>(snap.cells[i].kind);
    const int row = snap.cols > 0 ? static_cast<int>(i) / snap.cols : 0;
    const int col = snap.cols > 0 ? static_cast<int>(i) % snap.cols : 0;
    switch (kind_int) {
      case 0: snap.cells[i].kind = CellKind::Floor; break;
      case 1: snap.cells[i].kind = CellKind::Wall; break;
      case 2: snap.cells[i].kind = CellKind::Hazard; break;
      case 3: {  // Old Synchro → Floor + SynchroGoal annotation
        snap.cells[i].kind = CellKind::Floor;
        AnnotationSnapshot a;
        a.target_type = 0;
        a.pos = Position{row, col};
        a.agent_id = kInvalidObjectId;
        a.tag = SemanticTag::SynchroGoal;
        a.owner_lens_id = -1;
        migrated_annotations.push_back(std::move(a));
        break;
      }
      case 4: snap.cells[i].kind = CellKind::HealArea; break;  // Old 4 → new 3
      case 5: {  // Old Target → Floor + AggroTarget annotation
        snap.cells[i].kind = CellKind::Floor;
        AnnotationSnapshot a;
        a.target_type = 0;
        a.pos = Position{row, col};
        a.agent_id = kInvalidObjectId;
        a.tag = SemanticTag::AggroTarget;
        a.owner_lens_id = -1;
        migrated_annotations.push_back(std::move(a));
        break;
      }
      default:
        throw std::runtime_error("Snapshot v1: unknown CellKind value");
    }
  }
  snap.annotations = std::move(migrated_annotations);
}

}  // namespace

// =============================================================================
// Snapshot serialization
// =============================================================================

std::vector<uint8_t> Snapshot::Serialize() const {
  std::vector<uint8_t> buffer;

  // Magic number and version
  WriteValue(buffer, kSnapshotMagic);
  WriteValue(buffer, kSnapshotVersion);

  BinaryWriter writer(buffer);
  Snapshot::VisitFields(*this, writer);
  return buffer;
}

Snapshot Snapshot::Deserialize(const std::vector<uint8_t>& data) {
  if (data.size() < 8) {
    throw std::runtime_error("Snapshot data too small");
  }

  const uint8_t* ptr = data.data();
  const uint8_t* end = data.data() + data.size();

  // Magic number and version
  uint32_t magic = ReadValue<uint32_t>(ptr, end);
  if (magic != kSnapshotMagic) {
    throw std::runtime_error("Invalid snapshot magic number");
  }
  uint32_t version = ReadValue<uint32_t>(ptr, end);
  if (version != 1 && version != 2) {
    throw std::runtime_error("Unsupported snapshot version");
  }

  Snapshot snap;
  BinaryReader reader(ptr, end, version);
  Snapshot::VisitFields(snap, reader);

  // Chain migrations oldest-first.
  if (version <= 1) MigrateV1(snap);

  return snap;
}

}  // namespace companions
