#pragma once

#if defined(_WIN32) && defined(NLHCCore_EXPORTS)
#  define NLHC_CORE_EXPORT __declspec(dllexport)
#elif defined(_WIN32)
#  define NLHC_CORE_EXPORT __declspec(dllimport)
#else
#  define NLHC_CORE_EXPORT
#endif
