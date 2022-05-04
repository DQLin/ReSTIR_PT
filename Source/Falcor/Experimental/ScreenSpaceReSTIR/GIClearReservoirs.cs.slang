/***************************************************************************
 # Copyright (c) 2020, NVIDIA CORPORATION.  All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/

/** Compute shader implementing a reservoir reset pass.

    The shader resets initial sample buffer. If frameCount is 0, or forceClearReservoirs is set,
    It also clears two GI reservoir buffers.

    The dispatch dimension is over pixels (XY).
*/

import GIReservoir;

struct GIClearReservoirs
{
    uint2 frameDim;
    uint frameCount;
    uint reservoirCount;
    bool forceClearReservoirs;

    RWStructuredBuffer<PackedGIReservoir> initialSamples;
    RWStructuredBuffer<PackedGIReservoir> reservoirBuffer0;
    RWStructuredBuffer<PackedGIReservoir> reservoirBuffer1;

    uint pixelToLinearIndex(uint2 pixel)
    {
        return pixel.y * frameDim.x + pixel.x;
    }

    void execute(const uint2 pixel)
    {
        if (any(pixel >= frameDim)) return;

        uint pixelIndex = pixelToLinearIndex(pixel);

        GIReservoir reservoir = GIReservoir.createEmpty();

        for (int sampleIndex = 0; sampleIndex < reservoirCount && (frameCount == 0 || forceClearReservoirs); sampleIndex++)
        {
            writeReservoir(reservoirBuffer0, pixelIndex, sampleIndex * 2, frameDim.x * frameDim.y, reservoir);
            writeReservoir(reservoirBuffer0, pixelIndex, sampleIndex * 2 + 1, frameDim.x * frameDim.y, reservoir);
            writeReservoir(reservoirBuffer1, pixelIndex, sampleIndex * 2, frameDim.x * frameDim.y, reservoir);
            writeReservoir(reservoirBuffer1, pixelIndex, sampleIndex * 2 + 1, frameDim.x * frameDim.y, reservoir);
        }

        writeReservoir(initialSamples, pixelIndex, 0, frameDim.x* frameDim.y, reservoir);
        //writeReservoir(initialSamples, pixelIndex, 1, frameDim.x * frameDim.y, reservoir);
    }
};

cbuffer CB
{
    GIClearReservoirs gGIClearReservoirs;
}

[numthreads(16, 16, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gGIClearReservoirs.execute(dispatchThreadId.xy);
}
