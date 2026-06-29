# Review Issue Tracker

Items from the code review, with completed items checked off.

## Completed

- [x] **1. Python bindings did not compile.**
  Fixed by moving the `fix_links`, `fix_queues`, `record_local_gids`, and `update_links` declarations in `include/diy/resolve.hpp` before `master.hpp` is included, so code pulled in by `master.hpp` can see those declarations. Verified by rebuilding the `_diy` Python extension.

- [x] **2. `mpi=OFF` with default tests/examples was not buildable.**
  Fixed by adding no-MPI compatibility for `MPI_Wtime()` and replacing raw MPI datatype usage in load-balancing code/tests/examples with DIY MPI datatype helpers where needed. Verified by building with `-Dmpi=OFF -Dbuild_examples=ON -Dbuild_tests=ON`.

- [x] **3. Compiled `diympi` library mode could fail when system `fmt` was found.**
  Fixed by linking compiled DIY MPI library targets with `${diy_libraries}`, so configured dependencies such as `fmt::fmt` propagate their include/link requirements. Verified by building with `-Dbuild_diy_mpi_lib=ON`.

- [x] **4. `Master::Skip` callback behavior needed documentation.**
  Documented the intended behavior in `include/diy/detail/master/execution.hpp`: skipping avoids loading an unloaded block but does not suppress callbacks, because `foreach()` callbacks may still need to perform queue or bookkeeping work. No behavior was changed.

- [x] **5. Threaded `Master::iexchange()` could violate MPI thread guarantees.**
  Fixed threaded `iexchange()` by keeping DIY MPI progress and termination control on the calling thread, while running user callbacks on a worker thread when `threads() > 1`. This preserves communication/compute overlap without requiring `MPI_THREAD_MULTIPLE`, so the default `MPI_THREAD_FUNNELED` environment no longer causes DIY to call MPI from a spawned communication thread.

- [x] **6. Dynamic load balancing could corrupt pending incoming queues.**
  Fixed migrated-block queue transfer to serialize explicit incoming queue metadata: source gid count, source gid, record count, and every queued buffer for each source. Queue buffers now carry their data bytes, read position, blob position, and unconsumed blob payloads, so the receiver reconstructs migrated pending queues from the payload instead of guessing from local state. Dynamic migration loads external incoming queue records before serializing them. Added dynamic-balance regression coverage for multiple sources, multiple records, preserved read positions, preserved blob positions, and blob payloads.

- [x] **7. `DynamicPoint` constructors were broken.**
  Fixed the converting and pointer constructors by sizing the underlying vector before assigning coordinates. Added tests covering cross-type construction and pointer-based construction.

- [x] **8. `LinkFactory` could not reliably reload saved links.**
  Fixed built-in link serialization to write stable IDs for plain links, common regular links, and AMR links. `LinkFactory` now recognizes those stable IDs plus legacy same-compiler `typeid` IDs when loading, while preserving existing registered-link behavior for custom link types. Added serialization tests covering plain, regular, AMR, stable, and legacy IDs.

- [x] **9. Point-to-GID queries mishandled wrapped or out-of-domain points.**
  Fixed point queries to clear output gids, return no gids for non-wrapped points outside the domain, normalize wrapped coordinates into the periodic domain, wrap generated division coordinates modulo the decomposition, and return `-1` from `point_to_gid()`/`lowest_gid()` when no block owns the point. Added decomposer tests covering wrapped, below-domain, above-domain, ghost-boundary, and discrete coordinates.

- [x] **10. Associative container deserialization left stale entries.**
  Fixed `std::map`, `std::set`, `std::unordered_map`, and `std::unordered_set` deserialization to clear the destination before loading serialized entries. Added tests that load into containers with stale contents.

- [x] **21. Dense kd-tree test matrix did not vary `n`.**
  Fixed the CMake test matrix to pass `-n ${n}` to `kd-tree-test2` and use the full target path for the executable. Hardened the dense CSV reader so missing or malformed datasets fail clearly instead of hanging or adding bogus points.

## Open

- [ ] **11. Python callbacks are unsafe with `threads > 1`.**
  Python `Master` exposes a thread count, but callbacks can execute on DIY worker threads without correct GIL handling. Proposed fix: either conservatively reject `threads > 1` in the Python `Master` constructor, or fully support it by releasing the GIL around long-running `foreach`/`iexchange` calls and acquiring the GIL inside every callback that touches Python objects. Ensure Python callables and `py::object` instances are also destroyed while holding the GIL.

