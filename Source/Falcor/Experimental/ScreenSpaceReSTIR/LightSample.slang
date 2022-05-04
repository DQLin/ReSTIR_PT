/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
#include "Utils/Math/MathConstants.slangh"
import Scene.Scene;
import Utils.Math.MathHelpers;

#ifndef LIGHT_SAMPLE_ENV_INDEX_BITS
#error "LightSample bit allocations not defined!"
#endif

/** Light sample kind.
*/
enum class LightKind
{
    Env         = (0<<30),
    Emissive    = (1<<30),
    Analytic    = (2<<30),
};

typedef uint PackedLightSample;

/** Represents a light sample.

    Light samples are represented by a single 32-bit integer with the following encoding:
    - The light index (alias table index) is encoded in the lower bits.
    - The subtexel position is encoded in the middle bits.
    - The light kind (env, emissive, analytic) is encoded in the upper bits.

    To achieve maximum precision, different bit allocations are used for the different kinds
    of light samples. These bit allocations are determined by the actual scene data and
    provided as preprocessor defines.
*/
struct LightSample
{
    static const uint kInvalid = 0xffffffff;

    // The light sample kind is encoded in the 2 upper bits.
    static const uint kKindBits = 2;
    static const uint kKindOffset = 30;
    static const uint kKindMask = ((1 << kKindBits) - 1) << kKindOffset;

    static const uint kPositionIndexMask = ~kKindMask;

    // The env map index (alias table) is encoded in the lower bits.
    static const uint kEnvIndexBits = LIGHT_SAMPLE_ENV_INDEX_BITS;
    static const uint kEnvIndexMask = (1 << kEnvIndexBits) - 1;

    // Teh env map subtexel position is encoded in the remaining middle bits.
    static const uint kEnvPositionBits = LIGHT_SAMPLE_ENV_POSITION_BITS; // must be divisible by 2
    static const uint kEnvPositionOffset = kEnvIndexBits;
    static const uint kEnvPositionMask = ((1 << kEnvPositionBits) - 1) << kEnvPositionOffset;

    // The emissive index (alias table) is encoded in the lower bits.
    static const uint kEmissiveIndexBits = LIGHT_SAMPLE_EMISSIVE_INDEX_BITS;
    static const uint kEmissiveIndexMask = (1 << kEmissiveIndexBits) - 1;

    // The emissive uv position is encoded in the remaining middle bits.
    static const uint kEmissivePositionBits = LIGHT_SAMPLE_EMISSIVE_POSITION_BITS; // must be divisible by 2
    static const uint kEmissivePositionOffset = kEmissiveIndexBits;
    static const uint kEmissivePositionMask = ((1 << kEmissivePositionBits) - 1) << kEmissivePositionOffset;

    // The analytic light index (alias table) is encoded in the lower bits.
    static const uint kAnalyticIndexBits = LIGHT_SAMPLE_ANALYTIC_INDEX_BITS;
    static const uint kAnalyticIndexMask = (1 << kAnalyticIndexBits) - 1;

    static const uint kAnalyticPositionBits = LIGHT_SAMPLE_ANALYTIC_POSITION_BITS; // must be divisible by 2
    static const uint kAnalyticPositionOffset = kAnalyticIndexBits;
    static const uint kAnalyticPositionMask = ((1 << kAnalyticPositionBits) - 1) << kAnalyticPositionOffset;

    static uint encodePosition(const float2 position, const uint bitsPerComponent)
    {
        const uint mask = (1 << bitsPerComponent) - 1;
        uint2 tmp = uint2(position * float(1 << bitsPerComponent)) & mask;
        return tmp.x | (tmp.y << bitsPerComponent);
    }

    static float2 decodePosition(const uint position, const uint bitsPerComponent)
    {
        const uint mask = (1 << bitsPerComponent) - 1;
        return (uint2(position, position >> bitsPerComponent) & mask) / float(1 << bitsPerComponent);
    }

    uint data;

    static LightSample createInvalid()
    {
        LightSample ls;
        ls.data = kInvalid;
        return ls;
    }

