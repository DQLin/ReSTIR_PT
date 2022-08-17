/***************************************************************************
 # Copyright (c) 2022, Daqi Lin.  All rights reserved.
 **************************************************************************/
import Params;
import Shift;
import Scene.HitInfo;
import PathReservoir;
import PathTracer;
import Scene.Scene;
import Utils.Debug.PixelDebug;
import Rendering.Utils.PixelStats;
import Utils.Sampling.TinyUniformSampleGenerator;
import Utils.Math.Ray;
import Utils.Math.MathHelpers;
import LoadShadingData;
import Rendering.Materials.MaterialShading;
import RenderPasses.Shared.Denoising.NRDData;
import RenderPasses.Shared.Denoising.NRDBuffers;
import RenderPasses.Shared.Denoising.NRDConstants;
import NRDHelpers;

struct PathReusePass
{
    static const uint kNeighborOffsetCount = NEIGHBOR_OFFSET_COUNT;
    static const uint kNeighborOffsetMask = kNeighborOffsetCount - 1;

    // Resources
    RestirPathTracerParams params;                        ///< Runtime parameters.

    Texture2D<PackedHitInfo> vbuffer;                     ///< Fullscreen V-buffer for the primary hits.
    Texture1D<float2> neighborOffsets;

    ByteAddressBuffer nRooksPattern;

    RWTexture2D<float4> outputColor;                      ///< Output resolved color.
    StructuredBuffer<PathReservoir> outputReservoirs;     // reservoir from previous pass
    RWStructuredBuffer<PathReservoir> temporalReservoirs; // resulting reservoir for next frame
    StructuredBuffer<PixelReconnectionData> reconnectionDataBuffer;
    StructuredBuffer<PathReuseMISWeight> misWeightBuffer;

    RWTexture2D<float4> outputNRDDiffuseRadianceHitDist;    ///< Output resolved diffuse color in .rgb and hit distance in .a for NRD. Only valid if kOutputNRDData == true.
    RWTexture2D<float4> outputNRDSpecularRadianceHitDist;   ///< Output resolved specular color in .rgb and hit distance in .a for NRD. Only valid if kOutputNRDData == true.
    RWTexture2D<float4> outputNRDResidualRadianceHitDist;   ///< Output resolved residual color in .rgb and hit distance in .a for NRD. Only valid if kOutputNRDData == true.
    Texture2D<float4> primaryHitEmission;                     ///< Output per-pixel primary hit emission. Only valid if kOutputNRDData == true.

    int gSpatialRoundId;
    int  gNumSpatialRounds;
    uint gSpatialReusePattern;
    // for default pattern
    int gNeighborCount;
    float gGatherRadius;
    // for small window
    int gSmallWindowRadius;

    bool gIsLastRound;

    bool gFeatureBasedRejection;

    uint gSppId;

    Texture2D<float4> directLighting;  
    bool useDirectLighting;

    bool isValidPackedHitInfo(PackedHitInfo packed)
    {
        return packed.x != 0;
    }

    bool isValidScreenRegion(int2 pixel) { return all(pixel >= 0 && pixel < params.frameDim); }

    bool isValidGeometry(ShadingData centralSd, ShadingData neighborSd)
    {
        if (!gFeatureBasedRejection) return true;
        float3 camPos = gScene.camera.data.posW;
        float centralDist = distance(camPos, centralSd.posW);
        float neighborDist = distance(camPos, neighborSd.posW);
        return dot(centralSd.N, neighborSd.N) >= 0.5f && abs(centralDist - neighborDist) < 0.1f * centralDist;
    }

    ShadingData getPixelShadingData(int2 pixel, out PackedHitInfo PrimaryHitPacked)
    {
        ShadingData sd = {};
        Ray ray = gScene.camera.computeRayPinhole(pixel, params.frameDim);
        PrimaryHitPacked = vbuffer[pixel];
        if (isValidPackedHitInfo(PrimaryHitPacked))
        {
            HitInfo PrimaryHit; PrimaryHit.unpack(PrimaryHitPacked);

            TriangleHit th = PrimaryHit.getTriangleHit();
            sd = loadShadingData(PrimaryHit, -ray.dir, true);
        }
        return sd;
    }

