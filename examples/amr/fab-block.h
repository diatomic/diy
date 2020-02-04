#pragma once

#include <diy/serialization.hpp>
#include <diy/grid.hpp>
#include <diy/thirdparty/fmt/format.h>
#include <diy/thirdparty/fmt/ostream.h>

template<class T>
struct FabBlock
{
    using Shape = diy::Point<int, 3>;

                    FabBlock():
                        fab(fab_storage_.data(), fab_storage_.shape(), fab_storage_.c_order())  {}

                    FabBlock(const T* data, const Shape& shape):
                        fab(data, shape, /* c_order = */ false)         {}

    static void*    create()                                            { return new FabBlock; }
    static void     destroy(void* b_)                                   { delete static_cast<FabBlock<T>*>(b_); }
    static void     save(const void* b_, diy::BinaryBuffer& bb);
    static void     load(void* b_,       diy::BinaryBuffer& bb);

    diy::Grid<T, 3>          fab_storage_;        // container, in case we own the data
    diy::GridRef<const T, 3> fab;
};

template<class T>
void
FabBlock<T>::save(const void* b_, diy::BinaryBuffer& bb)
{
    auto* b = static_cast<const FabBlock<T>*>(b_);
    diy::save(bb, b->fab.shape());
    diy::save(bb, b->fab.c_order());
    diy::save(bb, b->fab.data(), b->fab.size());
}

template<class T>
void
FabBlock<T>::load(void* b_, diy::BinaryBuffer& bb)
{
    auto* b = static_cast<FabBlock<T>*>(b_);

    Shape   shape;
    bool    c_order;
    diy::load(bb, shape);
    diy::load(bb, c_order);

    b->fab_storage_ = decltype(b->fab_storage_)(shape, c_order);
    diy::load(bb, b->fab_storage_.data(), b->fab_storage_.size());

    b->fab = decltype(b->fab)(b->fab_storage_.data(), shape, c_order);     // fab points to the data in fab_storage_
}
