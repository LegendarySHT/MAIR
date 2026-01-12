#ifndef MOP_IR_IMPLEMENTATION_PASS_MANAGER_H
#define MOP_IR_IMPLEMENTATION_PASS_MANAGER_H

#include "Pass.h"
#include "PassContext.h"
#include "IRUnit.h"
#include <vector>
#include <memory>
#include <string>

namespace MopIRImpl {

/**
 * Pass Manager
 * 
 * 管理 Pass 的执行顺序和生命周期
 * 支持添加 Pass、运行 Pass 流水线、管理分析结果缓存
 */
class PassManager {
public:
  PassManager() = default;
  ~PassManager() = default;
  
  // 禁止拷贝，允许移动
  PassManager(const PassManager&) = delete;
  PassManager& operator=(const PassManager&) = delete;
  PassManager(PassManager&&) = default;
  PassManager& operator=(PassManager&&) = default;
  
  /**
   * 添加 Pass 到流水线
   * 
   * @param pass Pass 的 unique_ptr（PassManager 将获得所有权）
   */
  void addPass(std::unique_ptr<Pass> pass) {
    if (pass) {
      Passes.push_back(std::move(pass));
    }
  }
  
  /**
   * 运行所有 Pass 对 IR 进行优化
   * 
   * @param IR 要优化的 IR 单元
   * @param Context Pass 执行上下文（可选，如果不提供则创建新的）
   * @return 是否对 IR 进行了任何修改
   */
  bool run(IRUnit& IR, PassContext* Context = nullptr) {
    bool Modified = false;
    
    // 如果没有提供 Context，创建一个新的
    PassContext localContext;
    PassContext& ctx = Context ? *Context : localContext;
    
    for (auto& pass : Passes) {
      if (!pass) {
        continue;
      }
      
      try {
        bool passModified = pass->optimize(IR, ctx);
        Modified |= passModified;
        
        // 如果 Pass 失败且是必需的，继续执行
        // 如果不是必需的，可以选择是否继续
        if (!passModified && !pass->isRequired()) {
          // 可以在这里添加日志或统计
        }
      } catch (const std::exception& e) {
        // Pass 执行出错
        // 如果是必需 Pass，记录错误但继续
        // 如果不是必需 Pass，可以选择跳过
        if (pass->isRequired()) {
          // 记录错误但继续执行
          // 可以在这里添加错误日志
        } else {
          // 跳过这个 Pass
          continue;
        }
      }
    }
    
    return Modified;
  }
  
  /**
   * 运行 Pass 流水线（带统计信息）
   */
  struct RunStats {
    size_t PassesRun = 0;
    size_t PassesModified = 0;
    size_t PassesFailed = 0;
  };
  
  RunStats runWithStats(IRUnit& IR, PassContext* Context = nullptr) {
    RunStats stats;
    PassContext localContext;
    PassContext& ctx = Context ? *Context : localContext;
    
    for (auto& pass : Passes) {
      if (!pass) {
        continue;
      }
      
      stats.PassesRun++;
      
      try {
        bool passModified = pass->optimize(IR, ctx);
        if (passModified) {
          stats.PassesModified++;
        }
      } catch (const std::exception& e) {
        stats.PassesFailed++;
        if (!pass->isRequired()) {
          // 非必需 Pass 失败时跳过
          continue;
        }
      }
    }
    
    return stats;
  }
  
  /**
   * 获取 Pass 数量
   */
  size_t size() const {
    return Passes.size();
  }
  
  /**
   * 检查是否为空
   */
  bool empty() const {
    return Passes.empty();
  }
  
  /**
   * 清空所有 Pass
   */
  void clear() {
    Passes.clear();
  }
  
  /**
   * 获取 Pass 列表（用于调试）
   */
  std::vector<std::string> getPassNames() const {
    std::vector<std::string> names;
    for (const auto& pass : Passes) {
      if (pass) {
        names.push_back(pass->getName());
      }
    }
    return names;
  }

private:
  std::vector<std::unique_ptr<Pass>> Passes;
};

} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_PASS_MANAGER_H

