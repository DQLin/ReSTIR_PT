/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
#include "Utils/Math/MathConstants.slangh"
import Scene.Scene;
import Utils.Attributes;
import Utils.Sampling.TinyUniformSampleGenerator;
import Utils.Debug.PixelDebug;
import SurfaceData;
import EvalContext;
import Lights;
import Resampling;
import Params;

/** Performs initial resampling from light samples in the light tiles.
*/
struct InitialResampling
{
    // Static configuration.
    static const uint kLightTileCount = LIGHT_TILE_COUNT;
    static const uint kLightTileSize = LIGHT_TILE_SIZE;

    static const uint kScreenTileSize = SCREEN_TILE_SIZE;
    static const uint kInitialLightSampleCount = INITIAL_LIGHT_SAMPLE_COUNT;
    static const uint kInitialBRDFSampleCount = INITIAL_BRDF_SAMPLE_COUNT;
    static const float kBRDFSampleRatio = float(kInitialBRDFSampleCount) / float(kInitialLightSampleCount + kInitialBRDFSampleCount);
    static const bool kSpecularOnly = false;

    static const bool kCheckVisibility = CHECK_VISIBILITY;

    static const DebugOutput kDebugOutput = DebugOutput(DEBUG_OUTPUT);

    // Resources.
    [root] StructuredBuffer<PackedSurfaceData> surfaceData;
    Texture2D<uint> normalDepth;
    StructuredBuffer<PackedLoadedLightSample> lightTileData;
    RWStructuredBuffer<PackedReservoir> reservoirs;
    RWTexture2D<float4> debugOutput;

    Lights lights;

    uint2 frameDim;
    uint frameIndex;
    float brdfCutoff;
    bool useSourcePDFForRMIS;

    uint getPixelIndex(uint2 pixel) { return pixel.y * frameDim.x + pixel.x; }

    /** Heuristic to determine a max visibility ray length from a PDF wrt. solid angle.
        \param[in] pdf PDF wrt. solid angle.
    */
    float maxDistanceFromPdf(float pdf)
    {
        return brdfCutoff > 0.f ? sqrt((1.f / brdfCutoff - 1.f) * pdf) : Lights::kRayTMax;
    }

    /** Compute the PDF of an initial sample.
        If initial BRDF samples are used, we need to properly mix the PDFs of the two distributions.
    */
    float evalInitialSamplePdf(const EvalContext evalContext, const LightSample lightSample, const EvaluatedLightSample evaluatedLightSample)
    {
        if (kInitialBRDFSampleCount > 0)
        {
            float brdfPdf = evalContext.evalPdfBRDF(kSpecularOnly, evaluatedLightSample.dir);
            float maxDistance = maxDistanceFromPdf(brdfPdf);
            if (lightSample.getKind() == LightKind::Emissive && evaluatedLightSample.distance > maxDistance) brdfPdf = 0.f;
            if (lightSample.getKind() == LightKind::Analytic) brdfPdf = 0.f;
            return lerp(evaluatedLightSample.pdf, brdfPdf * evaluatedLightSample.geomFactor, kBRDFSampleRatio);
        }
        else
        {
            return evaluatedLightSample.pdf;
        }
    }

