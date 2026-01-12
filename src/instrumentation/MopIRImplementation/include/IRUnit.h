#ifndef MOP_IR_IMPLEMENTATION_IR_UNIT_H
#define MOP_IR_IMPLEMENTATION_IR_UNIT_H

#include <vector>
#include <memory>

namespace MopIRImpl {

/**
 * IR 单元基类
 * 
 * 表示一个可以被优化的 IR 单元（可以是函数、模块等）
 * 这是一个抽象接口，具体实现由使用者提供
 */
class IRUnit {
public:
  virtual ~IRUnit() = default;
  
  /**
   * 获取 IR 单元的名称（用于调试和日志）
   */
  virtual std::string getName() const = 0;
  
  /**
   * 检查 IR 是否有效
   */
  virtual bool isValid() const {
    return true;
  }
  
  /**
   * 打印 IR 内容（用于调试）
   */
  virtual void print() const {}
};

} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_IR_UNIT_H

