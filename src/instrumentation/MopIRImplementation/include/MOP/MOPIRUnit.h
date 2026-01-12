#ifndef MOP_IR_IMPLEMENTATION_MOP_IR_UNIT_H
#define MOP_IR_IMPLEMENTATION_MOP_IR_UNIT_H

#include "IRUnit.h"
#include "MOP/MOPData.h"
#include <string>

namespace MopIRImpl {
namespace MOP {

/**
 * MOP IRUnit
 * 
 * 将 MOP 列表包装为 IRUnit，使其可以在 Pass Manager 中使用
 */
class MOPIRUnit : public MopIRImpl::IRUnit {
private:
  std::string Name;
  MopList Mops;
  
public:
  explicit MOPIRUnit(const std::string& name) : Name(name) {}
  
  std::string getName() const override {
    return Name;
  }
  
  bool isValid() const override {
    return !Mops.empty();
  }
  
  void print() const override {
    // 打印 MOP 列表信息
  }
  
  // MOP 特定访问器
  MopList& getMops() { return Mops; }
  const MopList& getMops() const { return Mops; }
  
  void addMop(std::unique_ptr<Mop> mop) {
    Mops.push_back(std::move(mop));
  }
  
  size_t size() const { return Mops.size(); }
};

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_MOP_IR_UNIT_H

