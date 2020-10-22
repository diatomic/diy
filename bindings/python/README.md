# pyDIY

Python bindings for the [DIY][] library for block-parallel data analysis.

## Dependencies

  * MPI, e.g., [MPICH][]
  * [DIY][]
  * [PyBind11][]

[MPICH]:    http://www.mpich.org/
[DIY]:      https://github.com/diatomic/diy
[PyBind11]: https://github.com/pybind/pybind11

## Build

Install an MPI library. If you are using a package manager, it almost certainly
has a package for it. For example, on a Mac using [Homebrew][]:

[Homebrew]: https://brew.sh/

```
brew install mpich
```

pyDIY follows the standard CMake process.

```
mkdir build
cd build
cmake -D... ..
make
```

`cmake` needs to know paths to DIY and PyBind11. You can pass them using `-D` options:

```
cmake -DDIY_INCLUDE_DIR=.../path/to/diy/include -DPYBIND_INCLUDE_DIR=.../path/to/pybind11/include ..
```

Additionally, you may specify the Python version you'd like to use, e.g., `-DPYDIY_PYTHON_VERSION=2.7`.


## Installation

Currently, there is no installation process. If you run python from the
directory where you build pyDIY (`build` in the example above), you should be able to `import diy`.
`example.py` in the repository is a good place to start.
