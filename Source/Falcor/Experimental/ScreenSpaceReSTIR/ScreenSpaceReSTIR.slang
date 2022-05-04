/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
import SurfaceData;
import FinalSample;
import GIReservoir;

#ifndef SCREEN_SPACE_RESTIR_USE_DI
#define SCREEN_SPACE_RESTIR_USE_DI 0
#endif

#ifndef SCREEN_SPACE_RESTIR_USE_GI
#define SCREEN_SPACE_RESTIR_USE_GI 0
#endif

#ifndef SCREEN_SPACE_RESTIR_GI_DIFFUSE_THRESHOLD
#define SCREEN_SPACE_RESTIR_GI_DIFFUSE_THRESHOLD 0.17f
#endif

/** Public interface to the ReSTIR sampler.
*/
struct ScreenSpaceReSTIR
{
    static const bool kUseDI = SCREEN_SPACE_RESTIR_USE_DI;
    static const bool kUseGI = SCREEN_SPACE_RESTIR_USE_GI;
    static const float kDiffuseThreshold = SCREEN_SPACE_RESTIR_GI_DIFFUSE_THRESHOLD;

    // Common resources.
    RWStructuredBuffer<PackedSurfaceData> surfaceData;
    RWTexture2D<uint> normalDepth;

    // ReSTIR DI resources.
    StructuredBuffer<PackedFinalSample> finalSamples;

    // ReSTIR GI resources.
    RWStructuredBuffer<PackedGIReservoir> initialSamples;
    StructuredBuffer<PackedGIReservoir> prevReservoirs;    // Ping-pong buffers storing gather point samples
    StructuredBuffer<PackedGIReservoir> reservoirs;

    // Common parameters.
    uint2 frameDim;
    uint giReservoirCount;
    uint _pad;

    /** Get linear index for one pixel.
        \param[in] pixel Pixel coordinate.
        \return Returns linear index.
    */
    uint getPixelIndex(uint2 pixel) { return pixel.y * frameDim.x + pixel.x; }

    /** Determine whether a pixel should ignore ReSTIR GI.
        \param[in] roughness Roughness value.
        \param[in] diffuseWeight Diffuse component.
        \return Returns whether the pixel should ignore ReSTIR GI.
    */
    static bool ignoreReSTIRGI(float roughness, float diffuseWeight, float specularWeight)
    {
        // Consider light coming from normal direction, use GGX model to calculate.
        // outgoing radiance through normal direction, e.g. N=L=V.
        roughness = max(roughness, 0.01);
        float diffuseComponent = diffuseWeight;
        float specularComponent = (specularWeight * specularWeight) / (4 * roughness * roughness);
        float totalLight = (diffuseComponent + specularComponent);
        return diffuseComponent < ScreenSpaceReSTIR::kDiffuseThreshold * totalLight;
    }

    /** Set the per-pixel surface data required for screen-space ReSTIR.
        \param[in] pixel Pixel coordinate.
        \param[in] pos Position in world-space.
        \param[in] depth Depth from camera.
        \param[in] normal Shading normal in world-space.
        \param[in] faceNormal Face normal in world-space.
        \param[in] diffuseWeight Diffuse lobe weight.
        \param[in] specularWeight Specular lobe weight.
        \param[in] specularRoughness Specular roughness (linear).
    */
    void setSurfaceData(uint2 pixel, float3 pos, float depth, float3 normal, float3 faceNormal, float diffuseWeight, float specularWeight, float specularRoughness, uint lobes = 0)
    {
        // Store pixel data.
        SurfaceData sd;
        sd.pos = pos;
        sd.depth = depth;
        sd.normal = normal;
        sd.faceNormal = faceNormal;
        sd.diffuseWeight = diffuseWeight;
        sd.specularWeight = specularWeight;
        sd.specularRoughness = specularRoughness;
        sd.lobes = lobes;
        surfaceData[getPixelIndex(pixel)] = sd.pack();

        // Store compact normal depth information for spatial reuse.
        normalDepth[pixel] = packNormalDepth(float4(normal, depth));
    }

    /** Mark pixel as containing no valid shading data.
        \param[in] pixel Pixel coordinate.
    */
    void setInvalidSurfaceData(uint2 pixel)
    {
        surfaceData[getPixelIndex(pixel)] = SurfaceData::createInvalid().pack();
    }

    /** Get the final sample for the given pixel computed using ReSTIR DI.
        \param[in] pixel Pixel coordinate.
        \param[out] dir Direction to light in world-space.
        \param[out] distance Distance to light.
        \param[out] Li Weighted incident radiance.
        \return Returns true if a valid sample was returned.
    */
    bool getFinalSample(uint2 pixel, out float3 dir, out float distance, out float3 Li, bool useSecond = false)
    {
        FinalSample finalSample = FinalSample::createFromPacked(finalSamples[getPixelIndex(pixel)]);
        dir = finalSample.dir;
        distance = finalSample.distance;
        Li = finalSample.Li;
        return distance > 0.f;
    }

