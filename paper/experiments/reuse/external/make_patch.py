#!/usr/bin/env python3
"""Generate the predeclared ranking patch for TVM's TokenAllocatorMixed::RequestReuse.

The replaced block is TVM's Steps 2-4 (windowed best-fit / enlarge-smaller search).
The new block ranks every available same-scope/dtype token by added capacity,
then capacity, then storage id, mirroring Joggle's `reuse.better` rule.
Eligibility (scope/dtype pool, released tokens, dynamic-size branch) is unchanged.
"""
import difflib, hashlib, sys
from pathlib import Path

SRC = Path("/Users/lancer/Documents/Item/joggle-study/tvm/src/relax/transform/static_plan_block_memory.cc")
OUT = Path(__file__).resolve().parent
text = SRC.read_text()

start = text.index("    // Step 2. Get the range of memory blocks in [size / match_range_, size * match_range_)")
end = text.index("    // Return `std::nullopt` indicating that no satisfiable storage token is found in the available")
old = text[start:end]
new = '''    // Predeclared control ranking (storage-planner study): among every available
    // token of this scope/dtype with a constant size, prefer the token whose reuse
    // adds the least capacity, then the smallest capacity, then the earliest
    // storage id. This mirrors the compared Joggle `reuse.better` rule and replaces
    // TVM's windowed best-fit/enlarge-smaller search; eligibility is unchanged.
    auto best = pool.end();
    int64_t best_growth = 0;
    int64_t best_size = 0;
    for (auto it = pool.begin(); it != pool.end(); ++it) {
      int64_t available_size = it->second->const_bytes();
      if (available_size < 0) {
        continue;
      }
      int64_t growth = size > available_size ? size - available_size : 0;
      bool better = best == pool.end() || growth < best_growth ||
                    (growth == best_growth &&
                     (available_size < best_size ||
                      (available_size == best_size &&
                       it->second->storage_id < best->second->storage_id)));
      if (better) {
        best = it;
        best_growth = growth;
        best_size = available_size;
      }
    }
    if (best != pool.end()) {
      StorageToken available_token = best->second;
      TVM_FFI_ICHECK_EQ(available_token->ref_counter, 0)
          << "Available tokens are expected to have 0 reference.";
      if (size > best_size) {
        // Enlarge the token size.
        available_token->bytes = IntImm::Int64(size);
      }
      available_token->ref_counter = prototype->ref_counter;
      pool.erase(best);
      return available_token;
    }
'''
patched = text[:start] + new + text[end:]
(OUT / "static_plan_block_memory.patched.cc").write_text(patched)
rel = "src/relax/transform/static_plan_block_memory.cc"
diff = difflib.unified_diff(text.splitlines(True), patched.splitlines(True),
                            fromfile="a/" + rel, tofile="b/" + rel)
(OUT / "tvm-ranking.patch").write_text("".join(diff))
print("original sha256", hashlib.sha256(text.encode()).hexdigest())
print("patched  sha256", hashlib.sha256(patched.encode()).hexdigest())
print("replaced lines", old.count("\n"), "->", new.count("\n"))
