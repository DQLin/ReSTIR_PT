/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
__exported import Scene.Scene;
__exported import Scene.Shading;
__exported import Scene.HitInfo;
import Utils.Math.MathHelpers;

/** Helper for setting up the ShadingData struct based on loaded data.
    \param[in] pixel Current pixel coordinates.
    \param[in] frameDim Frame dimensions in pixel.
    \param[in] camera Current camera.
    \param[in] vbuffer VBuffer texture.
    \param[out] sd ShadingData struct.
    \return True if the pixel has valid data (not a background pixel). Note sd.V is always valid.
*/
bool loadShadingData(uint2 pixel, uint2 frameDim, const Camera camera, Texture2D<PackedHitInfo> vbuffer, out ShadingData sd)
{
    sd = {};

    float3 rayDir = camera.computeRayPinhole(pixel, frameDim).dir;
    bool valid = false;

    const HitInfo hit = HitInfo(vbuffer[pixel]);
    if (hit.isValid() && hit.getType() == HitType::Triangle)
    {
        const TriangleHit triangleHit = hit.getTriangleHit();

        // Evaluate Falcor's material parameters at the hit point.
        // TODO: Implement texLOD to enable texture filtering in prepareShadingData().
        const VertexData v = gScene.getVertexData(triangleHit);
        const uint materialID = gScene.getMaterialID(triangleHit.instanceID);
        sd = prepareShadingData(v, materialID, gScene.materials[materialID], gScene.materialResources[materialID], -rayDir, 0.f);

        // Adjust shading normals if GBuffer pass has flag enabled.
#if GBUFFER_ADJUST_SHADING_NORMALS
        adjustShadingNormal(sd, v);
#endif
        valid = true;
    }

    sd.V = -rayDir;
    return valid;
}
