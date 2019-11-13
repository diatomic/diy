#include <iostream>
#include <diy/coroutine.hpp>

namespace dc = diy::coroutine;

struct info
{
    dc::cothread_t  main;
    int             arg;
};

void f()
{
    info* i = static_cast<info*>(dc::argument());
    dc::cothread_t main = i->main;
    int x = i->arg;

    std::cout << x << std::endl;
    x += 1;

    dc::co_switch(main);

    std::cout << x << std::endl;
    dc::co_switch(main);
}

int main()
{
    auto fc = dc::co_create(4*1024*1024, &f);

    info i;
    i.main = dc::co_active();
    i.arg  = 5;

    dc::argument() = &i;

    std::cout << "Jumping to f()" << std::endl;

    dc::co_switch(fc);
    std::cout << "Back in main" << std::endl;

    dc::co_switch(fc);
    dc::co_delete(fc);

    std::cout << "Done" << std::endl;
}
