/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
import Utils.Attributes;
import Utils.Sampling.TinyUniformSampleGenerator;
import Utils.Debug.PixelDebug;
import SurfaceData;
import EvalContext;
import Lights;
import Resampling;
import Params;

/** Performs spatial resampling by combining the output reservoir from the
    temporal resampling pass with one or multiple reservoirs from neighbor
    pixels.
*/
struct SpatialResampling
{
    // Static configuration.
    static const uint kNeighborOffsetCount = NEIGHBOR_OFFSET_COUNT;
    static const uint kNeighborOffsetMask = kNeighborOffsetCount - 1;

    static const bool kUsePairwiseMIS = USE_PAIRWISE_MIS;
    static bool kUnbiased = UNBIASED;

    static const DebugOutput kDebugOutput = DebugOutput(DEBUG_OUTPUT);

    // Resources.
    [root] StructuredBuffer<PackedSurfaceData> surfaceData;
    Texture2D<uint> normalDepth;
    RWStructuredBuffer<PackedReservoir> reservoirs;
    StructuredBuffer<PackedReservoir> prevReservoirs;
    Texture1D<float2> neighborOffsets;
    RWTexture2D<float4> debugOutput;

    Lights lights;

    uint2 frameDim;         ///< Current frame dimensions.
    uint frameIndex;        ///< Current frame index.

    float normalThreshold;
    float depthThreshold;

    uint neighborCount;     ///< Number of neighbor reservoirs to resample.
    float gatherRadius;     ///< Maximum radius to gather neighbors from.

    uint getPixelIndex(uint2 pixel) { return pixel.y * frameDim.x + pixel.x; }

    bool isValidPixel(int2 pixel) { return all(pixel >= 0 && pixel < frameDim); }

    int2 getNextNeighborPixel(const uint startIndex, int2 pixel, int i)
    {
        uint neighborIndex = (startIndex + i) & kNeighborOffsetMask;
        int2 neighborPixel = pixel + int2(neighborOffsets[neighborIndex] * gatherRadius);

        return neighborPixel;
    }

