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

// if we don't use PathSamplingMode::PathReuse, this pass is used for computing contribution MIS weights

/** TODO.
*/
struct PathReusePass
{
    static const uint kNeighborOffsetCount = NEIGHBOR_OFFSET_COUNT;
    static const uint kNeighborOffsetMask = kNeighborOffsetCount - 1;

    // Resources
    RestirPathTracerParams params;                        ///< Runtime parameters.

    Texture2D<PackedHitInfo> vbuffer;                     ///< Fullscreen V-buffer for the primary hits.

    RWTexture2D<float4> outputColor;                      ///< Output resolved color.
    StructuredBuffer<PathReservoir> outputReservoirs;                  ///< New per-pixel paths.
    RWStructuredBuffer<PathReuseMISWeight> misWeightBuffer;

    StructuredBuffer<PixelReconnectionData> reconnectionDataBuffer;
    ByteAddressBuffer nRooksPattern;

    int gNumSpatialRounds;
    uint gSpatialReusePattern;
    bool gIsLastRound;

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

            TriangleHit th = PrimaryHit.getTriangleHit();
            sd = loadShadingData(PrimaryHit, -ray.dir, true);
        }
        return sd;
    }

    int getNeighborCount()
    {
        return 16;
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


    /*
    * Build a reservoir of paths whose suffix can be merged to a pixel's prefix.
    * This method is optimized for small windows such that we can run the entire
    * algorithm in one pass.
    */

    void BPRComputeMISWeights(const int2 pixel)
    {
#if BPR // path reuse
        int blockShiftOffset;
        if (PathReusePattern(gSpatialReusePattern) == PathReusePattern::NRooks) blockShiftOffset = 16;
        else blockShiftOffset = params.seed % 16;
        int2 block = (pixel - blockShiftOffset + 16) / 16;
        // TODO: How to seed efficiently?
        var sg = TinyUniformSampleGenerator(block, (kCandidateSamples + 1 + gNumSpatialRounds) * params.seed + kCandidateSamples);

        // randomly select one N-rooks pattern from 256 patterns
        const uint patternIndex = min(255, sampleNext1D(sg) * 256);
        const int2 pixelInBlock = (pixel - blockShiftOffset + 16) % 16;
        int pixelInBlockId = pixelInBlock.y * 16 + pixelInBlock.x;
        int byteAddress = pixelInBlockId / 2;
        uint word = nRooksPattern.Load(patternIndex * 256 + 128 + 4 * (byteAddress / 4));
        int groupId = (word >> (8 * (byteAddress % 4)) >> (4 * (pixelInBlockId % 2))) & 0xF;
        int4 NRookQuery = int4(pixelInBlock, groupId, patternIndex);
        if (PathReusePattern(gSpatialReusePattern) == PathReusePattern::Block) pixelInBlock.x = 4 * (pixel.y % 4) + (pixel.x % 4);

        // Compute offset into per-sample buffers. All samples are stored consecutively at this offset.
        const uint centralOffset = params.getReservoirOffset(pixel);
        PathReservoir centralReservoir = outputReservoirs[centralOffset];
        PackedHitInfo centralPrimaryHitPacked;
        ShadingData centralPrimarySd = getPixelShadingData(pixel, centralPrimaryHitPacked);

        if (!isValidPackedHitInfo(centralPrimaryHitPacked)) return;

        ReconnectionData dummyRcData;
        dummyRcData.Init();

        int neighborCount = getNeighborCount();

        uint shiftmask = 0;

        int validNeighborCount = 0;
        //
        float p_sum = 0;
        float p_self = 0;
        float p_sum_NEE = 0;
        float p_self_NEE = 0;
        float dstJacobian = 1.f;
        float dstMISProb = 1.f;
        uint dstShiftType = 0;
        int validCount = 0;

        for (int j = 0; j < neighborCount; ++j)
        {
            if (j == pixelInBlock.x)
            {
                p_self = PathReservoir::toScalar(centralReservoir.F) > 0.f ? 1.f : 0.f;
                p_sum += p_self;
                p_self_NEE = PathReservoir::toScalar(centralReservoir.rcVertexIrradiance[1]) > 0.f ? 1.f : 0.f;
                p_sum_NEE += p_self_NEE;
            }
            else
            {
                int2 tneighborPixel = getPathReuseNextNeighborPixel(NRookQuery, pixel, j);

                if (!isValidScreenRegion(tneighborPixel)) continue;
                PackedHitInfo tneighborPrimaryHitPacked;
                ShadingData tneighborPrimarySd = getPixelShadingData(tneighborPixel, tneighborPrimaryHitPacked);
                if (!isValidPackedHitInfo(tneighborPrimaryHitPacked)) continue;
                validCount++;

                float p = 0.f;
                float p_NEE;

                {
                    float tneighborJacobian = 0.f;
                    float output2 = 0.f;
                    float3 tneighborIntegrand = computeShiftedIntegrandExtraOutput(params, tneighborJacobian, output2, tneighborPrimaryHitPacked, tneighborPrimarySd,
                        centralPrimarySd, centralReservoir, dummyRcData, true);
                    p = tneighborJacobian;
                    p_NEE = output2;
                }

                if (j == pixelInBlock.x)
                {
                    p_self = p;
                }

                p_sum += p;

                if (j == pixelInBlock.x)
                {
                    p_self_NEE = p_NEE;
                }
                p_sum_NEE += p_NEE;
            }
        }

        float misWeight = p_sum == 0.f ? 0.f : p_self / p_sum;

        misWeightBuffer[centralOffset].rcBSDFMISWeight = misWeight;
        misWeightBuffer[centralOffset].rcNEEMISWeight = p_sum_NEE == 0.f ? 0.f : p_self_NEE / p_sum_NEE;
#endif
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

        BPRComputeMISWeights(pixel);
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
