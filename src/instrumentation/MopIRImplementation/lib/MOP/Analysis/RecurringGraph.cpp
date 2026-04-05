#include "MOP/Analysis/RecurringGraph.h"
#include <algorithm>
#include <iostream>

#ifdef MOP_IR_USE_LLVM
#include "llvm/ADT/DepthFirstIterator.h"
#include "llvm/ADT/GraphTraits.h"
#include "llvm/ADT/SetVector.h"
#endif

namespace MopIRImpl {
namespace MOP {

#ifdef MOP_IR_USE_LLVM
} // namespace MOP
} // namespace MopIRImpl

namespace llvm {

template <> struct GraphTraits<MopIRImpl::MOP::RecurringGraph::Vertex *> {
  using NodeRef = MopIRImpl::MOP::RecurringGraph::Vertex *;
  using ChildIteratorType = std::remove_pointer<NodeRef>::type::child_iterator;

  static NodeRef getEntryNode(NodeRef Node) { return Node; }

  static ChildIteratorType child_begin(NodeRef Node) {
    return Node->Children.begin();
  }

  static ChildIteratorType child_end(NodeRef Node) {
    return Node->Children.end();
  }
};

template <> struct GraphTraits<const MopIRImpl::MOP::RecurringGraph::Vertex *> {
  using NodeRef = const MopIRImpl::MOP::RecurringGraph::Vertex *;
  using ChildIteratorType =
      std::remove_pointer<NodeRef>::type::const_child_iterator;

  static NodeRef getEntryNode(NodeRef Node) { return Node; }

  static ChildIteratorType child_begin(NodeRef Node) {
    return Node->Children.begin();
  }

  static ChildIteratorType child_end(NodeRef Node) {
    return Node->Children.end();
  }
};

} // namespace llvm

