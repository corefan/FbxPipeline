#pragma once

//
// STL
//

#include <iostream>
#include <memory>

//
// ThirdParty
//

#include <flatbuffers/flatbuffers.h>
#include <spdlog/spdlog.h>
#include <cxxopts.hpp>

//
// ThirdParty
// Math
//

#include <math.h>
#include <mathfu/constants.h>
#include <mathfu/matrix.h>
#include <mathfu/vector.h>
#include <mathfu/glsl_mappings.h>

namespace mathfu {
    template <>
    bool InRange< vec2 >( vec2 val, vec2 range_start, vec2 range_end );
    template <>
    bool InRange< vec3 >( vec3 val, vec3 range_start, vec3 range_end );
}

//
// FBX SDK
//

#include <fbxsdk.h>

//
// PVRTexTool Library
//

#include <PVRTexture.h>