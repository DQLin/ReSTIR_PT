/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
import Scene.Scene;
import Lights;
import Utils.Sampling.TinyUniformSampleGenerator;

/** Generates random light samples and stores them into light tiles.
    These light samples are later resampled in the initial resampling pass.
    Using light samples from small light tiles highly improves performance
    due to reduced cache trashing.
*/
struct GenerateLightTiles
{
    // Static configuration.
    static const uint kLightTileCount = LIGHT_TILE_COUNT;
    static const uint kLightTileSize = LIGHT_TILE_SIZE;

    static const uint kEnvLightSampleCount = ENV_LIGHT_SAMPLE_COUNT;
    static const uint kEmissiveLightSampleCount = EMISSIVE_LIGHT_SAMPLE_COUNT;
    static const uint kAnalyticLightSampleCount = ANALYTIC_LIGHT_SAMPLE_COUNT;

    // Resources.
    RWStructuredBuffer<PackedLoadedLightSample> lightTileData;

    Lights lights;

    uint frameIndex;

    float getStratifiedRandom(uint sampleIndex, uint sampleCount, float offset)
    {
        return frac(offset + float(sampleIndex) / float(sampleCount));
    }

    void process(uint tileIndex, uint sampleIndex)
    {
        if (tileIndex >= kLightTileCount || sampleIndex >= kLightTileSize) return;

        // Compute index into light tile buffer.
        uint bufferIndex = tileIndex * kLightTileSize + sampleIndex;

        // Setup per-tile random generator and pick random offset.
        TinyUniformSampleGenerator tileSg = TinyUniformSampleGenerator(tileIndex, frameIndex);
        float offset = sampleNext1D(tileSg);

        // Setup per-thread random generator.
        TinyUniformSampleGenerator sg = TinyUniformSampleGenerator(bufferIndex, frameIndex + 1);

        LightSample lightSample = LightSample::createInvalid();

        if (sampleIndex < kEnvLightSampleCount)
        {
            // Environment map.
            float random = getStratifiedRandom(sampleIndex, kEnvLightSampleCount, offset);
            lightSample = lights.sampleEnv(random, sg);
        }
        else if (sampleIndex < kEnvLightSampleCount + kEmissiveLightSampleCount)
        {
            // Emissive light.
            float random = getStratifiedRandom(sampleIndex, kEmissiveLightSampleCount, offset);
            lightSample = lights.sampleEmissive(random, sg);
        }
        else /* if (sampleIndex < kEnvLightSampleCount + kEmissiveLightSampleCount + kAnalyticLightSampleCount) */
        {
            // Analytic light.
            float random = getStratifiedRandom(sampleIndex, kAnalyticLightSampleCount, offset);
            lightSample = lights.sampleAnalytic(random, sg);
        }

        // Write light sample.
        lightTileData[bufferIndex] = lights.loadLightSample(lightSample).pack();
    }
};

cbuffer CB
{
    GenerateLightTiles gGenerateLightTiles;
}

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gGenerateLightTiles.process(dispatchThreadId.y, dispatchThreadId.x);
}