    void process(uint2 pixel)
    {
        if (any(pixel >= frameDim)) return;

        printSetPixel(pixel);

        if (kDebugOutput == DebugOutput::SpatialReuse) debugOutput[pixel] = float4(1.f, 0.f, 0.f, 0.f);

        const uint pixelIndex = getPixelIndex(pixel);

        TinyUniformSampleGenerator sg = TinyUniformSampleGenerator(pixel, frameIndex);

        print("Spatial Reuse", 1);
        print("-", pixel);
        print("-", frameIndex);
        print("-", sampleNext1D(sg));

        // Setup evaluation context.
        EvalContext evalContext = EvalContext::create(pixel, frameDim, SurfaceData::createFromPacked(surfaceData[pixelIndex]));
        if (!evalContext.isValid())
        {
            reservoirs[pixelIndex] = Reservoir::createEmpty().pack();
            return;
        }

        // Setup resampling.
        RisState risState = RisState::createEmpty();
        const uint startIndex = sampleNext1D(sg) * kNeighborOffsetCount;
        uint validCount = 0;

        TinyUniformSampleGenerator sg2 = TinyUniformSampleGenerator(int2(0, 0), frameIndex + 1);

        if (kUsePairwiseMIS)
        {
            const Reservoir currentReservoir = Reservoir::createFromPacked(prevReservoirs[pixelIndex]);
            const LoadedLightSample currentLightSample = lights.loadLightSample(currentReservoir.lightSample);
            float currentTargetPdf = evalContext.evalTargetFunction(currentLightSample);

            risState.canonicalWeight = 1.f;
            uint validNeighbors = 1;

            for (uint i = 0; i < neighborCount; ++i)
            {
                // Get neighbor pixel.
                int2 neighborPixel = getNextNeighborPixel(startIndex, pixel, i);
                if (!isValidPixel(neighborPixel)) continue;
                uint neighborPixelIndex = getPixelIndex(neighborPixel);

                // Check for valid neighbor.
                float4 neighborNormalDepth = unpackNormalDepth(normalDepth[neighborPixel]);
                if (!evalContext.isValidNeighbor(neighborNormalDepth, normalThreshold, depthThreshold)) continue;

                const Reservoir neighborReservoir = Reservoir::createFromPacked(prevReservoirs[neighborPixelIndex]);
                if (neighborReservoir.M == 0) continue;

                // Setup neighbor evalulation context and resample.
                const EvalContext neighborContext = EvalContext::create(neighborPixel, frameDim, SurfaceData::createFromPacked(surfaceData[neighborPixelIndex]));
                if (!neighborContext.isValid()) continue;

                ++validNeighbors;
                ++validCount;

                if (kUnbiased)
                    streamingResampleStepMisUnbiased(risState, currentReservoir, currentLightSample, currentTargetPdf, evalContext, neighborReservoir, neighborContext, neighborCount, lights, sg);
                else
                    streamingResampleStepMis(risState, currentReservoir, currentLightSample, currentTargetPdf, evalContext, neighborReservoir, neighborContext, neighborCount, lights, sg);
            }

            streamingResampleFinalizeMis(risState, currentReservoir, currentTargetPdf, sg);
            risState.M = currentReservoir.M;
            risState.weight = risState.weight > 0.f ? (risState.weightSum / validNeighbors) / risState.weight : 0.f;
        }
        else
        {
            // Resample current pixel's reservoir.
            const Reservoir currentReservoir = Reservoir::createFromPacked(prevReservoirs[pixelIndex]);

            streamingResampleStep(risState, currentReservoir, evalContext, lights, sg, true);
            float currentM = risState.M;

            // Resample neighbor pixel reservoirs.
            uint validCache = 0;
            uint selected = -1;

            // Iterate over neighbor pixels.
            for (uint i = 0; i < neighborCount; ++i)
            {
                // Get neighbor pixel.
                int2 neighborPixel = getNextNeighborPixel(startIndex, pixel, i);
                if (!isValidPixel(neighborPixel)) continue;
                uint neighborPixelIndex = getPixelIndex(neighborPixel);

                // Check for valid neighbor.
                float4 neighborNormalDepth = unpackNormalDepth(normalDepth[neighborPixel]);
                if (!evalContext.isValidNeighbor(neighborNormalDepth, normalThreshold, depthThreshold)) continue;

                validCache |= (1 << i);
                ++validCount;

                Reservoir neighborReservoir = Reservoir::createFromPacked(prevReservoirs[neighborPixelIndex]);
                selected = streamingResampleStep(risState, neighborReservoir, evalContext, lights, sg, false) ? i : selected;
            }

            const LoadedLightSample lightSample = lights.loadLightSample(risState.lightSample);
            float centerVisibility = 1.f;
            if (kUnbiased) centerVisibility = evalContext.evalVisibility(lightSample);

            // Compute the unbiased normalization term (instead of using 1/M).
            float pi = selected == -1 ? centerVisibility * risState.weight : 0.f;
            float piSum = centerVisibility * risState.weight * currentM;

            // Iterate over neighbor pixels again.
            if (centerVisibility)
            {
                TinyUniformSampleGenerator sg;

                if (selected == -1)
                {
                    for (uint i = 0; i < neighborCount; ++i)
                    {
                        // Skip invalid neighbors.
                        if (!(validCache & (1 << i)))
                        {
                            continue;
                        }

                        int2 neighborPixel = getNextNeighborPixel(startIndex, pixel, i);
                        uint neighborPixelIndex = getPixelIndex(neighborPixel);

                        // Setup evaluation context.
                        EvalContext neighborEvalContext = EvalContext::create(neighborPixel, frameDim, SurfaceData::createFromPacked(surfaceData[neighborPixelIndex]));
                        if (!neighborEvalContext.isValid()) continue;

                        const Reservoir neighborReservoir = Reservoir::createFromPacked(prevReservoirs[neighborPixelIndex]);
                        const EvaluatedLightSample evaluatedLightSample = lightSample.eval(neighborEvalContext.pos);
                        float ps = neighborEvalContext.evalTargetFunction(evaluatedLightSample);

                        // If valid, shoot a visibility ray from the neighbor
                        if (kUnbiased && ps > 0.f) ps *= neighborEvalContext.evalVisibility(evaluatedLightSample);

                        pi = selected == i ? ps : pi;
                        piSum += ps * neighborReservoir.M;
                    }
                }
            }

            float normalization = piSum > 0.f ? pi / piSum : 0.f;

            risState.weight = risState.weight > 0.f ? (risState.weightSum * normalization) / risState.weight : 0.f;
        }

        // Save reservoir.
        reservoirs[pixelIndex] = risState.toReservoir().pack();

        // Write debug output.
        if (kDebugOutput == DebugOutput::SpatialReuse) debugOutput[pixel] = float4(lerp(float3(1.f, 0.f, 0.f), float3(0.f, 1.f, 0.f), float(validCount) / neighborCount), 0.f);
    }
};

cbuffer CB
{
    SpatialResampling gSpatialResampling;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gSpatialResampling.process(dispatchThreadId.xy);
}
