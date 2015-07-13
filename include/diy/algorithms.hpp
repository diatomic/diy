#ifndef DIY_ALGORITHMS_HPP
#define DIY_ALGORITHMS_HPP

#include <vector>

#include "master.hpp"
#include "assigner.hpp"
#include "reduce.hpp"
#include "reduce-operations.hpp"
#include "partners/swap.hpp"

#include "detail/algorithms/sort.hpp"

namespace diy
{

//! sample sort `values` of each block, store the boundaries between blocks in `samples`
template<class Block, class T, class Cmp>
void sort(Master&                   master,
          const Assigner&           assigner,
          std::vector<T> Block::*   values,
          std::vector<T> Block::*   samples,
          size_t                    num_samples,
          const Cmp&                cmp,
          int                       k   = 2)
{
  detail::SampleSort<Block,T,Cmp> sorter(values, samples, cmp, num_samples);

  // swap-reduce to all-gather samples
  RegularSwapPartners   partners(1, assigner.nblocks(), k);
  reduce(master, assigner, partners, sorter.sample());

  // all_to_all to exchange the values
  all_to_all(master, assigner, sorter.exchange(), k);
}

template<class Block, class T>
void sort(Master&                   master,
          const Assigner&           assigner,
          std::vector<T> Block::*   values,
          std::vector<T> Block::*   samples,
          size_t                    num_samples,
          int                       k   = 2)
{
    sort(master, assigner, values, samples, num_samples, std::less<T>(), k);
}

}

#endif
