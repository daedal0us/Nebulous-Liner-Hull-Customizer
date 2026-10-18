#pragma once

#include "nlhc/hull/HullAssembler.h"

namespace nlhc::demo {

// Synthetic three-segment ship. Used by --render-demo and the render smoke test until
// the extracted asset package is available.
AssemblyResult buildDemoAssembly();

// A single fixed-colour prop: used to prove livery colours never touch unpainted materials.
AssemblyResult buildFixedOnlyAssembly();

} // namespace nlhc::demo
