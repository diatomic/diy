#ifndef DIY_FACTORY_HPP
#define DIY_FACTORY_HPP

// From http://www.nirfriedman.com/2018/04/29/unforgettable-factory/
// with minor changes.

#include <memory>
#include <string>
#include <unordered_map>

#include <cstdlib>

#if !defined(_WIN32)
#include <cxxabi.h>
#endif

namespace diy
{

namespace detail
{
    std::string demangle(const char *name)
    {
#if !defined(_WIN32)
      int status = -1;
      std::unique_ptr<char, void (*)(void *)> res { abi::__cxa_demangle(name, NULL, NULL, &status), std::free };
      return (status == 0) ? res.get() : name;
#else
      return name;
#endif
    }
} // detail

template <class Base, class... Args>
class Factory
{
    public:
        template <class... T>
        static Base* make(const std::string &s, T&&... args)
        {
            return data().at(s)(std::forward<T>(args)...);
        }

        virtual std::string id() const          { return detail::demangle(typeid(Base).name()); }

        template <class T>
        struct Registrar: Base
        {
            friend T;

            static bool registerT()
            {
                const auto name = detail::demangle(typeid(T).name());
                Factory::data()[name] = [](Args... args) -> Base*
                {
                    return new T(std::forward<Args>(args)...);
                };
                return true;
            }
            static bool registered;

            std::string id() const override     { return detail::demangle(typeid(T).name()); }

            private:
                Registrar(): Base(Key{}) { (void)registered; }
        };

        friend Base;

    private:
        class Key
        {
            Key(){};
            template <class T> friend struct Registrar;
        };

        using FuncType = Base* (*)(Args...);

        Factory() = default;

        static std::unordered_map<std::string, FuncType>& data()
        {
            static std::unordered_map<std::string, FuncType> s;
            return s;
        }
};

template <class Base, class... Args>
template <class T>
bool Factory<Base, Args...>::Registrar<T>::registered = Factory<Base, Args...>::Registrar<T>::registerT();

}

#endif
