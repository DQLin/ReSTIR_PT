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
import LoadShadingData;
import Rendering.Materials.MaterialShading;

/** TODO.
*/
struct PathReusePass
{
    static const uint kNeighborOffsetCount = NEIGHBOR_OFFSET_COUNT;
    static const uint kNeighborOffsetMask = kNeighborOffsetCount - 1;

    // Resources
    RestirPathTracerParams params;                        ///< Runtime parameters.

    Texture2D<PackedHitInfo> vbuffer;                     ///< Fullscreen V-buffer for the primary hits.
    Texture2D<PackedHitInfo> temporalVbuffer;
    Texture2D<float2> motionVectors;

    RWTexture2D<float4> outputColor;                      ///< Output resolved color.
    RWStructuredBuffer<PathReservoir> outputReservoirs;    // reservoir for next pass
    RWStructuredBuffer<PathReservoir> temporalReservoirs;  // reservoir from previous frame
    StructuredBuffer<PixelReconnectionData> reconnectionDataBuffer;

    Texture2D<float4> directLighting;                  
    bool useDirectLighting;

    int  gNumSpatialRounds;

    bool gIsLastRound;
    bool gEnableTemporalReprojection;

    float gTemporalHistoryLength;
    bool gNoResamplingForTemporalReuse;

    bool isValidPackedHitInfo(PackedHitInfo packed)
    {
        return packed.x != 0;
    }

    bool isValidScreenRegion(int2 pixel) { return all(pixel >= 0 && pixel < params.frameDim); }

    ShadingData getPixelShadingData(int2 pixel, out PackedHitInfo PrimaryHitPacked)
    {
        ShadingData sd = {};
        Ray ray = gScene.camera.computeRayPinhole(pixel, params.frameDim);
        PrimaryHitPacked = vbuffer[pixel];
        if (isValidPackedHitInfo(PrimaryHitPacked))
        {
            HitInfo PrimaryHit; PrimaryHit.unpack(PrimaryHitPacked);
            sd = loadShadingData(PrimaryHit, -ray.dir, true);
        }
        return sd;
    }

    ShadingData getPixelTemporalShadingData(int2 pixel, out PackedHitInfo PrimaryHitPacked)
    {
        ShadingData sd = {};
        Ray ray = gScene.camera.computeRayPinholePrevFrame(pixel, params.frameDim);
        PrimaryHitPacked = temporalVbuffer[pixel];
        if (isValidPackedHitInfo(PrimaryHitPacked))
        {
            HitInfo PrimaryHit; PrimaryHit.unpack(PrimaryHitPacked);
            sd = loadShadingData(PrimaryHit, -ray.dir, true);
        }
        return sd;
    }

