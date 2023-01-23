The following algorithms are included in DIY:

## Sort

A parallel sample sort that sorts values of each block and computes the boundaries in `samples`. Optionally, the results can be all_to_all distributed to everyone. A shorter version of the API with a default comparison function and the all_to_all exchange mandatory is also included. The sort uses the same k-ary global reduction as in the communication module.

See the example [here](https://github.com/diatomic/diy/blob/master/examples/reduce/sample-sort.cpp).

## Kd-tree

A set of points initially distributed in a set of blocks can be redistributed in a kd-tree containing the same number of blocks. The number of blocks must be a power of 2, and the depth of the kd-tree will be log_2(num_blocks). The block boundaries are computed using a histogram; hence they are approximate to the bin width of the histogram. The number of histogram bins is user-supplied.

See the example [here](https://github.com/diatomic/diy/blob/master/examples/reduce/kd-tree.cpp).