    static LightSample createEnv(uint index, uint random)
    {
        LightSample ls;
        ls.data = uint(LightKind::Env) | (index & kEnvIndexMask) | (random & kEnvPositionMask);
        return ls;
    }

    static LightSample createEnv(const float3 dir)
    {
        uint2 dim = gScene.envMap.getDimensions();
        float2 uv = world_to_latlong_map(gScene.envMap.toLocal(dir));
        float2 texel = uv * dim;
        uint index = uint(texel.y) * dim.x + uint(texel.x);
        uint position = encodePosition(frac(texel), kEnvPositionBits / 2);

        LightSample ls;
        ls.data = uint(LightKind::Env) | (index & kEnvIndexMask) | (position << kEnvPositionOffset);
        return ls;
    }

    static LightSample createEmissive(uint index, uint random)
    {
        LightSample ls;
        ls.data = uint(LightKind::Emissive) | (index & kEmissiveIndexMask) | (random & kEmissivePositionMask);
        return ls;
    }

    static LightSample createEmissive(uint index, float2 uv)
    {
        uint position = encodePosition(uv, kEmissivePositionBits / 2);

        LightSample ls = {};
        ls.data = uint(LightKind::Emissive) | (index & kEmissiveIndexMask) | (position << kEmissivePositionOffset);
        return ls;
    }

    static LightSample createAnalytic(uint index, uint random)
    {
        LightSample ls;
        ls.data = uint(LightKind::Analytic) | (index & kAnalyticIndexMask) | (random & kAnalyticPositionMask);
        return ls;
    }

    static LightSample createFromPacked(const PackedLightSample packed)
    {
        LightSample ls;
        ls.unpack(packed);
        return ls;
    }

    PackedLightSample pack()
    {
        return PackedLightSample(data);
    }

    [mutating] void unpack(const PackedLightSample packed)
    {
        data = uint(packed);
    }

    bool isValid()
    {
        return data != kInvalid;
    }

    LightKind getKind()
    {
        return LightKind(data & kKindMask);
    }

    uint getEnvIndex() { return data & kEnvIndexMask; }
    uint getEmissiveIndex() { return data & kEmissiveIndexMask; }
    uint getAnalyticIndex() { return data & kAnalyticIndexMask; }

    uint getIndex()
    {
        switch (getKind())
        {
        case LightKind::Env: return getEnvIndex();
        case LightKind::Emissive: return getEmissiveIndex();
        case LightKind::Analytic: return getAnalyticIndex();
        }
        return 0;
    }

    float2 getEnvPosition() { return decodePosition(data >> kEnvPositionOffset, kEnvPositionBits / 2); }
    float2 getEmissivePosition() { return decodePosition(data >> kEmissivePositionOffset, kEmissivePositionBits / 2); }
    float2 getAnalyticPosition() { return decodePosition(data >> kAnalyticPositionOffset, kAnalyticPositionBits / 2); }

    /** Get the sampled world-space direction.
        Only valid if light sample is of kind LightKind::Env.
    */
    float3 getEnvWorldDir()
    {
        uint texel = getEnvIndex();
        uint2 dim = gScene.envMap.getDimensions();
        float2 uv = (float2(texel % dim.x, texel / dim.x) + getEnvPosition()) / dim;
        return gScene.envMap.toWorld(latlong_map_to_world(uv));
    }

    /** Get the sampled UV coordinates on the emissive triangle.
        Only valid if light sample is of kind LightKind::Emissive.
    */
    float2 getEmissiveUV()
    {
        float2 uv = getEmissivePosition();
        return uv.x + uv.y > 1.f ? 1.f - uv : uv;
    }

    /** Get the sampled barycentric coordinates on the emissive triangle.
        Only valid if light sample is of type LightKind::Emissive.
    */
    float3 getEmissiveBarycentrics()
    {
        float2 uv = getEmissiveUV();
        return float3(1.f - uv.x - uv.y, uv.x, uv.y);
    }
}