    void process(uint2 pixel)
    {
        if (any(pixel >= frameDim)) return;

        printSetPixel(pixel);

        if (kDebugOutput == DebugOutput::InitialWeight) debugOutput[pixel] = float4(0.f);

        uint pixelIndex = getPixelIndex(pixel);

        // Setup evaluation context.
        SurfaceData sd = SurfaceData::createFromPacked(surfaceData[pixelIndex]);
        EvalContext evalContext = EvalContext::create(pixel, frameDim, sd);

        // Early out if pixel contains no valid data (i.e. background pixel).
        if (!evalContext.isValid())
        {
            reservoirs[pixelIndex] = Reservoir::createEmpty().pack();
            return;
        }

        // Debug pixel data.
        switch (kDebugOutput)
        {
        case DebugOutput::Position: debugOutput[pixel] = float4(sd.pos, 0.f); break;
        case DebugOutput::Depth: debugOutput[pixel] = float4(float3(sd.depth), 0.f); break;
        case DebugOutput::Normal: debugOutput[pixel] = float4(sd.normal * 0.5f + 0.5f, 0.f); break;
        case DebugOutput::FaceNormal: debugOutput[pixel] = float4(sd.faceNormal * 0.5f + 0.5f, 0.f); break;
        case DebugOutput::DiffuseWeight: debugOutput[pixel] = float4(float3(sd.diffuseWeight), 0.f); break;
        case DebugOutput::SpecularWeight: debugOutput[pixel] = float4(float3(sd.specularWeight), 0.f); break;
        case DebugOutput::SpecularRoughness: debugOutput[pixel] = float4(float3(sd.specularRoughness), 0.f); break;
        case DebugOutput::PackedNormal: debugOutput[pixel] = float4(unpackNormalDepth(normalDepth[pixel]).xyz * 0.5f + 0.5f, 0.f); break;
        case DebugOutput::PackedDepth: debugOutput[pixel] = float4(float3(unpackNormalDepth(normalDepth[pixel]).w), 0.f); break;
        }

        // Determine screen tile.
        // Each screen tile uses light samples from a random light tile for improved coherency.
        uint2 screenTile = pixel / kScreenTileSize;

        // Determine light tile to use for this screen tile.
        TinyUniformSampleGenerator tileSg = TinyUniformSampleGenerator(screenTile, frameIndex);
        uint tileIndex = min(uint(sampleNext1D(tileSg) * kLightTileCount), kLightTileCount - 1);
        uint lightTileOffset = tileIndex * kLightTileSize;

        // Determine offset and stride to stratify samples within the light tile.
        TinyUniformSampleGenerator sg = TinyUniformSampleGenerator(pixel, frameIndex);

        print("Initial Resampling", 1);
        print("-",pixel);
        print("-", frameIndex);
        print("-", sampleNext1D(sg));

        uint stride = (kLightTileSize + kInitialLightSampleCount - 1) / kInitialLightSampleCount;
        uint offset = min(uint(sampleNext1D(sg) * stride), stride - 1);

        // Setup resampling.
        RisState risState = RisState::createEmpty();

        // Resample light samples from the light tile.
        for (uint i = 0; i < kInitialLightSampleCount; ++i)
        {
            // Pick light sample and evaluate it.
            uint index = lightTileOffset + (offset + i * stride) % kLightTileSize;
            const LoadedLightSample loadedLightSample = LoadedLightSample::createFromPacked(lightTileData[index]);
            const EvaluatedLightSample evaluatedLightSample = loadedLightSample.eval(evalContext.pos);

            // Resample light sample.
            float targetPdf = evalContext.evalTargetFunction(evaluatedLightSample);
            float sourcePdf = evalInitialSamplePdf(evalContext, loadedLightSample.lightSample, evaluatedLightSample);
            if (useSourcePDFForRMIS) targetPdf = sourcePdf;
            streamingResampleStep(risState, loadedLightSample.lightSample, targetPdf, sourcePdf, sg);
        }

        // Resample BRDF samples.
        if (kInitialBRDFSampleCount > 0)
        {
            for (uint i = 0; i < kInitialBRDFSampleCount; ++i)
            {
                LightSample lightSample = LightSample::createInvalid();
                float3 dir;
                if (evalContext.sampleBRDF(kSpecularOnly, sampleNext3D(sg), dir))
                {
                    float brdfPdf = evalContext.evalPdfBRDF(kSpecularOnly, dir);
                    float maxDistance = maxDistanceFromPdf(brdfPdf);
                    RayDesc ray = { evalContext.pos, 0.f, dir, maxDistance + 0.01f };
                    HitInfo hit;
                    if (Lights::kUseEmissiveLights && evalContext.traceRay(ray, hit))
                    {
                        // Check if we hit a triangle mesh (other geometry types are not supported).
                        if (hit.getType() == HitType::Triangle)
                        {
                            const TriangleHit triangleHit = hit.getTriangleHit();

                            uint triangleIndex = gScene.lightCollection.getTriangleIndex(triangleHit.instanceID, triangleHit.primitiveIndex);
                            if (triangleIndex != LightCollection::kInvalidIndex)// && any(gScene.lightCollection.getAverageRadiance(triangleIndex) > 0.f))
                            {
                                // Create a light sample from the emissive triangle hit.
                                lightSample = LightSample::createEmissive(triangleIndex, triangleHit.barycentrics);
                                // If MIS cutoff is used, we need to evaluate the sample and make sure it actually could have been
                                // generated by the area sampling technique. This is due to numerical precision.
                                if (brdfCutoff > 0.f)
                                {
                                    const EvaluatedLightSample evaluatedLightSample = lights.evalLightSample(lightSample, evalContext.pos);
                                    float brdfPdf = evalContext.evalPdfBRDF(kSpecularOnly, evaluatedLightSample.dir);
                                    float maxDistance = maxDistanceFromPdf(brdfPdf);
                                    if (evaluatedLightSample.distance > maxDistance) lightSample = LightSample::createInvalid();
                                }
                            }
                        }
                    }
                    else if (Lights::kUseEnvLight && !lightSample.isValid())
                    {
                        lightSample = LightSample::createEnv(dir);
                    }
                }

                if (!lightSample.isValid())
                {
                    risState.M += 1.f;
                    continue;
                }

                // Resample BRDF light sample.
                const EvaluatedLightSample evaluatedLightSample = lights.evalLightSample(lightSample, evalContext.pos);
                float targetPdf = evalContext.evalTargetFunction(evaluatedLightSample);
                float sourcePdf = evalInitialSamplePdf(evalContext, lightSample, evaluatedLightSample);
                streamingResampleStep(risState, lightSample, targetPdf, sourcePdf, sg);
            }
        }

        // Check visibility.
        if (kCheckVisibility && risState.lightSample.isValid())
        {
            const EvaluatedLightSample evaluatedLightSample = lights.evalLightSample(risState.lightSample, evalContext.pos);
            if (evalContext.evalVisibility(evaluatedLightSample) == 0.f) risState = RisState::createEmpty();
        }

        // Instead of storing the weight sum and weight individually,
        // we precompute the term that would be multiplied with the final sample evaluation.
        risState.weight = risState.weight > 0.f ? (risState.weightSum / risState.M) / risState.weight : 0.f;
        risState.M = 1.f;

        // Save reservoir.
        reservoirs[pixelIndex] = risState.toReservoir().pack();

        // Write debug output.
        if (kDebugOutput == DebugOutput::InitialWeight) debugOutput[pixel] = float4(float3(risState.weight), 0.f);
    }
};

cbuffer CB
{
    InitialResampling gInitialResampling;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gInitialResampling.process(dispatchThreadId.xy);
}
