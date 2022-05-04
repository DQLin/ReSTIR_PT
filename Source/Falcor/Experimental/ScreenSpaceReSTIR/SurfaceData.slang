/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
import Utils.Math.PackedFormats;

struct PackedSurfaceData
{
    uint3 pos;
    uint depth;
    uint normal;
    uint faceNormal;
    uint weightsAndLobes;
    uint _pad;
}

/** Per-pixel surface data.
*/
struct SurfaceData
{
    float3 pos;                 ///< Position in world-space.
    float depth;                ///< Depth from camera (-1 if invalid data).
    float3 normal;              ///< Shading normal in world-space.
    float3 faceNormal;          ///< Face normal in world-space.
    float diffuseWeight;        ///< Diffuse lobe weight.
    float specularWeight;       ///< Specular lobe weight.
    float specularRoughness;    ///< Specular lobe roughness (linear).
    uint  lobes;                ///< BSDF lobes

    static SurfaceData createFromPacked(const PackedSurfaceData packed)
    {
        SurfaceData surfaceData = {};
        surfaceData.unpack(packed);
        return surfaceData;
    }

    static SurfaceData createInvalid()
    {
        SurfaceData surfaceData = {};
        surfaceData.depth = -1.f;
        return surfaceData;
    }

    bool isValid()
    {
        return depth >= 0.f;
    }

    [mutating] void unpack(const PackedSurfaceData packed)
    {
        pos = asfloat(packed.pos);
        depth = asfloat(packed.depth);
        normal = decodeNormal2x16(packed.normal);
        faceNormal = decodeNormal2x16(packed.faceNormal);
        diffuseWeight = (packed.weightsAndLobes & 0xff) / float(0xff);
        specularWeight = ((packed.weightsAndLobes >> 8) & 0xff) / float(0xff);
        specularRoughness = ((packed.weightsAndLobes >> 16) & 0xff) / float(0xff);
        lobes = ((packed.weightsAndLobes >> 24) & 0xff);
    }

    PackedSurfaceData pack()
    {
        PackedSurfaceData packed = {};
        packed.pos = asuint(pos);
        packed.depth = asuint(depth);
        packed.normal = encodeNormal2x16(normal);
        packed.faceNormal = encodeNormal2x16(faceNormal);
        packed.weightsAndLobes = saturate(diffuseWeight) * 0xff;
        packed.weightsAndLobes |= uint((saturate(specularWeight) * 0xff)) << 8;
        packed.weightsAndLobes |= uint((saturate(specularRoughness) * 0xff)) << 16;
        packed.weightsAndLobes |= (lobes << 24);
        return packed;
    }
};

uint packNormalDepth(float4 normalDepth)
{
    return (encodeNormal2x8(normalDepth.xyz) << 16) | (f32tof16(normalDepth.w) & 0xffff);
}

float4 unpackNormalDepth(uint packed)
{
    return float4(decodeNormal2x8(packed >> 16), f16tof32(packed));
}
