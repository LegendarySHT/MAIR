#ifndef MOP_IR_IMPLEMENTATION_RECURRING_GRAPH_H
#define MOP_IR_IMPLEMENTATION_RECURRING_GRAPH_H

#include "../MOPData.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>

namespace MopIRImpl {
namespace MOP {

/**
 * 冗余图（Recurring Graph）
 * 
 * 用于表示 MOP 之间的冗余关系
 * - 顶点：MOP
 * - 边：如果 MOP1 覆盖 MOP2，则存在边 (MOP1 -> MOP2)
 * 
 * 支配集问题：找到能够到达图中所有顶点的最小顶点集合
 */
class RecurringGraph {
public:
  /**
   * 边结构
   * 
   * Killing -> Dead 表示 Killing 覆盖 Dead
   * From: 用于 ActiveMopAnalysis 检查的起始指令
   *   - 如果 KillingI dom DeadI，则 From = KillingI
   *   - 如果 KillingI pdom DeadI，则 From = DeadI
   */
  struct Edge {
    Mop* Killing;  // 覆盖者
    Mop* Dead;     // 被覆盖者
#ifdef MOP_IR_USE_LLVM
    const llvm::Instruction* From;  // 用于 ActiveMopAnalysis 检查的起始指令
#endif
    bool Blocked;   // 是否被阻塞（有函数调用干扰）
    
#ifdef MOP_IR_USE_LLVM
    Edge(Mop* killing, Mop* dead, const llvm::Instruction* from, bool blocked = false)
      : Killing(killing), Dead(dead), From(from), Blocked(blocked) {}
    // 兼容性构造函数（用于非 LLVM 路径，但编译时仍需要 From）
    Edge(Mop* killing, Mop* dead, bool blocked)
      : Killing(killing), Dead(dead), From(nullptr), Blocked(blocked) {}
#else
    Edge(Mop* killing, Mop* dead, bool blocked = false)
      : Killing(killing), Dead(dead), Blocked(blocked) {}
#endif
  };
  
  /**
   * 顶点结构
   */
  struct Vertex {
    Mop* const MopPtr;  // MOP 指针
    std::vector<Vertex*> Children;  // 子节点（被此 MOP 覆盖的 MOP）
    bool HasParent;                 // 是否有父节点（是否有其他 MOP 覆盖此 MOP）
    
    using child_iterator = decltype(Children)::iterator;
    using const_child_iterator = decltype(Children)::const_iterator;
    
    explicit Vertex(Mop* mop) : MopPtr(mop), HasParent(false) {}
    
    void addChild(Vertex* child) {
      Children.push_back(child);
      child->HasParent = true;
    }
  };

public:
  /**
   * 构造函数
   * 
   * @param vertices MOP 顶点列表
   * @param edges 边列表（只添加未阻塞的边）
   */
  RecurringGraph(const std::vector<Mop*>& vertices, 
                 const std::vector<Edge>& edges);
  
  /**
   * 填充支配集
   * 
   * 支配集 = 入度为 0 的顶点 + 未被遍历到的顶点
   * 
   * @param domSet 输出：支配集中的 MOP 列表
   */
  void fillDominatingSet(std::vector<Mop*>& domSet);

private:
  std::vector<Vertex> Vertices;
  std::unordered_map<Mop*, Vertex*> Mop2Vertex;  // MOP -> Vertex 映射
};

} // namespace MOP
} // namespace MopIRImpl

#endif // MOP_IR_IMPLEMENTATION_RECURRING_GRAPH_H

