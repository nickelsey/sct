// sct/core/base.hh

#ifndef SCT_CORE_BASE_HH
#define SCT_CORE_BASE_HH

// common macros & defs used throughout sct

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

// generated by cmake from macros.hh.in
#include "sct/core/macros.hh"

namespace sct {
  
  // define numerical constants
  constexpr double pi = 3.14159265359;
  constexpr double twoPi = 6.28318530718;
  
  // commonly used classes from std, use statements
  // in sct namespace so that including sct headers
  // won't modify the global namespace
  using std::pair;
  using std::string;
  using std::vector;
  using std::unique_ptr;
  using std::shared_ptr;
  using std::unordered_map;
  
  // use enum classes and pairs as keys, need to specify
  // the hash function
  struct EnumClassHash {
    template <typename T>
    std::size_t operator()(T t) const {
      return static_cast<std::size_t>(t);
    }
  };
  
  struct PairHash {
  public:
    template <typename T, typename U>
    std::size_t operator()(const std::pair<T, U> &x) const
    {
      return std::hash<T>()(x.first) ^ std::hash<U>()(x.second);
    }
  };
  
  // define the centrality boundaries for the traditional 16 bin, 0-80%
  // centrality definition for convenience
  static const vector<double> Centrality16BinLowerBound = {0, 5, 10, 15,
                         20, 25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75};
  static const vector<double> Centrality16BinUpperBound = {5, 10, 15, 20,
                          25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80};
  
  // make_unique is a C++14 feature. If we don't have 14, we will emulate
  // its behavior. This is copied from facebook's folly/Memory.h
  #if __cplusplus >= 201402L ||                                              \
      (defined __cpp_lib_make_unique && __cpp_lib_make_unique >= 201304L) || \
      (defined(_MSC_VER) && _MSC_VER >= 1900)
    /* using override */
    using std::make_unique;
  #else
  
    template<typename T, typename... Args>
    typename std::enable_if<!std::is_array<T>::value, std::unique_ptr<T>>::type
    make_unique(Args&&... args) {
      return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
  
    // Allows 'make_unique<T[]>(10)'. (N3690 s20.9.1.4 p3-4)
    template<typename T>
    typename std::enable_if<std::is_array<T>::value, std::unique_ptr<T>>::type
    make_unique(const size_t n) {
      return std::unique_ptr<T>(new typename std::remove_extent<T>::type[n]());
    }
  
    // Disallows 'make_unique<T[10]>()'. (N3690 s20.9.1.4 p5)
    template<typename T, typename... Args>
    typename std::enable_if<
    std::extent<T>::value != 0, std::unique_ptr<T>>::type
    make_unique(Args&&...) = delete;
  
  #endif

  // and include make_shared
  using std::make_shared;
  
} // namespace sct

#endif // SCT_CORE_BASE_HH
