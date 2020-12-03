// Copyright 2020 the V8 project authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef V8_COMPILER_LOOP_UNROLLING_H_
#define V8_COMPILER_LOOP_UNROLLING_H_

// Loop unrolling is an optimization that copies the body of a loop and creates
// a fresh loop, whose iteration corresponds to 2 or more iterations of the
// initial loop. Beginning with a loop as follows:

//             E
//             |                 A
//             |                 |                     (backedges)
//             | +---------------|---------------------------------+
//             | | +-------------|-------------------------------+ |
//             | | |             | +--------+                    | |
//             | | |             | | +----+ |                    | |
//             | | |             | | |    | |                    | |
//           ( Loop )<-------- ( phiA )   | |                    | |
//              |                 |       | |                    | |
//      ((======P=================U=======|=|=====))             | |
//      ((                                | |     ))             | |
//      ((        X <---------------------+ |     ))             | |
//      ((                                  |     ))             | |
//      ((     body                         |     ))             | |
//      ((                                  |     ))             | |
//      ((        Y <-----------------------+     ))             | |
//      ((                                        ))             | |
//      ((===K====L====M==========================))             | |
//           |    |    |                                         | |
//           |    |    +-----------------------------------------+ |
//           |    +------------------------------------------------+
//           |
//          exit

// The body of the loop is duplicated so that all nodes considered "inside" the
// loop (e.g. {P, U, X, Y, K, L, M}) have a corresponding copies in the second
// iteration (e.g. {P', U', X', Y', K', L', M'}). What were considered backedges
// of the loop correspond to edges from the in-nodes of the second iteration to
// the out-nodes of the first iteration, employing merge and phi nodes as
// appropriate. Similarly, any exits from the first and second loop iterations
// need to be merged together. E.g. unrolling the loop twice results in the
// following graph:

//             E
//             |                 A
//             |                 |
//             | +---------------|---------------------------------+
//             | | +-------------|-------------------------------+ |
//             | | |             | +----------------------+      | |
//             | | |             | | +------------------+ |      | |
//             | | |             | | |                  | |      | |
//           ( Loop )<-------- ( phiA )                 | |      | |
//              |                 |                     | |      | |
//      ((======P=================U===============))    | |      | |
//      ((                                        ))    | |      | |
//      ((        X <--------------+              ))    | |      | |
//      ((                         |              ))    | |      | |
//      ((     iteration1          |              ))    | |      | |
//      ((                         |              ))    | |      | |
//      ((        Y <------------+ |              ))    | |      | |
//      ((                       | |              ))    | |      | |
//      ((==K==L========M========|=|==============))    | |      | |
//          |  |        |        | |                    | |      | |
//   +------+  | +------+        | |                    | |      | |
//   |         | |               | |                    | |      | |
//   |        Merge <----------- phi                    | |      | |
//   |          |                 |                     | |      | |
//   |  ((======P'================U'==============))    | |      | |
//   |  ((                                        ))    | |      | |
//   |  ((        X' <----------------------------------+ |      | |
//   |  ((                                        ))      |      | |
//   |  ((     iteration2                         ))      |      | |
//   |  ((                                        ))      |      | |
//   |  ((        Y' <------------------------------------+      | |
//   |  ((                                        ))             | |
//   |  ((===K'===L'===M'=========================))             | |
//   |       |    |    |                                         | |
//   |       |    |    +-----------------------------------------+ |
//   +--+ +--+    +------------------------------------------------+
//      | |
//     Merge
//       |
//      exit

// Note that the boxes ((===)) above are not explicitly represented in the
// graph, but are instead computed by the {LoopFinder}.

#include "src/compiler/common-operator.h"
#include "src/compiler/loop-analysis.h"

namespace v8 {
namespace internal {
namespace compiler {

class LoopUnroller {
 public:
  LoopUnroller(Graph* graph, CommonOperatorBuilder* common, LoopTree* loop_tree,
               Zone* tmp_zone, SourcePositionTable* source_positions,
               NodeOriginTable* node_origins)
      : graph_(graph),
        common_(common),
        loop_tree_(loop_tree),
        tmp_zone_(tmp_zone),
        source_positions_(source_positions),
        node_origins_(node_origins) {}

  void Unroll();

 private:
  void UnrollLoop(const LoopTree::Loop*);
  Graph* const graph_;
  CommonOperatorBuilder* const common_;
  LoopTree* const loop_tree_;
  Zone* const tmp_zone_;
  SourcePositionTable* const source_positions_;
  NodeOriginTable* const node_origins_;
};

}  // namespace compiler
}  // namespace internal
}  // namespace v8

#endif  // V8_COMPILER_LOOP_UNROLLING_H_
