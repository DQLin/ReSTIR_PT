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
import Utils.Color.ColorHelpers;
import LocalEmissiveTriangle;

/** Collects emissive triangles from the scene representation and copies
    them to a compact local array for fast access during light sampling and evaluation.
*/
struct UpdateEmissiveTriangles
{
    // Resources.
    RWStructuredBuffer<PackedLocalEmissiveTriangle> emissiveTriangles;

    uint emissiveTriangleCount;

    void process(uint triangleIndex)
    {
        if (triangleIndex >= emissiveTriangleCount) return;

        EmissiveTriangle inTriangle = gScene.lightCollection.getTriangle(triangleIndex);

        LocalEmissiveTriangle outTriangle = {};
        outTriangle.pos = inTriangle.posW[0];
        outTriangle.normal = inTriangle.normal;
        outTriangle.edge0 = inTriangle.posW[1] - inTriangle.posW[0];
        outTriangle.edge1 = inTriangle.posW[2] - inTriangle.posW[0];
        outTriangle.radiance = luminance(gScene.lightCollection.getAverageRadiance(triangleIndex));

        emissiveTriangles[triangleIndex] = outTriangle.pack();
    }
};

cbuffer CB
{
    UpdateEmissiveTriangles gUpdateEmissiveTriangles;
};

[numthreads(256, 1, 1)]
void main(uint3 dispatchThreadId : SV_DispatchThreadID)
{
    gUpdateEmissiveTriangles.process(dispatchThreadId.x);
}