    int getNeighborCount()
    {
        if (PathSamplingMode(kPathSamplingMode) == PathSamplingMode::PathReuse) return 16; //include self
        else
        {
            if (SpatialReusePattern(gSpatialReusePattern) == SpatialReusePattern::Default)
            {
                return gNeighborCount; // does not include self
            }
            else
            {
                int smallWindowDiameter = 2 * gSmallWindowRadius + 1;
                return smallWindowDiameter * smallWindowDiameter; // count self, but not use self as neighbor
            }
        }
    }

    int2 getPathReuseNextNeighborPixel(int4 NRookQuery, int2 pixel, int i)
    {
        // 
        if (PathReusePattern(gSpatialReusePattern) == PathReusePattern::Block)
        {
            int2 block = pixel / 4;
            return 4 * block + int2(i % 4, i / 4);
        }
        else
        {
            // fetch neighbors
            int groupId = NRookQuery.z;
            uint2 words = nRooksPattern.Load2(NRookQuery.w * 256 + groupId * 8);
            int rowId = (words[i / 8] >> (4 * (i % 8))) & 0xF;

            return int2(i, rowId) - NRookQuery.xy + pixel;
        }
    }

    int2 getNextNeighborPixel(const uint startIndex, int2 pixel, int i)
    {
        int2 neighborPixel = int2(0,0);

        if (SpatialReusePattern(gSpatialReusePattern) == SpatialReusePattern::Default)
        {
            uint neighborIndex = (startIndex + i) & kNeighborOffsetMask;
            neighborPixel = pixel + int2(neighborOffsets[neighborIndex] * gGatherRadius);
        }
        else if (SpatialReusePattern(gSpatialReusePattern) == SpatialReusePattern::SmallWindow)
        {
            int smallWindowDiameter = 2 * gSmallWindowRadius + 1;
            neighborPixel = pixel + int2(-gSmallWindowRadius + (i % smallWindowDiameter),
                -gSmallWindowRadius + (i / smallWindowDiameter));
            if (all(neighborPixel == pixel)) neighborPixel = int2(-1);
        }
 
        return neighborPixel;
    }

