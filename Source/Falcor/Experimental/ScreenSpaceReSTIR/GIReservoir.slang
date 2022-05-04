/***************************************************************************
 # Copyright (c) 2021, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
import Utils.Math.PackedFormats;

struct PackedGIReservoir
{
    uint4 creationGeometry;         ///< Visible point's position and normal.
    uint4 hitGeometry;              ///< Hit point's position and normal.
    uint4 lightInfo;                ///< Reservoir information.
};

struct GIReservoir
{
    float3 creationPoint;           ///< Visible point's position.
    float3 creationNormal;          ///< Visible point's normal.
    float3 position;                ///< Hit point's position.
    float3 normal;                  ///< Hit point's normal.
    float3 radiance;                ///< Chosen sample's radiance.
    int    M;                       ///< Input sample count.
    float avgWeight;                ///< Average weight for chosen sample.
    uint  age;                      ///< Number of frames the sample has survived.

    static GIReservoir createEmpty()
    {
        GIReservoir reservoir = {};
        reservoir.creationPoint = float3(0.f);
        reservoir.creationNormal = float3(0.f, 0.f, 1.f);
        reservoir.position = float3(0.f, 0.f, 1.f);
        reservoir.normal = float3(0.f, 0.f, -1.f);
        reservoir.radiance = float3(0.f);
        reservoir.M = 0;
        reservoir.avgWeight = 1.f;
        reservoir.age = 0;
        return reservoir;
    }

    static GIReservoir createFromPacked(const PackedGIReservoir packed)
    {
        GIReservoir reservoir = {};
        reservoir.unpack(packed);
        return reservoir;
    }

    PackedGIReservoir pack()
    {
        PackedGIReservoir packed;
        packed.creationGeometry.xyz = asuint(creationPoint);
        packed.creationGeometry.w = encodeNormal2x16(creationNormal);
        packed.hitGeometry.xyz = asuint(position);
        packed.hitGeometry.w = encodeNormal2x16(normal);
        packed.lightInfo.x = f32tof16(radiance.x) | (f32tof16(radiance.y) << 16);
        packed.lightInfo.y = f32tof16(radiance.z) | (M << 16);
        packed.lightInfo.z = asuint(avgWeight);
        packed.lightInfo.w = age;
        return packed;
    }

    [mutating] void unpack(const PackedGIReservoir packed)
    {
        creationPoint = asfloat(packed.creationGeometry.xyz);
        creationNormal = decodeNormal2x16(packed.creationGeometry.w);
        position = asfloat(packed.hitGeometry.xyz);
        normal = decodeNormal2x16(packed.hitGeometry.w);
        radiance.x = f16tof32(packed.lightInfo.x & 0xffff);
        radiance.y = f16tof32(packed.lightInfo.x >> 16);
        radiance.z = f16tof32(packed.lightInfo.y & 0xffff);
        M = (packed.lightInfo.y >> 16);
        avgWeight = asfloat(packed.lightInfo.z);
        age = packed.lightInfo.w;
    }
};

GIReservoir readReservoir(StructuredBuffer<PackedGIReservoir> reservoirBuffer, uint baseIndex, uint sampleIndex, uint elementCount)
{
    uint index = baseIndex + sampleIndex * elementCount;
    return GIReservoir.createFromPacked(reservoirBuffer[index]);
}

GIReservoir readReservoir(RWStructuredBuffer<PackedGIReservoir> reservoirBuffer, uint baseIndex, uint sampleIndex, uint elementCount)
{
    uint index = baseIndex + sampleIndex * elementCount;
    return GIReservoir.createFromPacked(reservoirBuffer[index]);
}

void writeReservoir(RWStructuredBuffer<PackedGIReservoir> reservoirBuffer, uint baseIndex, uint sampleIndex, uint elementCount, GIReservoir reservoir)
{
    uint index = baseIndex + sampleIndex * elementCount;
    reservoirBuffer[index] = reservoir.pack();
}