- [ ] **12. Python MPI finalization can run before live wrappers are destroyed.**
  The Python package finalizes MPI through `atexit`, but live `Master` or communicator objects may later run destructors that call MPI after finalization. Proposed fix: guard MPI-dependent destructors, especially communicator destruction, with `MPI_Finalized` checks, and change Python finalization policy so automatic finalization does not occur while live wrappers remain. Prefer explicit/context-manager finalization or skip automatic finalization when wrappers are still alive.

- [ ] **13. Python custom serialization callbacks are effectively unusable.**
  Custom callbacks receive pointer-shaped C++ arguments such as `py::object*` and `BinaryBuffer*`, and `BinaryBuffer` has no usable Python API. Proposed fix: replace the callback API with a Pythonic form such as `save(obj) -> bytes` and `load(bytes) -> obj`, then marshal those bytes through DIY's `BinaryBuffer`. Apply the GIL-safety rules from item 11 to these callbacks.

- [ ] **14. Python output-parameter APIs do not behave like Python APIs.**
  Several bindings expose C++ output references such as `std::vector<int>&` and `int&`, which Python callers do not receive as mutated outputs. Proposed fix: replace these bindings with return-value APIs, for example returning vectors, tuples, or bounds objects directly. Add Python tests for `Proxy.incoming`, decomposer point queries, `top_bottom`, `gid_to_coords`, and `fill_bounds`.

- [ ] **15. Python callback proxy and partner objects can dangle.**
  Python callbacks receive pointers to stack/local proxy or partner objects; retaining them after the callback can cause use-after-free. Proposed fix: expose these objects as callback-scoped/non-owning handles that cannot be safely stored, or wrap them in lifetime-managed objects whose methods validate that the callback scope is still active. Document the lifetime rule and add tests that rejected retained handles fail clearly instead of dangling.

- [ ] **16. Python `Master.add` can retain blocks longer than expected.**
  `Master.add` stores a `py::object` and also uses `py::keep_alive`, so released or cleared blocks may remain alive until the `Master` wrapper is destroyed. Proposed fix: remove the redundant keep-alive relationship if the stored `py::object` already owns the block reference, and add lifetime tests covering `add`, `release`, `clear`, and `Master` destruction.

- [ ] **17. Python MPI communicator interop is brittle.**
  The binding reinterprets a `long` as an `MPI_Comm*` and relies on private `mpi4py.MPI._addressof`. Proposed fix: use mpi4py's public C API or a small compatibility layer to convert `mpi4py` communicators safely, avoid exposing raw integer pointer constructors as the primary API, and keep the raw path private or clearly documented as expert-only.

- [ ] **18. Python deserialization uses `pickle` without trusted-data documentation.**
  `pickle.loads` can execute arbitrary code when reading untrusted block data. Proposed fix: document that the default Python serializer is trusted-data-only, and optionally allow users to provide safer serializers through the custom callback API from item 13. Add a warning in Python docs/examples where block files are read.

- [ ] **19. Python source distributions may omit native sources.**
  `setup.py` declares the Python package and a CMake extension with no source manifest, and there is no root `MANIFEST.in`. Proposed fix: add a source distribution manifest or move to a modern `pyproject.toml`/scikit-build style package that explicitly includes `CMakeLists.txt`, `include/`, `bindings/python/src/`, vendored pybind11, and any CMake helper files required to build from an sdist. Verify with `python -m build --sdist` followed by installing from the generated tarball.

- [ ] **20. MPI CTest commands are fragile for multi-config generators.**
  MPI tests invoke bare executable names instead of target paths, which can fail when binaries live in configuration subdirectories. Proposed fix: replace bare executable names in `tests/CMakeLists.txt` with `$<TARGET_FILE:target>` generator expressions for all MPI-driven tests. Verify with Ninja Multi-Config, Xcode, or another multi-config generator.

- [ ] **22. Travis CI configuration is stale and misleading.**
  `.travis.yml` uses obsolete Python/Ubuntu versions and the configured matrix only exercises docs. Proposed fix: remove the stale Travis config if it is unused, or replace it with a maintained CI configuration that builds and tests the supported C++ and Python configurations.

- [ ] **23. Python threaded `iexchange()` needs explicit GIL handling.**
  Threaded C++ `iexchange()` now runs user callbacks on a worker thread while the calling thread drives MPI progress. The Python binding must either reject threaded `iexchange()` clearly, or release the GIL around the C++ call and acquire the GIL inside the worker-thread callback before touching Python objects. Add Python coverage for `Master(..., threads > 1).iexchange(...)` once the binding policy is chosen.
