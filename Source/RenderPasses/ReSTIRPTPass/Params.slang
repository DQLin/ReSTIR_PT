/***************************************************************************
 # Copyright (c) 2022, Daqi Lin.  All rights reserved.
 **************************************************************************/
#pragma once
#include "Utils/HostDeviceShared.slangh"

BEGIN_NAMESPACE_FALCOR

/** Enumeration of the atomic counters used.
*/
enum class Counters
{
    // TODO: Better naming
    kGeneralPaths = 0,
    kGeneralPathsProcessed,
    kSpecularPaths,
    kSpecularPathsProcessed,
    kDeltaReflectionPaths,
    kDeltaReflectionPathsProcessed,
    kDeltaTransmissionPaths,
    kDeltaTransmissionPathsProcessed,

    kGeneralQueued,
    kGeneralQueuedProcessed,

    kThreadCountSpecular,
    kThreadCountGeneral,
    kThreadCountDeltaReflection,
    kThreadCountDeltaTransmission,

    // Must be last
    kCount
};

/** Per-sample color format.
*/
enum class ColorFormat
    // TODO: Remove the ifdefs and the include when Slang supports enum type specifiers.
#ifdef HOST_CODE
    : uint32_t
#endif
{
    RGBA32F = 0,
    LogLuvHDR = 1,
};

enum class MISHeuristic
    // TODO: Remove the ifdefs and the include when Slang supports enum type specifiers.
#ifdef HOST_CODE
    : uint32_t
#endif
{
    Balance = 0,    ///< Balance heuristic.
    PowerTwo = 1,    ///< Power heuristic (exponent = 2.0).
    PowerExp = 2,    ///< Power heuristic (variable exponent).
};

// Define tile sizes in pixels.
// The frame is divided into tiles stored in scanline order, with pixels in tiles enumerated in Morton order.
static const uint2 kScreenTileDim = { 16, 16 };     ///< Screen-tile dimension in pixels.
static const uint2 kScreenTileBits = { 4, 4 };      ///< Bits needed to describe pixel position within a screen-tile.

// Define path configuration limits.
static const uint kMaxSamplesPerPixel = 64;         ///< Maximum supported sample count. We can use tiling to support large sample counts if needed.
static const uint kMaxFrameDimension = 4096;        ///< Maximum supported frame dimension in pixels along x or y. We can increase the bit allocation if needed.
static const uint kMaxBounces = 14;                ///< Maximum supported number of bounces per bounce category (value 255 is reserved for internal use). The resulting path length may be longer than this.
static const uint kMaxLightSamplesPerVertex = 8;    ///< Maximum number of shadow rays per path vertex for next-event estimation.

enum class ShiftMapping
#ifdef HOST_CODE
    : uint32_t
#endif
{
    Reconnection = 0,
    RandomReplay = 1,
    Hybrid = 2,
};

enum class LocalStrategy
#ifdef HOST_CODE
    : uint32_t
#endif
{
    None = 0x0,
    RoughnessCondition = 0x1,
    DistanceCondition = 0x2,
};

enum class ReSTIRMISKind
#ifdef HOST_CODE
    : uint32_t
#endif
{
    Constant = 0,
    Talbot = 1,
    Pairwise = 2,
    ConstantBinary = 3,
    ConstantBiased = 4
};

enum class SpatialReusePattern
#ifdef HOST_CODE
    : uint32_t
#endif
{
    Default = 0, // same as in ScreenSpaceReSTIR
    SmallWindow = 1,
};

enum class PathSamplingMode
{
    ReSTIR,
    PathReuse,
    PathTracing
};

enum class PathReusePattern
{
    Block,
    NRooks,
    NRooksShift
};


// Import static specialization constants.
#ifndef HOST_CODE
__exported import StaticParams;
__exported import Utils.Math.BitTricks;
#endif

/** Path tracer parmeters shared between host/device code.
*/
struct RestirPathTracerParams
{
    int     useFixedSeed = false;       ///< Use fixed random seed. This is useful for debugging.
    uint    seed = 0;                   ///< Random seed. This will get updated from the host depending on settings.
    uint    fixedSeed = 1;              ///< The seed to use when 'useFixedSeed' is enabled.
    float   lodBias = 0.f;              ///< LOD bias applied to secondary hits.

    // Runtime values
    uint2   frameDim = { 0, 0 };        ///< Frame dimension in pixels.
    uint2   screenTiles = { 0, 0 };     ///< Number of screen-tiles. Screen tiles may extend outside the frame.

    uint    frameCount = 0;             ///< Frames rendered. This is used as random seed.
    uint    localStrategyType = (uint)LocalStrategy::RoughnessCondition | (uint)LocalStrategy::DistanceCondition;
    int     rejectShiftBasedOnJacobian = false; // can be helpful for dynamic geometry
    float   jacobianRejectionThreshold = 10.f;

    // params for roughness condition in mixed strategy
    float   specularRoughnessThreshold = 0.2f; ///< Specular reflection events are only classified as specular if the material's roughness value is equal or smaller than this threshold. Otherwise they are classified diffuse.
    // params for distance condition in mixed strategy
    float   nearFieldDistance = 0.1f; //TODO: make this adaptive to spatial reuse size / scene size
    uint2 dummy; // padding

#ifndef HOST_CODE

    uint getReservoirOffset(const uint2 pixel)
    {
        uint2 tileID = pixel >> kScreenTileBits;

        uint stride = kScreenTileDim.x * kScreenTileDim.y;
        uint tileIdx = tileID.y * screenTiles.x + tileID.x;
        uint tileOffset = tileIdx * stride;

        uint tileBits = kScreenTileBits.x + kScreenTileBits.y;
        uint pixelIdx = interleave_16bit(pixel) & ((1 << tileBits) - 1); // TODO: Use interleave_8bit() if kScreenTileBits <= 4.
        return tileOffset + pixelIdx;
    }

#endif
};

END_NAMESPACE_FALCOR
