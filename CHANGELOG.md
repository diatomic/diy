# Version 3.5.dev1

- Add `DynamicPoint` class (dimension specified at runtime, uses `small_vector` for
  storage). Switch Bounds to use this class, deprecating old uses.
- Make geometry functions in `include/diy/pick.hpp` generic with respect to the
  floating point type.
- Change `LinkFactory` mechanism to make link serialization generic.
- Add `AMRLink` in `include/diy/link.hpp`
- Add `min_queue_size` and `max_hold_time` to control `Master::iexchange()` behavior

# Version 3.5.0
