/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
__exported import LightSample;

// Use compact packing. Changing this requires restarting the renderer.
#define USE_COMPACT_RESERVOIR 0

#if USE_COMPACT_RESERVOIR
typedef uint2 PackedReservoir;
#else
typedef uint4 PackedReservoir;
#endif

/** Represents a reservoir containing a weighted light sample obtained through resampled importance sampling.
*/
struct Reservoir
{
    LightSample lightSample;    ///< Selected light sample.
    uint M;                     ///< Number of samples seen so far.
    float weight;               ///< Probabilistic weight.

    static Reservoir createEmpty()
    {
        return { LightSample::createInvalid(), 0.f, 0.f };
    }

    static Reservoir createFromPacked(const PackedReservoir packed)
    {
        Reservoir reservoir = {};
        reservoir.unpack(packed);
        return reservoir;
    }

    bool isValid()
    {
        return lightSample.isValid();
    }

    PackedReservoir pack()
    {
        PackedReservoir packed;
#if USE_COMPACT_RESERVOIR
        packed.x = uint(lightSample.pack());
        packed.y = f32tof16(weight) | (clamp(M, 0, 0xffff) << 16);
#else
        packed.x = lightSample.pack();
        packed.y = asuint(weight);
        packed.z = M;
        packed.w = 0;
#endif
        return packed;
    }

    [mutating] void unpack(const PackedReservoir packed)
    {
#if USE_COMPACT_RESERVOIR
        lightSample.unpack(packed.x);
        weight = f16tof32(packed.y & 0xffff);
        M = packed.y >> 16;
#else
        lightSample.unpack(packed.x);
        weight = asfloat(packed.y);
        M = packed.z;
#endif
        // Replace with empty reservoir if invalid.
        if (isinf(weight) || isnan(weight))
        {
            lightSample = LightSample::createInvalid();
            weight = 0.f;
            M = 0.f;
        }
    }
};
