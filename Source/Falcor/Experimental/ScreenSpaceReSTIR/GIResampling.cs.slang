/***************************************************************************
 # Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

import Scene.RaytracingInline;
import Utils.Attributes;
import Params;
import Lights;
import Scene.HitInfo;
import Scene.ShadingData;
import Utils.Debug.PixelDebug;
import Utils.Sampling.TinyUniformSampleGenerator;
import ScreenSpaceReSTIR;
import GIReservoir;
import SurfaceData;
import EvalContext;
import Utils.Geometry.GeometryHelpers;
import Utils.Color.ColorHelpers;
import Rendering.Materials.StandardMaterial;

#ifndef RESTIR_TARGET_FUNCTION
#define RESTIR_TARGET_FUNCTION 1
#endif

#ifndef NEIGHBOR_OFFSET_COUNT
#define NEIGHBOR_OFFSET_COUNT 0
#endif

#define RESTIR_MIS_WEIGHT 0

#ifndef RESTIR_MODE
#define RESTIR_MODE 3
#endif

#ifndef RESTIR_GI_USE_RESTIR_N
#define RESTIR_GI_USE_RESTIR_N 0
#endif


struct GIResampling
{
    enum class ReSTIRMode
    {
        InputOnly                   = 0,    ///< Output input samples directly.
        TemporalOnly                = 1,    ///< Output temporal reuse result.
        TemporalAndBiasedSpatial    = 2,    ///< Output temporal followed by biased spatial reuse result.
        TemporalAndUnbiasedSpatial  = 3     ///< Output temporal followed by unbiased spatial reuse result.
    };

    // Static configuration.
    static const bool kUseAlphaTest = USE_ALPHA_TEST;
    static const int kTargetFunction = RESTIR_TARGET_FUNCTION;
    static const ReSTIRMode kReSTIRMode = ReSTIRMode(RESTIR_MODE);
    static const int kReSTIRMISWeight = 0;
    static const float kGGXRoughnessClamp = 0.15f; ///< GGX roughness clamp threshold to improve stability.
    static const uint kNeighborOffsetCount = NEIGHBOR_OFFSET_COUNT;
    static const uint kNeighborOffsetMask = kNeighborOffsetCount - 1;
    static const bool kReSTIRGIUseReSTIRN = RESTIR_GI_USE_RESTIR_N;

    // Resources.
    [root] StructuredBuffer<PackedSurfaceData> surfaceData;
    [root] StructuredBuffer<PackedSurfaceData> prevSurfaceData;

    float4x4 viewProj;
    float4x4 prevViewProj;
    float3 cameraOrigin;
    float3 prevCameraOrigin;
    float normalThreshold;
    float depthThreshold;

    uint2 frameDim = { 0, 0 };          ///< Frame dimension in pixels.
    uint frameIndex;                    ///< Current frame index.

    uint temporalMaxSamples;            ///< Maximum M value for temporal reservoir.
    uint spatialMaxSamples;             ///< Maximum M value for spatial reservoir.
    uint reservoirCount;                ///< Number of reservoirs per pixel.
    uint maxSampleAge;                  ///< Maximum frames that a sample can survive.
    bool forceClearReservoirs;          ///< Clear temporal and spatial reservoirs.

    Texture2D<PackedHitInfo> vbuffer;   ///< Fullscreen V-buffer for the primary hits.
    Texture2D<uint> sampleCount;        ///< Optional input sample count buffer. Only valid when kSamplesPerPixel == 0.
    Texture2D<uint> sampleOffset;       ///< Output offset into per-sample buffers. Only valid when kSamplesPerPixel == 0.

    Texture2D<float2> motionVectors;

    RWTexture2D<float4> outputColor;    ///< Output resolved color.

    Texture2D<uint> normalDepth;
    Texture2D<uint> prevNormalDepth;

    RWStructuredBuffer<PackedGIReservoir> initialSamples;

    Texture1D<float2> neighborOffsets;

    // Ping-pong buffers storing gather point samples.
    StructuredBuffer<PackedGIReservoir> prevReservoirs;
    RWStructuredBuffer<PackedGIReservoir> reservoirs;

    float spatialWeightClampThreshold;
    float jacobianClampThreshold;
    bool enableSpatialWeightClamping;
    bool enableJacobianClamping;
    bool enableTemporalJacobian;

    static const float largeFloat = 1e20f;

    uint getPixelIndex(uint2 pixel) { return pixel.y * frameDim.x + pixel.x; }

    float evalTargetFunction(float3 radiance, float3 normal, float3 position, float3 samplePosition, EvalContext evalContext)
    {
        if (kTargetFunction == 1)
        {
            float3 L = normalize(samplePosition - position);            
            float3 fCos = max(0.1f, evalContext.evalBRDF(L) * saturate(dot(normal, L)));
            float pdf = luminance(radiance * fCos);
            return pdf;
        }
        else
        {
            return luminance(radiance);
        }
    }

    uint toLinearIndex(uint2 pixel)
    {
        return pixel.y * frameDim.x + pixel.x;
    }

    bool evalSegmentVisibility(float3 pos, float3 endPoint)
    {
        float3 offset = endPoint - pos;
        float tMax = length(offset);
        float3 dir = offset / tMax;
        RayDesc ray = { pos, 0.001f, dir, 0.999* tMax };
        SceneRayQuery<kUseAlphaTest> sceneRayQuery;
        return sceneRayQuery.traceVisibilityRay(ray, RAY_FLAG_NONE, 0xff);
    }

    /** Randomly update reservoir.
        \param[in] weight The input reservoir's weight.
        \param[in] srcReservoir The input reservoir.
        \param[inout] sg Random number generator.
        \param[inout] weightSum The output reservoir's weight.
        \param[inout] dstReservoir The output reservoir.
    */
    bool updateReservoir(float weight, GIReservoir srcReservoir, inout TinyUniformSampleGenerator sg, inout float weightSum, inout GIReservoir dstReservoir)
    {
        weightSum += weight;
        dstReservoir.M += srcReservoir.M;

        // Conditionally update reservoir.
        float random = sampleNext1D(sg);
        bool isUpdate = random * weightSum <= weight;
        if (isUpdate)
        {
            dstReservoir.position = srcReservoir.position;
            dstReservoir.normal = srcReservoir.normal;
            dstReservoir.radiance = srcReservoir.radiance;
            dstReservoir.age = srcReservoir.age;
        }
        return isUpdate;
    }

    void execute(const uint2 pixel)
    {
        if (any(pixel >= frameDim))
        {
            return;
        }

        const uint pixelIndex = getPixelIndex(pixel);

        TinyUniformSampleGenerator sg = TinyUniformSampleGenerator(pixel, frameIndex);

        // Setup evaluation context.
        SurfaceData sfd = SurfaceData::createFromPacked(surfaceData[pixelIndex]);
        EvalContext evalContext = EvalContext::create(pixel, frameDim, sfd);
        if (!evalContext.isValid())
        {
            return;
        }

        // Discard translucent and pure specular pixels.
        if ((evalContext.lobes & ((uint)LobeType::Transmission)) != 0 ||
            ScreenSpaceReSTIR::ignoreReSTIRGI(evalContext.ggxAlpha, evalContext.diffuseWeight, evalContext.specularWeight))
        {
            return;
        }
        // Clamp roughness to improve stability.
        evalContext.ggxAlpha = max(kGGXRoughnessClamp, evalContext.ggxAlpha);

        float4 posClip = mul(float4(evalContext.pos, 1), viewProj);
        float3 worldPosition = computeRayOrigin(sfd.pos, -sfd.faceNormal);
        float3 worldNormal = evalContext.N;


        // Reproject pixel position.
        float4 prevClip = mul(float4(worldPosition, 1.f), prevViewProj);
        float3 prevScreen = prevClip.xyz / prevClip.w;
        float2 prevUV = prevScreen.xy * float2(0.5f, -0.5f) + 0.5f;
        uint2 prevID = clamp(prevUV * frameDim.xy, 0, frameDim.xy - 1);
        uint prevIdx = toLinearIndex(prevID);

        // Validate temporal reprojection.
        bool isPrevValid = evalContext.isValid() && frameIndex > 0 && !forceClearReservoirs && all(prevUV > 0.f) && all(prevUV < 1.f);
        if (isPrevValid)
        {
            uint2 prevPixel = prevUV * frameDim.xy;
            const uint prevPixelIndex = getPixelIndex(prevPixel);

            // Setup evaluation context.
            EvalContext prevEvalContext = EvalContext::create(prevPixelIndex, frameDim, SurfaceData::createFromPacked(prevSurfaceData[prevPixelIndex]));

            isPrevValid &= length(prevEvalContext.pos - evalContext.pos) < 0.1f && dot(prevEvalContext.N, evalContext.N) > 0.8f;
            float rand = sampleNext1D(sg);
            float viewDepth = length(worldPosition - cameraOrigin);
            float prevViewDepth = length(worldPosition - prevCameraOrigin);
            if (viewDepth / prevViewDepth < 0.98f && rand < 0.15f)
            {
                isPrevValid = false;
            }
        }

        for (uint localIndex = 0; localIndex < reservoirCount; ++localIndex)
        {
            // Read latest samples generated by create gather point shader.
            uint initialSampleIndex = kReSTIRGIUseReSTIRN ? localIndex : 0;// 1;
            GIReservoir initialSample = readReservoir(initialSamples, pixelIndex, initialSampleIndex, frameDim.x * frameDim.y);

            int indexOffsetBegin = 2 * localIndex;
            int temporalSourceIndex = indexOffsetBegin;
            int spatialSourceIndex = indexOffsetBegin + 1;
            int temporalTargetIndex = temporalSourceIndex;
            int spatialTargetIndex = spatialSourceIndex;

            // Use input samples only.
            if (kReSTIRMode == ReSTIRMode::InputOnly)
            {
                writeReservoir(reservoirs, pixelIndex, temporalTargetIndex, frameDim.x * frameDim.y, initialSample);
                writeReservoir(reservoirs, pixelIndex, spatialTargetIndex, frameDim.x * frameDim.y, initialSample);
                continue;
            }

            // Temporal Reuse.
            // Read temporal reservoir.
            GIReservoir temporalReservoir = readReservoir(prevReservoirs, prevIdx, temporalSourceIndex, frameDim.x * frameDim.y);
            if (length(temporalReservoir.creationPoint - worldPosition) > 1.f)
            {
                isPrevValid = false;
            }

            float3 radiance = initialSample.radiance;
            temporalReservoir.M = clamp(temporalReservoir.M, 0, temporalMaxSamples);
            if (!isPrevValid || temporalReservoir.age > maxSampleAge)
            {
                temporalReservoir.M = 0;
            }

            // Compute reuse weight.
            float tf = evalTargetFunction(temporalReservoir.radiance, worldNormal, worldPosition, temporalReservoir.position, evalContext);

            float jacobian = 1.f;

            if (enableTemporalJacobian)
            {
                float3 offsetB = temporalReservoir.position - temporalReservoir.creationPoint;
                float3 offsetA = temporalReservoir.position - worldPosition;

                float RB2 = dot(offsetB, offsetB);
                float RA2 = dot(offsetA, offsetA);
                offsetB = normalize(offsetB);
                offsetA = normalize(offsetA);
                float cosA = dot(worldNormal, offsetA);
                float cosB = dot(temporalReservoir.creationNormal, offsetB);
                float cosPhiA = -dot(offsetA, temporalReservoir.normal);
                float cosPhiB = -dot(offsetB, temporalReservoir.normal);

                if (cosA <= 0.f || cosPhiA <= 0.f || RA2 <= 0.f || RB2 <= 0.f || cosB <= 0.f || cosPhiB <= 0.f)
                {
                    tf = 0.f;
                }

                // assuming visible

                // Calculate Jacobian determinant and weight.
                const float maxJacobian = enableJacobianClamping ? jacobianClampThreshold : largeFloat;
                jacobian = RA2 * cosPhiB <= 0.f ? 0.f : clamp(RB2 * cosPhiA / (RA2 * cosPhiB), 0.f, maxJacobian);

                tf *= jacobian;
            }

            float wSum = max(0.f, temporalReservoir.avgWeight) * temporalReservoir.M * tf;
            print("tf", tf);

            float pNew = evalTargetFunction(radiance, worldNormal, worldPosition, initialSample.position, evalContext);
            float wi = initialSample.avgWeight <= 0.f ? 0.f : pNew * initialSample.avgWeight;

            // Update temporal reservoir.
            bool selectedNew = updateReservoir(wi, initialSample, sg, wSum, temporalReservoir);

            print("wSum", wSum);
            float avgWSum = wSum / temporalReservoir.M;
            pNew = evalTargetFunction(temporalReservoir.radiance, temporalReservoir.creationNormal, temporalReservoir.creationPoint, temporalReservoir.position, evalContext);
            temporalReservoir.avgWeight = pNew <= 0.f ? 0.f : avgWSum / pNew;
            print("avgWSum", avgWSum);
            print("pNew", pNew);
            print("temporalReservoir.avgWeight", temporalReservoir.avgWeight);
            temporalReservoir.M = clamp(temporalReservoir.M, 0, temporalMaxSamples);
            temporalReservoir.age++;
            temporalReservoir.creationPoint = worldPosition;
            temporalReservoir.creationNormal = worldNormal;

            writeReservoir(reservoirs, pixelIndex, temporalTargetIndex, frameDim.x * frameDim.y, temporalReservoir);

            if (kReSTIRMode == ReSTIRMode::TemporalOnly)
            {
                writeReservoir(reservoirs, pixelIndex, spatialTargetIndex, frameDim.x* frameDim.y, temporalReservoir);
                continue;
            }

            // Spatial Reuse.
            // Read spatial reservoir.

            GIReservoir spatialReservoir = readReservoir(prevReservoirs, prevIdx, spatialSourceIndex, frameDim.x * frameDim.y);

            spatialReservoir.M = max(0, spatialReservoir.M);
            if (!isPrevValid || spatialReservoir.age > maxSampleAge)
            {
                spatialReservoir.M = 0;
            }

            // Add near sample to s reservoir.
            uint prevIdx2;
            GIReservoir neighborReservoir = temporalReservoir;
            float wSumS = max(0.f, spatialReservoir.avgWeight) * spatialReservoir.M * evalTargetFunction(spatialReservoir.radiance, spatialReservoir.creationNormal, spatialReservoir.creationPoint, spatialReservoir.position, evalContext);

            // Determine maximum iteration based on current sample count.
            // If sample count is low, use more iterations to boost sample count.
            const float fastReuseRatio = 0.5f;
            const float fastReuseThreshold = spatialMaxSamples * fastReuseRatio;
            const int normalIteration = 3;
            const int fastReuseIteration = 10;

            int maxIteration = spatialReservoir.M > fastReuseThreshold ? normalIteration : fastReuseIteration;

            const float searchRadiusRatio = 0.1f;
            float searchRadius = frameDim.x * searchRadiusRatio;

            // Initialize reuse history.
            float3 positionList[10];
            float3 normalList[10];
            int MList[10];
            int nReuse = 0;
            int reuseID = 0;
            positionList[nReuse] = worldPosition;
            normalList[nReuse] = worldNormal;
            MList[nReuse] = spatialReservoir.M;
            nReuse++;
            spatialReservoir.creationPoint = worldPosition;
            spatialReservoir.creationNormal = worldNormal;

            // Search and reuse neighbor samples.
            const uint startIndex = sampleNext1D(sg) * kNeighborOffsetCount;

            for (int i = 0; i < maxIteration; i++)
            {
                // Get search radius.
                const float radiusShrinkRatio = 0.5f;
                const float minSearchRadius = 10.f;
                searchRadius = max(searchRadius* radiusShrinkRatio, minSearchRadius);
                // Randomly sample a neighbor.
                float3 randOffset = sampleNext3D(sg);
                randOffset = randOffset * 2.f - 1.f;
                int2 neighborID = prevID + randOffset.xy * searchRadius;

                uint2 boundary = frameDim.xy - 1;
                neighborID.x = neighborID.x < 0 ? -neighborID.x : (neighborID.x > boundary.x ? 2 * boundary.x - neighborID.x : neighborID.x);
                neighborID.y = neighborID.y < 0 ? -neighborID.y : (neighborID.y > boundary.y ? 2 * boundary.y - neighborID.y : neighborID.y);

                // Check geometric similarity.
                float4 neighborNormalDepth = unpackNormalDepth(prevNormalDepth[neighborID]);
                if (!evalContext.isValidNeighbor(neighborNormalDepth, normalThreshold, depthThreshold))
                    continue;

                // Read neighbor's spatial reservoir.
                prevIdx2 = toLinearIndex(neighborID);

                bool bReuseSpatialSample = (kReSTIRMode == ReSTIRMode::TemporalAndUnbiasedSpatial ? i % 2 == 1 : 0);
                neighborReservoir = readReservoir(prevReservoirs, prevIdx2, bReuseSpatialSample ? spatialSourceIndex : temporalSourceIndex, frameDim.x * frameDim.y);

                // Discard black samples.
                if (neighborReservoir.M <= 0)
                {
                    continue;
                }

                // Calculate target function.
                float3 offsetB = neighborReservoir.position - neighborReservoir.creationPoint;
                float3 offsetA = neighborReservoir.position - worldPosition;
                float pNewTN = evalTargetFunction(neighborReservoir.radiance, worldNormal, worldPosition, neighborReservoir.position, evalContext);
                // Discard back-face.
                if (dot(worldNormal, offsetA) <= 0.f)
                {
                    pNewTN = 0.f;
                }

                float RB2 = dot(offsetB, offsetB);
                float RA2 = dot(offsetA, offsetA);
                offsetB = normalize(offsetB);
                offsetA = normalize(offsetA);
                float cosA = dot(worldNormal, offsetA);
                float cosB = dot(neighborReservoir.creationNormal, offsetB);
                float cosPhiA = -dot(offsetA, neighborReservoir.normal);
                float cosPhiB = -dot(offsetB, neighborReservoir.normal);
                if (cosB <= 0.f || cosPhiB <= 0.f)
                {
                    continue;
                }
                if (cosA <= 0.f || cosPhiA <= 0.f || RA2 <= 0.f || RB2 <= 0.f)
                {
                    pNewTN = 0.f;
                }

                bool isVisible = evalSegmentVisibility(computeRayOrigin(worldPosition, worldNormal), neighborReservoir.position);
                if (!isVisible)
                {
                    pNewTN = 0.f;
                }

                // Calculate Jacobian determinant and weight.
                const float maxJacobian = enableJacobianClamping ? jacobianClampThreshold : largeFloat;
                float jacobian = RA2 * cosPhiB <= 0.f ? 0.f : clamp(RB2 * cosPhiA / (RA2 * cosPhiB), 0.f, maxJacobian);
                float wiTN = clamp(neighborReservoir.avgWeight * pNewTN * neighborReservoir.M * jacobian, 0.f, largeFloat);

                // Conditionally update spatial reservoir.
                bool isUpdated = updateReservoir(wiTN, neighborReservoir, sg, wSumS, spatialReservoir);
                if (isUpdated) reuseID = nReuse;

                // Update reuse history.
                positionList[nReuse] = neighborReservoir.creationPoint;
                normalList[nReuse] = neighborReservoir.creationNormal;
                MList[nReuse] = neighborReservoir.M;
                nReuse++;

                // Expand search radius.
                const float radiusExpandRatio = 3.f;
                searchRadius *= radiusExpandRatio;
            }

            // Calculate weight of spatial reuse.
            float m;
            if (kReSTIRMode == ReSTIRMode::TemporalAndBiasedSpatial)
            {
                m = spatialReservoir.M <= 0.f ? 0.f : 1.f / float(spatialReservoir.M);
            }
            else if (kReSTIRMode == ReSTIRMode::TemporalAndUnbiasedSpatial)
            {
                // Trace extra rays if unbiased spatial reuse is enabled.
                float totalWeight = 0.f;
                float chosenWeight = 0.f;
                int nValid = 0;
                int Z = 0;
                for (int i = 0; i < nReuse; i++)
                {
                    bool isVisible = true;
                    bool shouldTest = true;
                    float3 directionVec = spatialReservoir.position - positionList[i];
                    if (dot(directionVec, normalList[i]) < 0.f)
                    {
                        shouldTest = false;
                        isVisible = false;
                    }
                    if (shouldTest)
                    {
                        isVisible = evalSegmentVisibility(computeRayOrigin(positionList[i], normalList[i]), spatialReservoir.position);
                    }
                    // Discard new sample if it is occluded.
                    if (isVisible)
                    {
                        if (kReSTIRMISWeight == 0)
                            totalWeight += MList[i];
                        else
                        {
                            float misWeight = saturate(dot(normalList[i], normalize(directionVec))) * luminance(spatialReservoir.radiance);
                            totalWeight += misWeight * MList[i];
                            if (reuseID == i)
                            {
                                chosenWeight = misWeight;
                            }
                        }
                        nValid++;
                    }
                    else if (i == 0)
                    {
                        break;
                    }
                }

                if (kReSTIRMISWeight == 0) m = totalWeight <= 0.f ? 0.f : 1.f / totalWeight;
                else m = totalWeight <= 0.f ? 0.f : chosenWeight / totalWeight;
            }

            pNew = evalTargetFunction(spatialReservoir.radiance, worldNormal, worldPosition, spatialReservoir.position, evalContext);
            float mWeight = pNew <= 0.f ? 0.f : 1.f / pNew * m;
            spatialReservoir.M = clamp(spatialReservoir.M, 0, spatialMaxSamples);
            float W = wSumS * mWeight;
            // TODO: add UI control for this
            const float maxSpatialWeight = enableSpatialWeightClamping ? spatialWeightClampThreshold : largeFloat;
            spatialReservoir.avgWeight = clamp(W, 0.f, maxSpatialWeight);
            spatialReservoir.age++;

            // Write spatial reservoir.
            writeReservoir(reservoirs, pixelIndex, spatialTargetIndex, frameDim.x* frameDim.y, spatialReservoir);
        }
    }
};

cbuffer CB
{
    GIResampling gGIResampling;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    printSetPixel(dispatchThreadId.xy);
    gGIResampling.execute(dispatchThreadId.xy);
}
