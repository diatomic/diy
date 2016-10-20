#ifndef DIY_EXAMPLES_SORT_H
#define DIY_EXAMPLES_SORT_H

//typedef     int                         Value;
typedef     float                       Value;


template<class T>
T random(T min, T max);

template<>
int random(int min, int max)            { return min + rand() % (max - min); }

template<>
float random(float min, float max)      { return min + float(rand() % 1024) / 1024 * (max - min); }

template<class T>
struct Block
{
                  Block(int bins_):
                      bins(bins_)         {}

  static void*    create()                                      { return new Block; }
  static void     destroy(void* b)                              { delete static_cast<Block*>(b); }
  static void     save(const void* b, diy::BinaryBuffer& bb);
  static void     load(void* b, diy::BinaryBuffer& bb);

  void            generate_values(size_t n, T min_, T max_)
  {
    min = min_;
    max = max_;

    values.resize(n);
    for (size_t i = 0; i < n; ++i)
      values[i] = random<Value>(min, max);
  }

  void          print_block(const diy::Master::ProxyWithLink& cp, bool verbose)
  {
    std::cout << cp.gid() << ": " << min << " - " << max << ": " << values.size() << std::endl;

    if (verbose)
      for (size_t i = 0; i < values.size(); ++i)
        std::cout << "  " << values[i] << std::endl;
  }

  void          verify_block(const diy::Master::ProxyWithLink& cp)
  {
    for (size_t i = 0; i < values.size(); ++i)
      if (values[i] < min || values[i] > max)
        std::cout << "Warning: " << values[i] << " outside of [" << min << "," << max << "]" << std::endl;

    cp.all_reduce(values.size(), std::plus<size_t>());
  }

  T                     min, max;
  std::vector<T>        values;
  std::vector<T>        samples;            // used only in sample sort

  int                   bins;

  private:
                  Block()                                     {}
};

template<class T>
void
Block<T>::
save(const void* b_, diy::BinaryBuffer& bb)
{
  const Block<T>& b = *static_cast<const Block<T>*>(b_);

  diy::save(bb, b.min);
  diy::save(bb, b.max);
  diy::save(bb, b.values);
  diy::save(bb, b.samples);
  diy::save(bb, b.bins);
}

template<class T>
void
Block<T>::
load(void* b_, diy::BinaryBuffer& bb)
{
  Block<T>& b = *static_cast<Block<T>*>(b_);

  diy::load(bb, b.min);
  diy::load(bb, b.max);
  diy::load(bb, b.values);
  diy::load(bb, b.samples);
  diy::load(bb, b.bins);
}


#endif
