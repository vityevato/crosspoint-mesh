#include "T4InputEngine.h"

#include <Logging.h>

#include "T4Dictionary.h"

namespace t4 {

// ── Explicit template instantiation for production ──────────────────────
// All method bodies are inline in T4InputEngine.h.
// This forces the compiler to verify every method compiles with T4Dictionary
// and generates a single copy of the vtable-free code.
template class T4InputEngine<T4Dictionary>;

}  // namespace t4
