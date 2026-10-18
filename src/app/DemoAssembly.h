#pragma once

#include "nlhc/hull/HullAssembler.h"

namespace nlhc::demo {

// Synthetic three-segment ship. Used by --render-demo and the render smoke test until
// the extracted asset package is available.
AssemblyResult buildDemoAssembly();

} // namespace nlhc::demo
