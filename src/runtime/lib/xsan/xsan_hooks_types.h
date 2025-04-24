#pragma once

#include "sanitizer_common/sanitizer_type_traits.h"

namespace __sanitizer {
extern const char *SanitizerToolName;  // Can be changed by the tool.
}

namespace __xsan {

/// TODO: This enum should be unified
enum class XsanHooksSanitizer {
  Asan,
  Tsan,
};

struct XsanHooksSanitizerUnImpl;

template <XsanHooksSanitizer san>
struct XsanHooksSanitizerImpl {
  // Hooks should implement like DefaultHooks in `xsan_hooks_default.h:25`.
  // You can simply inherit from it to use most of the default hooks.
  // If you see this error, you need to specialize this struct to register the
  // hooks for your sanitizer.
  using Hooks = XsanHooksSanitizerUnImpl;
};

class XsanThread;

struct XsanThreadQueryKey {
  friend class XsanThread;

 private:
  XsanThread *xsan_thread_ = nullptr;
};

class ScopedSanitizerToolName {
 public:
  explicit ScopedSanitizerToolName(const char *new_tool_name)
      : old_tool_name_(__sanitizer::SanitizerToolName) {
    __sanitizer::SanitizerToolName = new_tool_name;
  }
  ~ScopedSanitizerToolName() {
    __sanitizer::SanitizerToolName = old_tool_name_;
  }

 private:
  const char *const old_tool_name_;
};

// -------------------------- Hooks Checks --------------------------

// If T is a valid type, `using void_t = void` will be exported
template <typename... T>
using void_t = void;

template <bool, typename = void>
struct enable_if {};

template <typename T>
struct enable_if<true, T> {
  using type = T;
};

template <bool v, typename T = void>
using enable_if_t = typename enable_if<v, T>::type;

template <XsanHooksSanitizer san>
inline constexpr bool xsan_hooks_has_impl_v =
    !__sanitizer::is_same<typename XsanHooksSanitizerImpl<san>::Hooks,
                          XsanHooksSanitizerUnImpl>::value;

template <XsanHooksSanitizer san, typename = void>
inline constexpr bool xsan_hooks_has_context_v = false;

template <XsanHooksSanitizer san>
inline constexpr bool xsan_hooks_has_context_v<
    san, void_t<typename XsanHooksSanitizerImpl<san>::Hooks::Context>> = true;

template <XsanHooksSanitizer san, typename = void>
inline constexpr bool xsan_hooks_has_thread_v = false;

template <XsanHooksSanitizer san>
inline constexpr bool xsan_hooks_has_thread_v<
    san, void_t<typename XsanHooksSanitizerImpl<san>::Hooks::Thread>> = true;

#define XSAN_HOOKS_CHECK_IMPL(san)                                           \
  static_assert(                                                             \
      ::__xsan::xsan_hooks_has_impl_v<::__xsan::XsanHooksSanitizer::san>,    \
      #san " hooks not registered.");                                        \
  static_assert(                                                             \
      ::__xsan::xsan_hooks_has_context_v<::__xsan::XsanHooksSanitizer::san>, \
      #san " Hooks::Context not exists.");                                   \
  static_assert(                                                             \
      ::__xsan::xsan_hooks_has_thread_v<::__xsan::XsanHooksSanitizer::san>,  \
      #san " Hooks::Thread not exists.");

}  // namespace __xsan
