#pragma once

namespace __sanitizer {
extern const char *SanitizerToolName;  // Can be changed by the tool.
}

namespace __xsan {

/// TODO: This enum should be unified
enum class XsanHooksSanitizer {
  Asan,
  Tsan,
};

template <XsanHooksSanitizer san>
struct XsanHooksSanitizerUnImpl;

template <XsanHooksSanitizer san>
struct XsanHooksSanitizerImpl {
  // Hooks should implement like DefaultHooks in `xsan_hooks_default.h:25`.
  // You can simply inherit from it to use most of the default hooks.
  // If you see this error, you need to specialize this struct to register the
  // hooks for your sanitizer.
  using Hooks = XsanHooksSanitizerUnImpl<san>;
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

}  // namespace __xsan
