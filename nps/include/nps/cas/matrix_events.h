#ifndef NPS_CAS_MATRIX_EVENTS_H
#define NPS_CAS_MATRIX_EVENTS_H

#include "nps/core/matrix.h"

namespace nps {

class MatrixRowSink {
  public:
    virtual ~MatrixRowSink() = default;
    // Returning false stops delivery and withholds the final matrix.
    virtual bool row(NodeId before, NodeId after, const MatrixRowOperation &operation) = 0;
};

}

#endif