    /** Write initial sample for ReSTIR GI.
        \param[in] pixel Pixel coordinate.
        \param[in] creationPoint Position of visible point.
        \param[in] creationNormal Normal of visible point.
        \param[in] position Position of a secondary ray's hit point.
        \param[in] normal Normal of a secondary ray's hit point.
        \param[in] radiance Radiance of a secondary ray's hit point.
    */
    void setGIInitialSample(uint2 pixel, float3 creationPoint, float3 creationNormal, float3 position, float3 normal, float3 radiance, float srcPDF, int reservoirIndexOffset=0)
    {
        GIReservoir initialSample;
        initialSample.creationPoint = creationPoint;
        initialSample.creationNormal = creationNormal;
        initialSample.position = position;
        initialSample.normal = normal;
        if (dot(initialSample.normal, initialSample.creationPoint - initialSample.position) < 0)
        {
            initialSample.normal *= -1;
        }
        if (dot(initialSample.creationNormal, initialSample.position - initialSample.creationPoint) < 0)
        {
            initialSample.creationNormal *= -1;
        }
        initialSample.radiance = radiance;
        initialSample.avgWeight = srcPDF == 0.f ? 0.f : 1.f / srcPDF;// 1;
        initialSample.M = 1;
        initialSample.age = 0;
        uint sampleIndexOffset = reservoirIndexOffset;
        uint pixelIndex = getPixelIndex(pixel);
        writeReservoir(initialSamples, pixelIndex, sampleIndexOffset, frameDim.x * frameDim.y, initialSample);
    }

    void setGIInitialSampleFromReservoir(uint2 pixel, GIReservoir giReservoir, float wSum, float pHat, int M)
    {
        giReservoir.avgWeight = pHat == 0.f ? 0.f : wSum / (M * pHat);
        giReservoir.M = 1;
        giReservoir.age = 0;
        uint sampleIndexOffset = 0;//1;
        uint pixelIndex = getPixelIndex(pixel);
        writeReservoir(initialSamples, pixelIndex, sampleIndexOffset, frameDim.x * frameDim.y, giReservoir);
    }

    void addGISample(float3 creationPoint, float3 creationNormal, float3 position, float3 normal, float3 radiance, float targetPDF, float srcPDF, float rnd, inout float wSum, inout float pHat, inout int M, inout GIReservoir giReservoir)
    {
        GIReservoir initialSample;
        initialSample.creationPoint = creationPoint;
        initialSample.creationNormal = creationNormal;
        initialSample.position = position;
        initialSample.normal = normal;
        if (dot(initialSample.normal, initialSample.creationPoint - initialSample.position) < 0)
        {
            initialSample.normal *= -1;
        }
        if (dot(initialSample.creationNormal, initialSample.position - initialSample.creationPoint) < 0)
        {
            initialSample.creationNormal *= -1;
        }
        initialSample.radiance = radiance;
        initialSample.avgWeight = srcPDF == 0.f ? 0.f : 1.f / srcPDF;// 1;

        float weight = targetPDF * initialSample.avgWeight;
        wSum += weight;
        M += 1;

        // Conditionally update reservoir.
        float random = rnd;
        bool isUpdate = random * wSum <= weight;
        if (isUpdate)
        {
            pHat = targetPDF;
            giReservoir.position = initialSample.position;
            giReservoir.normal = initialSample.normal;
            giReservoir.radiance = initialSample.radiance;
            giReservoir.creationNormal = initialSample.creationNormal;
            giReservoir.creationPoint = initialSample.creationPoint;
        }
    }



    /** Get the final sample for the given pixel computed using ReSTIR GI.
        \param[in] pixel Pixel coordinate.
        \param[out] creationPoint Position of visible point.
        \param[out] creationNormal Normal of visible point.
        \param[out] position Position of a secondary ray's hit point.
        \param[out] normal Normal of a secondary ray's hit point.
        \param[out] radiance Radiance of a secondary ray's hit point.
        \param[out] avgWeight Sample weight.
    */
    void getGIFinalSample(uint2 pixel, uint reservoirID, out float3 creationPoint, out float3 creationNormal, out float3 position, out float3 normal, out float3 radiance, out float avgWeight)
    {
        uint pixelIndex = getPixelIndex(pixel);
        uint elementCount = frameDim.x * frameDim.y;
        uint sampleIndex = reservoirID * 2 + 1;
        GIReservoir reservoir = readReservoir(reservoirs, pixelIndex, sampleIndex, elementCount);
        creationPoint = reservoir.creationPoint;
        creationNormal = reservoir.creationNormal;
        position = reservoir.position;
        normal = reservoir.normal;
        radiance = reservoir.radiance;
        avgWeight = reservoir.avgWeight;
    }
};
