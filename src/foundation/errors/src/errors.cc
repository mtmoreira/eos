// errors: error and result types (L0 foundation).
//
// Status and Result<T> are header-only by design; this translation unit
// exists so the module target carries the module's version anchor.
#include "eos/foundation/errors.h"

namespace eos::foundation {
// Anchor: bumped when the error surface changes incompatibly.
int errors_api_anchor = 1;
}  // namespace eos::foundation
