#ifndef MOP_IR_IMPLEMENTATION_PASS_CONTEXT_H
#define MOP_IR_IMPLEMENTATION_PASS_CONTEXT_H

#include <memory>
#include <unordered_map>
#include <typeindex>
#include <typeinfo>

namespace MopIRImpl {

/**
 * Pass 执行上下文
 * 
 * 用于在 Pass 之间传递分析结果和共享数据
 * 支持类型安全的分析结果缓存
 */
class PassContext {
public:
  PassContext() = default;
  ~PassContext() = default;
  
  // 禁止拷贝，允许移动
  PassContext(const PassContext&) = delete;
  PassContext& operator=(const PassContext&) = delete;
  PassContext(PassContext&&) = default;
  PassContext& operator=(PassContext&&) = default;
  
  /**
   * 获取或计算分析结果
   * 
   * @tparam AnalysisT 分析结果类型
   * @tparam AnalysisFn 计算函数类型（返回 AnalysisT）
   * @param computeFn 如果结果不存在，调用此函数计算
   * @return 分析结果的引用
   */
  template<typename AnalysisT, typename AnalysisFn>
  AnalysisT& getOrCompute(AnalysisFn&& computeFn) {
    std::type_index key = typeid(AnalysisT);
    
    auto it = AnalysisCache.find(key);
    if (it != AnalysisCache.end()) {
      return *static_cast<AnalysisT*>(it->second.get());
    }
    
    // 计算新的分析结果
    AnalysisT* resultPtr = new AnalysisT(computeFn());
    AnalysisCache[key] = std::unique_ptr<void, AnalysisDeleter>(
        resultPtr, AnalysisDeleter::make<AnalysisT>());
    
    return *resultPtr;
  }
  
  /**
   * 获取分析结果（如果不存在则返回 nullptr）
   */
  template<typename AnalysisT>
  AnalysisT* get() {
    std::type_index key = typeid(AnalysisT);
    auto it = AnalysisCache.find(key);
    if (it != AnalysisCache.end()) {
      return static_cast<AnalysisT*>(it->second.get());
    }
    return nullptr;
  }
  
  /**
   * 设置分析结果
   */
  template<typename AnalysisT>
  void set(std::unique_ptr<AnalysisT> result) {
    std::type_index key = typeid(AnalysisT);
    AnalysisT* resultPtr = result.release();
    AnalysisCache[key] = std::unique_ptr<void, AnalysisDeleter>(
        resultPtr, AnalysisDeleter::make<AnalysisT>());
  }
  
  /**
   * 清除所有分析结果
   */
  void clear() {
    AnalysisCache.clear();
  }
  
  /**
   * 使特定类型的分析结果失效
   */
  template<typename AnalysisT>
  void invalidate() {
    std::type_index key = typeid(AnalysisT);
    AnalysisCache.erase(key);
  }

private:
  // 类型擦除的分析结果缓存
  struct AnalysisDeleter {
    void (*deleter)(void*);
    
    template<typename T>
    static void delete_impl(void* ptr) {
      delete static_cast<T*>(ptr);
    }
    
    template<typename T>
    static AnalysisDeleter make() {
      return {delete_impl<T>};
    }
    
    void operator()(void* ptr) const {
      if (deleter && ptr) {
        deleter(ptr);
      }
    }
  };
  
  std::unordered_map<std::type_index, std::unique_ptr<void, AnalysisDeleter>> AnalysisCache;
};

} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_PASS_CONTEXT_H

