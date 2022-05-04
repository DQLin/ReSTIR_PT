/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

/** Enumeration of available debug outputs.
    Note: Keep in sync with definition in ScreenSpaceReSTIR.h
*/
enum class DebugOutput
{
    Disabled,
    Position,
    Depth,
    Normal,
    FaceNormal,
    DiffuseWeight,
    SpecularWeight,
    SpecularRoughness,
    PackedNormal,
    PackedDepth,
    InitialWeight,
    TemporalReuse,
    SpatialReuse,
    FinalSampleDir,
    FinalSampleDistance,
    FinalSampleLi,
};

enum class SpatialReusePattern
#ifdef HOST_CODE
    : uint32_t
#endif
{
    Default = 0, // same as in ScreenSpaceReSTIR
};