    /*
    * Build a reservoir of paths whose suffix can be merged to a pixel's prefix.
    * This method is optimized for small windows such that we can run the entire
    * algorithm in one pass.
    */
    void ReSTIR(const uint2 pixel)
    {
        var sg = TinyUniformSampleGenerator(pixel, (kCandidateSamples + 1 + gNumSpatialRounds) * params.seed + kCandidateSamples);

        // Compute offset into per-sample buffers. All samples are stored consecutively at this offset.
        const uint centralOffset = params.getReservoirOffset(pixel);
        PathReservoir dstReservoir = outputReservoirs[centralOffset];
        PathReservoir centralReservoir = dstReservoir;
        float currentM = dstReservoir.M;
        float3 initialColor = dstReservoir.F * dstReservoir.weight;

        if (ReSTIRMISKind(kTemporalReSTIRMISKind) == ReSTIRMISKind::Talbot)
        {
            dstReservoir.init();
        }
        else
        {
            dstReservoir.prepareMerging();
        }

        PackedHitInfo centralPrimaryHitPacked;
        ShadingData centralPrimarySd = getPixelShadingData(pixel, centralPrimaryHitPacked);
        if (!isValidPackedHitInfo(centralPrimaryHitPacked)) return;

        float3 color = 0.f;
        ReconnectionData dummyRcData;
        dummyRcData.Init();

        {
            bool chooseCurrent = true;

            // color averaging test

            // fetch temporal reservoir
            float2 motionVector = motionVectors[pixel];
            int2 prevPixel = pixel;

            bool foundTemporalSurface = true;

            if (gEnableTemporalReprojection)
            {
                prevPixel = pixel + motionVector * params.frameDim + (sampleNext2D(sg) * 1.f - 0.f);
            }

            if (!isValidScreenRegion(prevPixel) || !foundTemporalSurface) return;

            PackedHitInfo temporalPrimaryHitPacked;
            ShadingData temporalPrimarySd = getPixelTemporalShadingData(prevPixel, temporalPrimaryHitPacked);
            if (!isValidPackedHitInfo(temporalPrimaryHitPacked)) return;

            bool doTemporalUpdateForDynamicScene = kTemporalUpdateForDynamicScene;

            PathReservoir temporalReservoir = temporalReservoirs[params.getReservoirOffset(prevPixel)];

            temporalReservoir.M = min(gTemporalHistoryLength * currentM, temporalReservoir.M);

            float dstJacobian;

            ///////////////////
            /// TALBOT RMIS ///
            ///////////////////
            if (ReSTIRMISKind(kTemporalReSTIRMISKind) == ReSTIRMISKind::Talbot)
            {
                static const int curSampleId = -1;
                static const int prevSampleId = 0;

                for (int i = curSampleId; i <= prevSampleId; i++)
                {
                    float p_sum = 0;
                    float p_self = 0;

                    PathReservoir tempDstReservoir = dstReservoir;

                    bool possibleToBeSelected = false;

                    if (i == curSampleId)
                    {
                        tempDstReservoir = outputReservoirs[centralOffset];
                        dstJacobian = 1.f;
                        possibleToBeSelected = tempDstReservoir.weight > 0;
                    }
                    else
                    {
                        ReconnectionData rcData;
                        if (ShiftMapping(kShiftStrategy) == ShiftMapping::Hybrid && temporalReservoir.pathFlags.rcVertexLength() > 1)
                            rcData = reconnectionDataBuffer[centralOffset].data[1];
                        else
                            rcData = dummyRcData;

                        possibleToBeSelected = shiftAndMergeReservoir(params, doTemporalUpdateForDynamicScene, dstJacobian, centralPrimaryHitPacked, centralPrimarySd, tempDstReservoir,
                            temporalPrimarySd, temporalReservoir, rcData, true, sg, false, 1.f, true); // "true" means hypothetically selected as the sample
                    }

                    if (possibleToBeSelected)
                    {
                        for (int j = curSampleId; j <= prevSampleId; ++j)
                        {
                            if (j == curSampleId)
                            {
                                float cur_p = PathReservoir::toScalar(tempDstReservoir.F) * currentM;
                                p_sum += cur_p;
                                if (i == curSampleId) p_self = cur_p;
                            }
                            else
                            {
                                if (i == j)
                                {
                                    p_self = PathReservoir::toScalar(temporalReservoir.F) / dstJacobian * temporalReservoir.M;
                                    p_sum += p_self;
                                    continue;
                                }

                                float p_ = 0.f;
                                float tneighborJacobian;

                                ReconnectionData rcData;
                                if (ShiftMapping(kShiftStrategy) == ShiftMapping::Hybrid && tempDstReservoir.pathFlags.rcVertexLength() > 1)
                                    rcData = reconnectionDataBuffer[centralOffset].data[0];
                                else
                                    rcData = dummyRcData;

                                float3 tneighborIntegrand = computeShiftedIntegrand(params, tneighborJacobian, temporalPrimaryHitPacked, temporalPrimarySd,
                                    centralPrimarySd, tempDstReservoir, rcData, true, true); //usePrev
                                p_ = PathReservoir::toScalar(tneighborIntegrand) * tneighborJacobian;
                                p_sum += p_ * temporalReservoir.M;
                            }
                        }
                    }

                    float misWeight = p_sum == 0.f ? 0.f : p_self / p_sum;
                    PathReservoir neighborReservoir;
                    if (i == curSampleId) neighborReservoir = tempDstReservoir;
                    else neighborReservoir = temporalReservoir;

                    mergeReservoirWithResamplingMIS(params, tempDstReservoir.F, dstJacobian, dstReservoir, tempDstReservoir, neighborReservoir, sg, false, misWeight);
                }

                if (dstReservoir.weight > 0)
                {
                    dstReservoir.finalizeGRIS();
                }
            }
            /////////////////////
            /// CONSTANT RMIS ///
            /////////////////////
            else
            {
                float chosen_Jacobian = 1.f;

                bool selected;

                if (gNoResamplingForTemporalReuse)
                    selected = mergeReservoirNoResampling(params, dstReservoir, temporalReservoir, sg);
                else
                    selected = shiftAndMergeReservoir(params, doTemporalUpdateForDynamicScene, dstJacobian, centralPrimaryHitPacked, centralPrimarySd, dstReservoir,
                        temporalPrimarySd, temporalReservoir, dummyRcData, true, sg, false);

                if (selected)
                {
                    chooseCurrent = false;
                    chosen_Jacobian = dstJacobian;
                }

                // MIS weight
                if (dstReservoir.weight > 0 && ReSTIRMISKind(kTemporalReSTIRMISKind) != ReSTIRMISKind::ConstantBiased && !gNoResamplingForTemporalReuse)
                {
                    if (chooseCurrent) // need to know prev pdf 
                    {
                        float count = currentM;
                        float prefixJacobian = 1.f;

                        // compute two shifts

                        int shiftCount = 1;
                        float prefix_approxPdf;

                        float3 prefixIntegrand = computeShiftedIntegrand(params, prefixJacobian, temporalPrimaryHitPacked, temporalPrimarySd,
                            centralPrimarySd, dstReservoir, dummyRcData, true, true); //usePrev // determinisitically select the reconnection shift
                        prefix_approxPdf = PathReservoir::toScalar(prefixIntegrand) * prefixJacobian;

                        if (prefix_approxPdf > 0.f)
                        {
                            count += temporalReservoir.M;
                        }

                        float misWeight = 1.f / max(1.f, count);
                        if (ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::Constant)
                            misWeight = PathReservoir::toScalar(dstReservoir.F) / (PathReservoir::toScalar(dstReservoir.F) * currentM + prefix_approxPdf * temporalReservoir.M);
                        dstReservoir.weight *= dstReservoir.M * misWeight;
                    }
                    else // have already computed everything
                    {
                        float count = currentM;

                        // compute the other shift
                        if (ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::Constant)
                        {
                            float sum_pdf = PathReservoir::toScalar(temporalReservoir.F) / chosen_Jacobian;

                            float misWeight = (PathReservoir::toScalar(temporalReservoir.F) / chosen_Jacobian) / (sum_pdf * temporalReservoir.M + PathReservoir::toScalar(dstReservoir.F) * currentM);

                            if (ReSTIRMISKind(kSpatialReSTIRMISKind) == ReSTIRMISKind::ConstantBinary)
                            {
                                misWeight = 1.f / max(1.f, count);
                            }
                            dstReservoir.weight *= dstReservoir.M * misWeight;
                        }
                    }
                }

                dstReservoir.finalizeRIS();
            }

            if (dstReservoir.weight < 0.f || isinf(dstReservoir.weight) || isnan(dstReservoir.weight)) dstReservoir.weight = 0.f;
            outputReservoirs[centralOffset] = dstReservoir;
            color = dstReservoir.F * dstReservoir.weight;

            if (gIsLastRound)
            {
                if (useDirectLighting)
                {
                    color += directLighting[pixel].rgb;
                    if (any(isnan(color) || isinf(color))) color = 0.f;
                }

                outputColor[pixel] += float4(color / kSamplesPerPixel, 1.f);
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
