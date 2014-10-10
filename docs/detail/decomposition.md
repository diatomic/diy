\defgroup Decomposition

Provides helper functions for regular decomposition of the domain.

Example     {#decomposition-example}
-------

~~~{.cpp}
void create(int gid, const Bounds& core, const Bounds& bounds, const Bounds& domain, const diy::Link& link)
{
    // ...
}

// share faces along the first axis
diy::RegularDecomposer<Bounds>::BoolVector          share_face;
share_face.push_back(true);

// wrap along the first and second axis
diy::RegularDecomposer<Bounds>::BoolVector          wrap;
wrap.push_back(true);
wrap.push_back(true);

// use 1 and 2 ghosts along the first and second axes, respectively
diy::RegularDecomposer<Bounds>::CoordinateVector    ghosts;
ghosts.push_back(1); ghosts.push_back(2);

// decompose a 3D domain
diy::decompose(3, rank, domain, assigner, create, share_face, wrap, ghosts);
~~~
