#ifndef MOP_IR_IMPLEMENTATION_PASS_H
#define MOP_IR_IMPLEMENTATION_PASS_H

#include <string>

namespace MopIRImpl {

// 前向声明
class IRUnit;
class PassContext;

/**
 * Pass 基类接口
 * 
 * 所有优化 Pass 都应该继承自此类并实现 optimize 方法
 */
class Pass {
public:
  virtual ~Pass() = default;
  
  /**
   * 执行优化
   * @param IR 要优化的 IR 单元
   * @param Context Pass 执行上下文（包含分析结果等）
   * @return 是否对 IR 进行了修改
   */
  virtual bool optimize(IRUnit& IR, PassContext& Context) = 0;
  
  /**
   * 获取 Pass 名称（用于调试和日志）
   */
  virtual const char* getName() const = 0;
  
  /**
   * 获取 Pass 描述
   */
  virtual std::string getDescription() const {
    return std::string(getName());
  }
  
  /**
   * 检查 Pass 是否必需（必需 Pass 即使失败也会继续执行）
   */
  virtual bool isRequired() const {
    return false;
  }
};

} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_PASS_H

