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

/** Performs temporal resampling by combining the reservoir from the initial
    resampling pass with a reservoir from the last frame obtained through
    reprojection.
*/
struct TemporalResampling
{
    // Static configuration.
    static const uint kMaxHistoryLength = MAX_HISTORY_LENGTH;

    static const bool kUsePairwiseMIS = USE_PAIRWISE_MIS;
    static const bool kUnbiased = UNBIASED;

    static const DebugOutput kDebugOutput = DebugOutput(DEBUG_OUTPUT);

    [root] StructuredBuffer<PackedSurfaceData> surfaceData;
    [root] StructuredBuffer<PackedSurfaceData> prevSurfaceData;
    Texture2D<float2> motionVectors;
    RWStructuredBuffer<PackedReservoir> reservoirs;
    StructuredBuffer<PackedReservoir> prevReservoirs;
    RWTexture2D<float4> debugOutput;

    Lights lights;

    uint2 frameDim;
    uint frameIndex;

    float normalThreshold;
    float depthThreshold;

    uint getPixelIndex(uint2 pixel) { return pixel.y * frameDim.x + pixel.x; }

    bool isValidPixel(int2 pixel) { return all(pixel >= 0 && pixel < frameDim); }

    void process(uint2 pixel)
    {
        if (any(pixel >= frameDim)) return;

        printSetPixel(pixel);

        if (kDebugOutput == DebugOutput::TemporalReuse) debugOutput[pixel] = float4(1.f, 0.f, 0.f, 0.f);

        const uint pixelIndex = getPixelIndex(pixel);

        TinyUniformSampleGenerator sg = TinyUniformSampleGenerator(pixel, frameIndex);

        // Back-project this pixel to previous frame.
        const int2 prevPixel = pixel + motionVectors[pixel] * frameDim + (sampleNext2D(sg) * 1.f - 0.f);
        if (!isValidPixel(prevPixel)) return;
        const uint prevPixelIndex = getPixelIndex(prevPixel);

        // Setup evaluation context for current pixel.
        const EvalContext currentContext = EvalContext::create(pixel, frameDim, SurfaceData::createFromPacked(surfaceData[pixelIndex]));
        if (!currentContext.isValid()) return;

        // Setup evaluation context for back-projected pixel.
        const EvalContext prevContext = EvalContext::create(prevPixel, frameDim, SurfaceData::createFromPacked(prevSurfaceData[prevPixelIndex]));
        if (!prevContext.isValid()) return;

        // Write debug output.
        if (kDebugOutput == DebugOutput::TemporalReuse) debugOutput[pixel] = float4(0.f, 1.f, 0.f, 0.f);

        // Load reservoirs.
        Reservoir currentReservoir = Reservoir::createFromPacked(reservoirs[pixelIndex]);
        Reservoir prevReservoir = Reservoir::createFromPacked(prevReservoirs[prevPixelIndex]);
        prevReservoir.M = min(prevReservoir.M, currentReservoir.M * kMaxHistoryLength);

        // Setup resampling.
        RisState risState = RisState::createEmpty();

        if (kUsePairwiseMIS)
        {
            const LoadedLightSample currentLightSample = lights.loadLightSample(currentReservoir.lightSample);
            float currentTargetPdf = currentContext.evalTargetFunction(currentLightSample);
            streamingResampleStepMis(risState, currentReservoir, currentLightSample, currentTargetPdf, currentContext, prevReservoir, prevContext, 1, lights, sg);
            streamingResampleFinalizeMis(risState, currentReservoir, currentTargetPdf, sg);
            risState.weight = risState.weight > 0.f ? risState.weightSum / risState.weight : 0.f;
        }
        else
        {
            // Check if previous sample is valid for reuse.
            if (!kUnbiased && !currentContext.isValidNeighbor(prevContext, normalThreshold, depthThreshold)) return;

            // Resample current and previous reservoirs.
            streamingResampleStep(risState, currentReservoir, currentContext, lights, sg);
            bool usedPrev = streamingResampleStep(risState, prevReservoir, currentContext, lights, sg);

            // Compute the unbiased normalization term (instead of using 1/M).
            const LoadedLightSample lightSample = lights.loadLightSample(risState.lightSample);
            float currentPdf = currentContext.evalTargetFunction(lightSample, kUnbiased);
            float prevPdf = prevContext.evalTargetFunction(lightSample, kUnbiased);
            float normalization = (usedPrev ? prevPdf : currentPdf) / (currentReservoir.M * currentPdf + prevReservoir.M * prevPdf);
            risState.weight = risState.weight > 0.f ? (risState.weightSum * normalization) / risState.weight : 0.f;
        }

        // Save reservoir.
        reservoirs[pixelIndex] = risState.toReservoir().pack();
    }
};

cbuffer CB
{
    TemporalResampling gTemporalResampling;
}

[numthreads(16, 4, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gTemporalResampling.process(dispatchThreadId.xy);
}
