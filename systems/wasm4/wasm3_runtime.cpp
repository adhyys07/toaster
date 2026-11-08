#include "wasm4.h"


#ifdef TOASTER_HAVE_WASM3
#error "TOASTER_HAVE_WASM3 is defined but Wasm3Runtime is not implemented yet."
#endif

namespace emu::wasm4 {

std::unique_ptr<Runtime> makeWasm3Runtime() {
    return nullptr;
}

} // namespace emu::wasm4