namespace MopIRImpl {
namespace MOP {
#endif

RecurringGraph::RecurringGraph(const std::vector<Mop*>& vertices,
                               const std::vector<Edge>& edges) {
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 开始构建图\n";
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 顶点数量: " << vertices.size() 
            << ", 边数量: " << edges.size() << "\n";
  
  // 预先分配空间，避免 vector 重新分配导致指针失效
  Vertices.reserve(vertices.size());
  
  // 创建顶点
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 创建顶点\n";
  for (size_t idx = 0; idx < vertices.size(); ++idx) {
    Mop* mop = vertices[idx];
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 创建顶点[" << idx 
              << "], MOP 指针: " << static_cast<const void*>(mop) << "\n";
    Vertices.emplace_back(mop);
    Mop2Vertex[mop] = &Vertices.back();
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: Mop2Vertex[" 
              << static_cast<const void*>(mop) << "] = " 
              << static_cast<const void*>(&Vertices.back()) << "\n";
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: Vertices.back().MopPtr = " 
              << static_cast<const void*>(Vertices.back().MopPtr) << "\n";
  }
  
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 顶点创建完成，Vertices 大小: " 
            << Vertices.size() << ", Mop2Vertex 大小: " << Mop2Vertex.size() << "\n";
  
  // 添加边（只添加未阻塞的边）
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 开始添加边\n";
  size_t addedEdges = 0;
  size_t blockedEdges = 0;
  size_t missingVertices = 0;
  
  for (size_t idx = 0; idx < edges.size(); ++idx) {
    const Edge& edge = edges[idx];
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 处理边[" << idx 
              << "]: killing=" << static_cast<const void*>(edge.Killing)
              << ", dead=" << static_cast<const void*>(edge.Dead)
              << ", blocked=" << (edge.Blocked ? "true" : "false") << "\n";
    
    if (edge.Blocked) {
      blockedEdges++;
      std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 边被阻塞，跳过\n";
      continue;
    }
    
    auto killingIt = Mop2Vertex.find(edge.Killing);
    auto deadIt = Mop2Vertex.find(edge.Dead);
    
    if (killingIt == Mop2Vertex.end()) {
      std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 错误！killing 顶点不在图中\n";
      missingVertices++;
      continue;
    }
    if (deadIt == Mop2Vertex.end()) {
      std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 错误！dead 顶点不在图中\n";
      missingVertices++;
      continue;
    }
    
    Vertex* killingV = killingIt->second;
    Vertex* deadV = deadIt->second;
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 添加边: killingV -> deadV\n";
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: killingV MOP=" 
              << static_cast<const void*>(killingV->MopPtr) << "\n";
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: deadV MOP=" 
              << static_cast<const void*>(deadV->MopPtr) << "\n";
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 调用 addChild\n";
    killingV->addChild(deadV);
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: addChild 完成\n";
    addedEdges++;
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: deadV->HasParent = " 
              << (deadV->HasParent ? "true" : "false") << "\n";
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: killingV->Children.size() = " 
              << killingV->Children.size() << "\n";
    std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 边[" << idx << "] 处理完成\n";
  }
  
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 边添加完成\n";
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 添加的边数: " << addedEdges 
            << ", 阻塞的边数: " << blockedEdges 
            << ", 缺失顶点的边数: " << missingVertices << "\n";
  
  // 打印每个顶点的入度（HasParent）和出度（Children.size()）
  std::cout << "[DEBUG] RecurringGraph::RecurringGraph: 打印顶点信息:\n";
  for (size_t idx = 0; idx < Vertices.size(); ++idx) {
    const Vertex& v = Vertices[idx];
    std::cout << "[DEBUG]   顶点[" << idx << "]: MOP=" 
              << static_cast<const void*>(v.MopPtr)
              << ", HasParent=" << (v.HasParent ? "true" : "false")
              << ", Children.size()=" << v.Children.size() << "\n";
  }
}

void RecurringGraph::fillDominatingSet(std::vector<Mop*>& domSet) {
#ifdef MOP_IR_USE_LLVM
  // 使用 LLVM 的 depth_first_ext 迭代器（与原项目一致）
  llvm::df_iterator_default_set<Vertex *, 16> Visited;
  
  std::cout << "[DEBUG] RecurringGraph::fillDominatingSet: 顶点数量: " << Vertices.size() << "\n";
  
  // 第一轮：添加所有入度为 0 的顶点（HasParent == false）
  int rootCount = 0;
  for (Vertex &V : Vertices) {
    if (V.HasParent) {
      continue;
    }
    std::cout << "[DEBUG] 找到根顶点（入度为 0），添加到支配集\n";
    domSet.push_back(V.MopPtr);
    rootCount++;
    
    // 使用 depth_first_ext 遍历从此顶点可达的所有顶点
    for (const Vertex *V2 : llvm::depth_first_ext(&V, Visited)) {
      (void)V2;  // 未使用，但需要遍历以标记为已访问
    }
  }
  std::cout << "[DEBUG] 第一轮：找到 " << rootCount << " 个根顶点，已访问 " << Visited.size() << " 个顶点\n";
  
  // 第二轮：添加未被遍历到的顶点（形成独立的连通分量）
  int isolatedCount = 0;
  for (Vertex &V : Vertices) {
    if (!V.HasParent || Visited.contains(&V)) {
      continue;
    }
    std::cout << "[DEBUG] 找到孤立顶点，添加到支配集\n";
    domSet.push_back(V.MopPtr);
    isolatedCount++;
    
    // 使用 depth_first_ext 遍历从此顶点可达的所有顶点
    for (const Vertex *V2 : llvm::depth_first_ext(&V, Visited)) {
      (void)V2;  // 未使用，但需要遍历以标记为已访问
    }
  }
  std::cout << "[DEBUG] 第二轮：找到 " << isolatedCount << " 个孤立顶点\n";
  std::cout << "[DEBUG] 最终支配集大小: " << domSet.size() << "\n";
#else
  // 非 LLVM 版本的简单实现（使用递归 DFS）
  std::unordered_set<Vertex*> visited;
  
  // 第一轮：添加所有入度为 0 的顶点
  for (Vertex& v : Vertices) {
    if (!v.HasParent) {
      domSet.push_back(v.MopPtr);
      // 简单的 DFS 遍历
      std::function<void(Vertex*)> dfs = [&](Vertex* v) {
        if (visited.find(v) != visited.end()) {
          return;
        }
        visited.insert(v);
        for (Vertex* child : v->Children) {
          dfs(child);
        }
      };
      dfs(&v);
    }
  }
  
  // 第二轮：添加未被遍历到的顶点
  for (Vertex& v : Vertices) {
    if (visited.find(&v) == visited.end()) {
      domSet.push_back(v.MopPtr);
      std::function<void(Vertex*)> dfs = [&](Vertex* v) {
        if (visited.find(v) != visited.end()) {
          return;
        }
        visited.insert(v);
        for (Vertex* child : v->Children) {
          dfs(child);
        }
      };
      dfs(&v);
    }
  }
#endif
}

} // namespace MOP
} // namespace MopIRImpl

