#ifndef UTILS_DEFER_HPP
#define UTILS_DEFER_HPP

#include <functional>

namespace utils
{

namespace detail
{

class defer_ final
{
  public:
    template <typename _Func, typename = std::enable_if_t<!std::is_same_v<std::decay_t<_Func>, defer_>>>
    explicit defer_(_Func&& func) noexcept : func_{std::forward<_Func>(func)}
    {
    }

    ~defer_()
    {
        if (func_)
        {
            func_();
        }
    }

    defer_(defer_&& rhs) noexcept : func_{std::move(rhs.func_)}
    {
    }

    defer_& operator=(defer_&& rhs) noexcept
    {
        func_ = std::move(rhs.func_);
        return *this;
    }

  private:
    std::function<void()> func_;
};

} // namespace detail

class defer_maker final
{
  public:
    template <typename _Func> detail::defer_ operator+(_Func&& func)
    {
        return detail::defer_{std::forward<_Func>(func)};
    }
};

} // namespace utils

#define DEFER_CONCAT_NAME(l, r) l##r
#define DEFER_CREATE_NAME(l, r) DEFER_CONCAT_NAME(l, r)
#define defer auto DEFER_CREATE_NAME(_defer_, __COUNTER__) = utils::defer_maker{} +
#define defer_scope defer[&]

#endif // UTILS_DEFER_HPP
