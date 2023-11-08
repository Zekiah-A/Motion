#pragma once
#include "Config.h"

#if defined(MOTION_EXPORTS)
    #define MOTION_C_API MOTION_C_API_EXPORT
    #define MOTION_CXX_API MOTION_CXX_API_EXPORT
#else
    #define MOTION_C_API MOTION_C_API_IMPORT
    #define MOTION_CXX_API MOTION_CXX_API_IMPORT
#endif
