/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # Redistribution and use in source and binary forms, with or without
 # modification, are permitted provided that the following conditions
 # are met:
 #  * Redistributions of source code must retain the above copyright
 #    notice, this list of conditions and the following disclaimer.
 #  * Redistributions in binary form must reproduce the above copyright
 #    notice, this list of conditions and the following disclaimer in the
 #    documentation and/or other materials provided with the distribution.
 #  * Neither the name of NVIDIA CORPORATION nor the names of its
 #    contributors may be used to endorse or promote products derived
 #    from this software without specific prior written permission.
 #
 # THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS "AS IS" AND ANY
 # EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 # IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 # PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 # CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 # EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 # PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 # PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 # OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 # (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 # OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 **************************************************************************/
import Scene.Scene;
import Utils.Attributes;
import Utils.Color.ColorHelpers;
import Rendering.Lights.EnvMapSampler;
import Rendering.Materials.StandardMaterial;
import Rendering.Materials.ClothMaterial;
import Rendering.Materials.HairMaterial;
import LoadShadingData;
import PathTracer;
import PathState;
import Params;
import PathReservoir;

// Shared memory variables.
// TODO: Can we declare these inside PathGenerator?
static const uint kWarpCount = (kScreenTileDim.x * kScreenTileDim.y) / 32;


/** Helper struct for generating paths in screen space.

    The primary hits are classified based on material properties and appended
    to different queues. Hits on the background are directly evaluated and written
    to the output buffer, as there is no point in deferring them to a separate queue.

    The dispatch size is one thread group per screen tile. A warp is assumed to be 32 threads.
    Within a thread group, the threads are linearly indexed and mapped to pixels in Morton order.

    Output queues
    -------------

    The output queues have all paths packed tightly, organized by tile.
    The tile order is non-deterministic, but the order within tiles is well-defined.
    The pixels are enumerated in Morton order with all samples for a pixel stored consecutively.

    For each pixel on screen, we determine which queue it should be processed on and append
    one path entry for each sample in that pixel. Note that the number of samples per pixel may vary.

    Output sample buffer
    --------------------

    For each pixel that belongs to the background, and hence does not need to be path traced,
    we directly evaluate the background color and write all samples to the output sample buffer.

    The output sample buffer is organized by tiles in scanline order. Within tiles,
    the pixels are enumerated in Morton order with all samples for a pixel stored consecutively.

    When the number of samples/pixel is not fixed, we additionally write a 2D lookup table,
    for each pixel storing the tile-local offset to where the first sample is stored.
    Based on this information, subsequent passes can easily find the location of a given sample.

*/
struct PathGenerator
{
    // Resources
    RestirPathTracerParams params;                  ///< Runtime parameters.

    Texture2D<PackedHitInfo> vbuffer;               ///< Fullscreen V-buffer for the primary hits.

    RWTexture2D<float4> outputColor;                ///< Output color buffer if kSamplesPerPixel == 1.


    int gSampleId;

    // Render settings that depend on the scene.
    // TODO: Move into scene defines.
    //static bool kUseEnvBackground = USE_ENV_BACKGROUND;
    bool kUseEnvBackground;

    /** Entry point for path generator.
        \param[in] tileID Tile ID in x and y on screen.
        \param[in] threadIdx Thread index within the tile.
    */
    void execute(const uint2 tileID, const uint threadIdx)
    {
        // Map thread to pixel based on Morton order within tile.
        // The tiles themselves are enumerated in scanline order on screen.
        const uint2 tileOffset = tileID << kScreenTileBits; // Tile offset in pixels.
        const uint2 pixel = deinterleave_8bit(threadIdx) + tileOffset; // Assumes 16x16 tile or smaller. A host-side assert checks this assumption.

        // Process the pixel to determine which queue to use.
        // If we don't hit any surfaces or volumes, then the background will be evaluated and written out directly.
        Ray cameraRay;
        bool useGeneralQueue = false;

        // Note: Do not terminate threads for out-of-bounds pixels because we need all threads active for the prefix sum pass below.
        if (all(pixel < params.frameDim))
        {
            // Determine number of samples at the current pixel.
            // This is either a fixed number or loaded from the sample count texture.
            // TODO: We may want to use a nearest sampler to allow the texture to be of arbitrary dimension.

            // Compute the primary ray.
            // TODO: Support depth-of-field using computeRayThinlens, if aperture size > 0.
            cameraRay = gScene.camera.computeRayPinhole(pixel, params.frameDim);

            // Load the primary hit from the V-buffer.
            const HitInfo hit = HitInfo(vbuffer[pixel]);
            bool hitSurface = hit.isValid();

            if (hitSurface)
            {
                useGeneralQueue = true;
            }
        }

        if (all(pixel < params.frameDim))
        {
            // Append paths to queues.
            uint pathID = (pixel.y << 12) | pixel.x; // TODO: Helper struct for encoding/decoding path ID.

            if (useGeneralQueue)
            {
            }
            else if (gSampleId == 0)
            {
                // Write background pixels.
                writeBackground(pixel, cameraRay.dir);
            }

            // Only one path per pixel needed
            pathID = (pixel.y << 12) | pixel.x;
        }
    }

    void writeBackground(const uint2 pixel, const float3 dir)
    {
        // Evaluate background color for the current pixel.
        float3 color = float3(0.f);
        if (kUseEnvBackground)
        {
            color = gScene.envMap.eval(dir);
        }

        // Write color and denoising guide data for all samples in pixel.
        // For the special case of fixed 1 spp we write the color directly to the output texture.
        {
            outputColor[pixel] = float4(color, 1.f);
        }
    }
};

cbuffer CB
{
    PathGenerator gPathGenerator;
}

// TODO: Replace by compile-time uint2 constant when it works in Slang.
// See related issue https://gitlab-master.nvidia.com/nvresearch-gfx/Tools/slang/-/issues/87
[numthreads(256 /* kScreenTileDim.x * kScreenTileDim.y */, 1, 1)]
void main(
    uint3 groupID : SV_GroupID,
    uint3 groupThreadID : SV_GroupThreadID)
{
    gPathGenerator.execute(groupID.xy, groupThreadID.x);
}
