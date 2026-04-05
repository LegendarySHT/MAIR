#ifndef MOP_IR_IMPLEMENTATION_PASS_CONTEXT_H
#define MOP_IR_IMPLEMENTATION_PASS_CONTEXT_H

#include <cstdint>
#include <memory>
#include <unordered_map>

namespace MopIRImpl {

namespace detail {
/// 每个 T 对应不同函数实例，地址在全体翻译单元中唯一（不依赖 RTTI）
template <typename T> void analysisTypeAnchor() {}
} // namespace detail

template <typename T> std::uintptr_t analysisTypeKey() {
  using FnPtr = void (*)();
  return reinterpret_cast<std::uintptr_t>(
      reinterpret_cast<FnPtr>(&detail::analysisTypeAnchor<T>));
}

/**
 * Pass 执行上下文
 *
 * 用于在 Pass 之间传递分析结果和共享数据
 * 支持类型安全的分析结果缓存
 */
class PassContext {
public:
  /// 用于在 -fno-rtti 下替代 dynamic_cast
  enum class Kind { Generic, LLVM };

  PassContext() = default;
  virtual ~PassContext() = default;

  virtual Kind getPassContextKind() const { return Kind::Generic; }

  PassContext(const PassContext &) = delete;
  PassContext &operator=(const PassContext &) = delete;
  PassContext(PassContext &&) = default;
  PassContext &operator=(PassContext &&) = default;

  template <typename AnalysisT, typename AnalysisFn>
  AnalysisT &getOrCompute(AnalysisFn &&computeFn) {
    const std::uintptr_t key = analysisTypeKey<AnalysisT>();

    auto it = AnalysisCache.find(key);
    if (it != AnalysisCache.end()) {
      return *static_cast<AnalysisT *>(it->second.get());
    }

    AnalysisT *resultPtr = new AnalysisT(computeFn());
    AnalysisCache[key] = std::unique_ptr<void, AnalysisDeleter>(
        resultPtr, AnalysisDeleter::make<AnalysisT>());

    return *resultPtr;
  }

  template <typename AnalysisT> AnalysisT *get() {
    const std::uintptr_t key = analysisTypeKey<AnalysisT>();
    auto it = AnalysisCache.find(key);
    if (it != AnalysisCache.end()) {
      return static_cast<AnalysisT *>(it->second.get());
    }
    return nullptr;
  }

  template <typename AnalysisT> void set(std::unique_ptr<AnalysisT> result) {
    const std::uintptr_t key = analysisTypeKey<AnalysisT>();
    AnalysisT *resultPtr = result.release();
    AnalysisCache[key] = std::unique_ptr<void, AnalysisDeleter>(
        resultPtr, AnalysisDeleter::make<AnalysisT>());
  }

  void clear() { AnalysisCache.clear(); }

  template <typename AnalysisT> void invalidate() {
    AnalysisCache.erase(analysisTypeKey<AnalysisT>());
  }

private:
  struct AnalysisDeleter {
    void (*deleter)(void *);

    template <typename T> static void delete_impl(void *ptr) {
      delete static_cast<T *>(ptr);
    }

    template <typename T> static AnalysisDeleter make() {
      return {delete_impl<T>};
    }

    void operator()(void *ptr) const {
      if (deleter && ptr) {
        deleter(ptr);
      }
    }
  };

  std::unordered_map<std::uintptr_t, std::unique_ptr<void, AnalysisDeleter>>
      AnalysisCache;
};

} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_PASS_CONTEXT_H
