\defgroup Serialization

~~~~{.cpp}
    #include <diy/serialization.hpp>

    std::vector<MyStruct>   vec;
    // ...

    diy::BinaryBuffer bb;
    diy::save(bb, vec);

    // ...
    vec.clear();
    diy::load(bb, vec);
~~~~


The primary interface for serialization in DIY are `diy::save()` and
`diy::load()`.  They call the respective functions in the `diy::Serialization`
class, explained below.
Array versions of the functions optimize copying of arrays of binary data (but
still do the right thing for customized types).


Customizing
-----------

The default (unspecialized) version of `diy::Serialization<T>` copies
`sizeof(T)` bytes from `&x` to or from the `diy::BinaryBuffer`
via `diy::BinaryBuffer::save_binary()` and `diy::BinaryBuffer::load_binary()`.
This works out perfectly for plain old data (e.g., simple structs), but
to save a more complicated type, one has to specialize
`diy::Serialization<T>`. (Specializations are already provided for many of the STL types, for example,
`std::vector<T>`, `std::map<K,V>`, `std::pair<T,U>`, `std::valarray<T>`, `std::string`, `std::set<T>`,
`std::unordered_map<K,V>`, `std::unordered_set<T>`, `std::tuple<Args...>`.)
As a result, one can quickly add a specialization of one's own:

~~~~{.cpp}
struct Point
{
    int                 x, y;
    std::vector<int>    neighbors;
};

template<>
struct Serialization<Point>
{
    static void save(BinaryBuffer& bb, const Point& p)
    {
        diy::save(bb, x);
        diy::save(bb, y);
        diy::save(bb, neighbors);
    }

    static void load(BinaryBuffer& bb, Point& p)
    {
        diy::load(bb, x);
        diy::load(bb, y);
        diy::load(bb, neighbors);
    }
};
~~~~

Note that if point had only members `x` and `y`, it would be plain old data.
So we would not need to specialize `diy::Serialization<Point>` because `Point`
could just be copied as a binary. In general, it's better to leave
`diy::Serialization` unspecialized for plain old data because then serialization
of `std::vector<...>` of that type can be optimized (by copying the entire array
at once).