    void ReSTIR(const uint2 pixel)
    {
        var sg = TinyUniformSampleGenerator(pixel, (kCandidateSamples + 1 + gNumSpatialRounds) * params.seed + kCandidateSamples + 1 + gSpatialRoundId);

        // Compute offset into per-sample buffers. All samples are stored consecutively at this offset.
        const uint centralOffset = params.getReservoirOffset(pixel);
        PathReservoir dstReservoir = outputReservoirs[centralOffset];
        PathReservoir centralReservoir = dstReservoir;

        float3 initialColor = dstReservoir.F * dstReservoir.weight;

        float centralM = dstReservoir.M;
        PackedHitInfo centralPrimaryHitPacked;
        ShadingData centralPrimarySd = getPixelShadingData(pixel, centralPrimaryHitPacked);

        if (!isValidPackedHitInfo(centralPrimaryHitPacked)) return;

        if (PathSamplingMode(kPathSamplingMode) == PathSamplingMode::PathReuse ||
            (ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::Talbot || ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::Pairwise))
        {
            dstReservoir.init();
        }
        else
        {
            dstReservoir.prepareMerging();
        }

        float3 color = 0.f;
        ReconnectionData dummyRcData;
        dummyRcData.Init();

        int neighborCount = getNeighborCount();
        const uint startIndex = sampleNext1D(sg) * kNeighborOffsetCount;

#if BPR
        ////////////////////////////////
        /// BEKAERT-STYLE PATH REUSE ///
        ////////////////////////////////
        {
            dstReservoir.F = 0.f;
            int blockShiftOffset;
            int4 NRookQuery = 0;
            int2 pixelInBlock;
            if (PathReusePattern(gSpatialReusePattern) == PathReusePattern::NRooks) blockShiftOffset = 16;
            else blockShiftOffset = params.seed % 16;
            if (PathReusePattern(gSpatialReusePattern) == PathReusePattern::Block)
                pixelInBlock.x = 4 * (pixel.y % 4) + (pixel.x % 4);
            else
            {
                int2 block = (pixel - blockShiftOffset + 16) / 16;
                var NRookSg = TinyUniformSampleGenerator(block, (kCandidateSamples + 1 + gNumSpatialRounds) * params.seed + kCandidateSamples);
                // randomly select one N-rooks pattern from 256 patterns
                const uint patternIndex = min(255, sampleNext1D(NRookSg) * 256);
                pixelInBlock = (pixel - blockShiftOffset + 16) % 16;
                int pixelInBlockId = pixelInBlock.y * 16 + pixelInBlock.x;
                int byteAddress = pixelInBlockId / 2;
                uint word = nRooksPattern.Load(patternIndex * 256 + 128 + 4 * (byteAddress / 4));
                int groupId = (word >> (8 * (byteAddress % 4)) >> (4 * (pixelInBlockId % 2))) & 0xF;

                NRookQuery = int4(pixelInBlock, groupId, patternIndex);
            }

            for (int i = 0; i < neighborCount; ++i)
            {
                int2 neighborPixel = getPathReuseNextNeighborPixel(NRookQuery, pixel, i);

                if (!isValidScreenRegion(neighborPixel)) continue;
                PathReservoir neighborReservoir = outputReservoirs[params.getReservoirOffset(neighborPixel)];

                PackedHitInfo neighborPrimaryHitPacked;
                ShadingData neighborPrimarySd = getPixelShadingData(neighborPixel, neighborPrimaryHitPacked);
                if (!isValidPackedHitInfo(neighborPrimaryHitPacked)) continue;

                float p_sum = 0;
                float p_self = 0;
                float dstJacobian;

                PathReservoir tempDstReservoir = dstReservoir;

                if (i == pixelInBlock.x) 
                {
                    tempDstReservoir = neighborReservoir;
                    tempDstReservoir.rcVertexIrradiance[0] = tempDstReservoir.F;
                    tempDstReservoir.rcVertexIrradiance[1] = tempDstReservoir.cachedJacobian;
                    tempDstReservoir.cachedJacobian[0] = 1;
                    tempDstReservoir.cachedJacobian[1] = 1;
                }
                else
                {
                    shiftAndMergeReservoir(params, false, dstJacobian, centralPrimaryHitPacked, centralPrimarySd, tempDstReservoir,
                        neighborPrimarySd, neighborReservoir, dummyRcData, true, sg, true, 1.f, true); // "true" means hypothetically selected as the sample
                }

                {
                    float misWeight = misWeightBuffer[params.getReservoirOffset(neighborPixel)].rcBSDFMISWeight;
                    float jacobian = tempDstReservoir.cachedJacobian[0];
                    float jacobianNEE = tempDstReservoir.cachedJacobian[1];
                    dstReservoir.F += tempDstReservoir.rcVertexIrradiance[0] * jacobian * misWeight;
                    float misWeightNEE = misWeightBuffer[params.getReservoirOffset(neighborPixel)].rcNEEMISWeight;
                    dstReservoir.F += tempDstReservoir.rcVertexIrradiance[1] * jacobianNEE * misWeightNEE;
                }
            }

            color = dstReservoir.F;
        }
#else
        ///////////////////
        /// TALBOT RMIS ///
        ///////////////////
        if (ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::Talbot)
        {
            for (int i = -1; i < neighborCount; ++i)
            {
                int2 neighborPixel = i == -1 ? pixel : getNextNeighborPixel(startIndex, pixel, i);
                if (!isValidScreenRegion(neighborPixel)) continue;
                PathReservoir neighborReservoir = outputReservoirs[params.getReservoirOffset(neighborPixel)];

                PackedHitInfo neighborPrimaryHitPacked;
                ShadingData neighborPrimarySd = getPixelShadingData(neighborPixel, neighborPrimaryHitPacked);
                if (!isValidPackedHitInfo(neighborPrimaryHitPacked)) continue;
                if (!isValidGeometry(centralPrimarySd, neighborPrimarySd)) continue;

                float p_sum = 0;
                float p_self = 0;
                float dstJacobian;

                PathReservoir tempDstReservoir = dstReservoir;
                bool possibleToBeSelected = false;

                if (i == -1)
                {
                    tempDstReservoir = neighborReservoir;
                    dstJacobian = 1.f;
                    possibleToBeSelected = neighborReservoir.weight > 0;
                }
                else
                {
                    possibleToBeSelected = shiftAndMergeReservoir(params, false, dstJacobian, centralPrimaryHitPacked, centralPrimarySd, tempDstReservoir,
                        neighborPrimarySd, neighborReservoir, dummyRcData, true, sg, true, 1.f, true); // "true" means hypothetically selected as the sample
                }

                if (possibleToBeSelected)
                {
                    for (int j = -1; j < neighborCount; ++j)
                    {
                        if (j == -1)
                        {
                            float cur_p = PathReservoir::computeWeight(tempDstReservoir.F) * centralM;
                            p_sum += cur_p;
                            if (i == -1) p_self = cur_p;
                        }
                        else
                        {
                            if (i == j)
                            {
                                p_self = PathReservoir::computeWeight(neighborReservoir.F) / dstJacobian * neighborReservoir.M;
                                p_sum += p_self;
                                continue;
                            }

                            int2 tneighborPixel = j == -1 ? pixel : getNextNeighborPixel(startIndex, pixel, j);
                            if (!isValidScreenRegion(tneighborPixel)) continue;
                            PathReservoir tneighborReservoir = outputReservoirs[params.getReservoirOffset(tneighborPixel)];
                            PackedHitInfo tneighborPrimaryHitPacked;
                            ShadingData tneighborPrimarySd = getPixelShadingData(tneighborPixel, tneighborPrimaryHitPacked);
                            if (!isValidPackedHitInfo(tneighborPrimaryHitPacked)) continue;
                            if (!isValidGeometry(centralPrimarySd, tneighborPrimarySd)) continue;

                            float tneighborJacobian;
                            float3 tneighborIntegrand = computeShiftedIntegrand(params, tneighborJacobian, tneighborPrimaryHitPacked, tneighborPrimarySd,
                                centralPrimarySd, tempDstReservoir, dummyRcData, true);
                            float p_ = PathReservoir::computeWeight(tneighborIntegrand) * tneighborJacobian;
                            p_sum += p_ * tneighborReservoir.M;
                        }
                    }
                }

                float misWeight = p_sum == 0.f ? 0.f : p_self / p_sum;
                mergeReservoirWithResamplingMIS(params, tempDstReservoir.F, dstJacobian, dstReservoir, tempDstReservoir, neighborReservoir, sg, true, misWeight);
            }
            if (dstReservoir.weight > 0)
            {
                dstReservoir.finalizeGRIS();
            }
            color = dstReservoir.F * dstReservoir.weight;
        }
        /////////////////////
        /// PAIRWISE RMIS ///
        /////////////////////
        else if (ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::Pairwise)
        {
            int validNeighborCount = 0;
            float canonicalWeight = 1;

            for (int i = 0; i < neighborCount; ++i)
            {
                int2 neighborPixel = getNextNeighborPixel(startIndex, pixel, i);

                if (!isValidScreenRegion(neighborPixel)) continue;

                PackedHitInfo neighborPrimaryHitPacked;
                ShadingData neighborPrimarySd = getPixelShadingData(neighborPixel, neighborPrimaryHitPacked);
                if (!isValidPackedHitInfo(neighborPrimaryHitPacked)) continue;
                if (!isValidGeometry(centralPrimarySd, neighborPrimarySd)) continue;

                PathReservoir neighborReservoir = outputReservoirs[params.getReservoirOffset(neighborPixel)];

                float dstJacobian;

                validNeighborCount++;

                float prefix_approxPdf = 0.f;

                float prefixJacobian;

                ReconnectionData rcData;
                if (ShiftMapping(kShiftStrategy) == ShiftMapping::Hybrid && centralReservoir.pathFlags.rcVertexLength() > 1)
                    rcData = reconnectionDataBuffer[centralOffset].data[2 * i];
                else
                    rcData = dummyRcData;

                float3 prefixIntegrand = computeShiftedIntegrand(params, prefixJacobian, neighborPrimaryHitPacked, neighborPrimarySd,
                    centralPrimarySd, centralReservoir, rcData, true);

                prefix_approxPdf = PathReservoir::computeWeight(prefixIntegrand) * prefixJacobian;

                canonicalWeight += 1;
                if (prefix_approxPdf > 0.f) 
                    canonicalWeight -= prefix_approxPdf * neighborReservoir.M / (prefix_approxPdf * neighborReservoir.M + centralReservoir.M * PathReservoir::computeWeight(centralReservoir.F) / (neighborCount));

                PathReservoir tempDstReservoir = dstReservoir;

                if (ShiftMapping(kShiftStrategy) == ShiftMapping::Hybrid && neighborReservoir.pathFlags.rcVertexLength() > 1)
                    rcData = reconnectionDataBuffer[centralOffset].data[2 * i + 1];
                else
                    rcData = dummyRcData;

                bool possibleToBeSelected = shiftAndMergeReservoir(params, false, dstJacobian, centralPrimaryHitPacked, centralPrimarySd, tempDstReservoir,
                    neighborPrimarySd, neighborReservoir, rcData, true, sg, true, 1.f, true); // "true" means hypothetically selected as the sample

                float neighborWeight = 0.f;

                if (possibleToBeSelected)
                {
                    neighborWeight = PathReservoir::computeWeight(neighborReservoir.F) / dstJacobian * neighborReservoir.M
                        / ((PathReservoir::computeWeight(neighborReservoir.F) / dstJacobian) * neighborReservoir.M + PathReservoir::computeWeight(tempDstReservoir.F) * centralReservoir.M / (neighborCount));
                    if (isnan(neighborWeight) || isinf(neighborWeight)) neighborWeight = 0.f;
                }

                mergeReservoirWithResamplingMIS(params, tempDstReservoir.F, dstJacobian, dstReservoir, tempDstReservoir, neighborReservoir, sg, true, neighborWeight);
            }

            mergeReservoirWithResamplingMIS(params, centralReservoir.F, 1.f, dstReservoir, centralReservoir, centralReservoir, sg, true, canonicalWeight);

            if (dstReservoir.weight > 0)
            {
                dstReservoir.finalizeGRIS();
                dstReservoir.weight /= (validNeighborCount + 1); // compensate for the fact that pairwise resampling MIS was not divided by (k+1)
            }
            color = dstReservoir.F * dstReservoir.weight;
        }
        /////////////////////
        /// CONSTANT RMIS ///
        /////////////////////
        else
        {
            uint2 chosenPixel = pixel;
            int chosen_i = -1;
            float chosen_Jacobian = 1.f;

            for (uint i = 0; i < neighborCount; ++i)
            {
                int2 neighborPixel = getNextNeighborPixel(startIndex, pixel, i);
                if (!isValidScreenRegion(neighborPixel)) continue;

                PathReservoir neighborReservoir = outputReservoirs[params.getReservoirOffset(neighborPixel)];
                PackedHitInfo neighborPrimaryHitPacked;
                ShadingData neighborPrimarySd = getPixelShadingData(neighborPixel, neighborPrimaryHitPacked);
                if (!isValidPackedHitInfo(neighborPrimaryHitPacked)) continue;
                if (!isValidGeometry(centralPrimarySd, neighborPrimarySd)) continue;

                float dstJacobian;

                bool selected = shiftAndMergeReservoir(params, false, dstJacobian, centralPrimaryHitPacked, centralPrimarySd, dstReservoir,
                    neighborPrimarySd, neighborReservoir, dummyRcData, true, sg, true);

                if (selected)
                {
                    chosen_i = i;
                    chosenPixel = neighborPixel;
                    chosen_Jacobian = dstJacobian;
                }
            }

            if (dstReservoir.weight > 0)
            {
                float count = 0;

                // Evaluate the MIS weight by shifting back.

                PackedHitInfo chosenPrimaryHitPacked;
                ShadingData chosenPrimarySd = getPixelShadingData(chosenPixel, chosenPrimaryHitPacked);
                PathReservoir chosenReservoir = outputReservoirs[params.getReservoirOffset(chosenPixel)];

                float chosen_approxPdf = 0.f;
                float sum_approxPdf = 0.f;

                // Evaluate MIS weight.

                if (ReSTIRMISKind(kSpatialReSTIRMISKind) != ReSTIRMISKind::ConstantBiased)
                {
                    if (chosen_i == -1)
                    {
                        chosen_approxPdf = PathReservoir::computeWeight(chosenReservoir.F);
                        sum_approxPdf += chosen_approxPdf * centralM;
                        count += centralM;
                    }
                    else
                    {
                        sum_approxPdf += PathReservoir::computeWeight(dstReservoir.F) * centralM;
                        count += centralM;
                    }

                    for (uint i = 0; i < neighborCount; ++i)
                    {
                        if (i == chosen_i)
                        {
                            chosen_approxPdf = PathReservoir::computeWeight(chosenReservoir.F) / chosen_Jacobian;
                            sum_approxPdf += chosen_approxPdf * chosenReservoir.M;
                            count += chosenReservoir.M; 

                            continue;
                        }

                        int2 prefixPixel = getNextNeighborPixel(startIndex, pixel, i);
                        float prefixJacobian;
                        if (!isValidScreenRegion(prefixPixel)) continue;

                        PathReservoir prefixReservoir = outputReservoirs[params.getReservoirOffset(prefixPixel)];
                        PackedHitInfo prefixPrimaryHitPacked;
                        ShadingData prefixPrimarySd = getPixelShadingData(prefixPixel, prefixPrimaryHitPacked);
                        if (!isValidPackedHitInfo(prefixPrimaryHitPacked)) continue;
                        if (!isValidGeometry(centralPrimarySd, prefixPrimarySd)) continue;

                        float prefix_approxPdf;
                        int shiftCount = 0;

                        float3 prefixIntegrand = computeShiftedIntegrand(params, prefixJacobian, prefixPrimaryHitPacked, prefixPrimarySd,
                            centralPrimarySd, dstReservoir, dummyRcData, true);
                        prefix_approxPdf = PathReservoir::computeWeight(prefixIntegrand) * prefixJacobian;

                        if (prefix_approxPdf > 0.f) count += prefixReservoir.M;

                        sum_approxPdf += prefix_approxPdf * prefixReservoir.M;
                    }

                    float misWeight = 0.f;

                    if (sum_approxPdf > 0.f)
                    {
                        if (ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::Constant) misWeight = chosen_approxPdf / sum_approxPdf;
                        else misWeight = 1.f / count;
                    }

                    dstReservoir.weight *= dstReservoir.M * misWeight;

                }
                else
                {
                    if (chosen_i == -1)
                    {
                        int smallWindowDiameter = 2 * gSmallWindowRadius + 1;
                        chosen_i = (smallWindowDiameter * smallWindowDiameter) / 2;
                    }
                }

                dstReservoir.finalizeRIS();

                color = dstReservoir.F * dstReservoir.weight;
            }
        }
#endif

        if (dstReservoir.weight < 0.f) dstReservoir.weight = 0.f;
        if (isnan(dstReservoir.weight) || isinf(dstReservoir.weight)) dstReservoir.weight = 0.f;

        if (PathSamplingMode(kPathSamplingMode) != PathSamplingMode::PathReuse)
            temporalReservoirs[centralOffset] = dstReservoir;

        if (any(isnan(color) || isinf(color) || color < 0.f)) color = 0.f;
        if (gIsLastRound)
        {
            if (useDirectLighting)
            {
                color += directLighting[pixel].rgb;
                if (any(isnan(color) || isinf(color))) color = 0.f;
            }

            outputColor[pixel] += float4(color / kSamplesPerPixel, 1.f);

            // Output Denoiser Data (developed for NRD v2.12, doesn't guarantee to work with the latest version)
            if (kOutputNRDData && PathSamplingMode(kPathSamplingMode) == PathSamplingMode::ReSTIR)
            {
                float3 diffuseRadiance = 0.f;
                float3 specularRadiance = 0.f;
                float3 residualRadiance = 0.f;
                float hitDist = 0.f;
                float3 reflectance = 0.f;
                float3 radiance = 0.f;
                NRDPathType pathType = NRDPathType::Residual; 

                //
                if (ShiftMapping(kShiftStrategy) == ShiftMapping::Reconnection ||
                    ShiftMapping(kShiftStrategy) == ShiftMapping::Hybrid && dstReservoir.pathFlags.rcVertexLength() == 1)  //rcVertexHit must exists
                {
                    if (dstReservoir.rcVertexHit.instanceID == 0xffffffff)
                    {
                        hitDist = kNRDInvalidPathLength;
                    }
                    else
                    {
                        bool isDiffusePrimaryHit = !dstReservoir.pathFlags.decodeIsSpecularBounce(true);
                        bool isSpecularPrimaryHit = dstReservoir.pathFlags.decodeIsSpecularBounce(true) && !dstReservoir.pathFlags.decodeIsDeltaEvent(true);

                        reflectance = computeSampleReflectance(kUseNRDDemodulation, isDiffusePrimaryHit, isSpecularPrimaryHit, centralPrimarySd);

                        ShadingData RcVertexSd = loadShadingDataWithPrevVertexPosition(dstReservoir.rcVertexHit.getHitInfo(), centralPrimarySd.posW, false);
                        hitDist = length(RcVertexSd.posW - centralPrimarySd.posW);

                        pathType = NRDPathType::Residual;
                        if (isDiffusePrimaryHit) pathType = NRDPathType::Diffuse;
                        else if (isSpecularPrimaryHit) pathType = NRDPathType::Specular;

                        radiance = dstReservoir.F * dstReservoir.weight;
                    }
                }
                else
                {
                    HitInfo centralPrimaryHit; centralPrimaryHit.unpack(centralPrimaryHitPacked);
                    Ray ray = Ray(gScene.camera.getPosition(), -centralPrimarySd.V, 0, kRayTMax);

                    traceDenoiserData(params, centralPrimaryHitPacked, centralPrimarySd,
                        dstReservoir.pathFlags, dstReservoir.initRandomSeed, hitDist, pathType, reflectance);

                    radiance = dstReservoir.F * dstReservoir.weight;
                }

                if (useDirectLighting)
                    radiance += directLighting[pixel].rgb;


                reflectance = max(0.04f, reflectance);
                float3 emission = primaryHitEmission[pixel].rgb;
                float3 demodulatedRadiance = max(0.f, (radiance - emission)) / reflectance;

                switch (pathType)
                {
                case NRDPathType::Diffuse:
                    diffuseRadiance += demodulatedRadiance;
                    break;
                case NRDPathType::Specular:
                    specularRadiance += demodulatedRadiance;
                    break;
                default:
                    // Do not demodulate residual.
                    residualRadiance += radiance;
                    break;
                }

                float invSpp = 1.f / kSamplesPerPixel;

                if (gSppId == 0)
                {
                    outputNRDDiffuseRadianceHitDist[pixel] = float4(invSpp * diffuseRadiance, invSpp * hitDist);
                    outputNRDSpecularRadianceHitDist[pixel] = float4(invSpp * specularRadiance, invSpp * hitDist);
                    outputNRDResidualRadianceHitDist[pixel] = float4(invSpp * residualRadiance, invSpp * hitDist);
                }
                else
                {
                    outputNRDDiffuseRadianceHitDist[pixel] += float4(invSpp * diffuseRadiance, invSpp * hitDist);
                    outputNRDSpecularRadianceHitDist[pixel] += float4(invSpp * specularRadiance, invSpp * hitDist);
                    outputNRDResidualRadianceHitDist[pixel] += float4(invSpp * residualRadiance, invSpp * hitDist);
                }
            }
        }

    }

    /** Entry point for merge pass.
        \param[in] pixel Pixel coordinates.
    */
    void execute(const uint2 _pixel)
    {
        const uint2 pixel = _pixel;

        if (any(pixel >= params.frameDim)) return;

        printSetPixel(pixel);
        logSetPixel(pixel);

        ReSTIR(pixel);
    }
};

cbuffer CB
{
    PathReusePass gPathReusePass;
}


[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gPathReusePass.execute(dispatchThreadId.xy);
}
