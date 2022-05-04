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
#include "Scene/ScenePrimitiveDefines.slangh"
import Scene.Scene;
__exported import Scene.HitInfo;
__exported import Scene.Shading;
import Utils.Math.MathHelpers;
import Rendering.Materials.TexLODHelpers;
import Params;
import Utils.Debug.PixelDebug;

/** Compute derivatives at primarty triangle hit.
*/
void computeDerivativesAtPrimaryTriangleHit(const TriangleHit hit, uint2 pixel, uint2 frameDim, out float2 ddx, out float2 ddy)
{
    // Get world space position
    VertexData v = gScene.getVertexData(hit);
    float hitT = length(v.posW - gScene.camera.getPosition());
    const float3 nonNormalizedRayDir = gScene.camera.computeNonNormalizedRayDirPinhole(pixel, frameDim);
    float3 rayDir = normalize(nonNormalizedRayDir);

    float3 dDdx, dDdy;
    computeRayDirectionDifferentials(nonNormalizedRayDir, gScene.camera.data.cameraU, gScene.camera.data.cameraV, frameDim, dDdx, dDdy);

    RayDiff rd = RayDiff(float3(0.0f), float3(0.0f), dDdx, dDdy);
    rd = rd.propagate(v.posW.xyz, rayDir, hitT, v.faceNormalW);

    // Bounce using primary hit geometry.
    const float4x4 worldMat = gScene.getWorldMatrix(hit.instanceID);
    const float3x3 worldInvTransposeMat = gScene.getInverseTransposeWorldMatrix(hit.instanceID);
    const uint3 vertexIndices = gScene.getIndices(hit.instanceID, hit.primitiveIndex);
    StaticVertexData vertices[3] = { gScene.getVertex(vertexIndices[0]), gScene.getVertex(vertexIndices[1]), gScene.getVertex(vertexIndices[2]) };
    float2 dBarydx, dBarydy;
    float3 unnormalizedN, normals[3];

    prepareRayDiffAtHitPoint(v, vertices, hit.getBarycentricWeights(), rayDir, worldMat, worldInvTransposeMat, rd, unnormalizedN, normals, dBarydx, dBarydy, ddx, ddy);
}

/** Setup vertex data based on hit point.
    \param[in] hit Hit information.
    \param[out] materialID Material ID.
    \param[out] curveSphereRadius Curve sphere radius.
    \return VertexData struct.
*/
VertexData loadVertexData(const HitInfo hit, const float3 viewDir, out uint materialID, out float curveSphereRadius)
{
    VertexData v = {};
#if IS_SET(SCENE_PRIMITIVE_TYPE_FLAGS, PRIMITIVE_TYPE_TRIANGLE_MESH)
#if IS_SET(SCENE_PRIMITIVE_TYPE_FLAGS, PRIMITIVE_TYPE_PROCEDURAL)
    if (hit.getType() == HitType::Triangle)
#endif
    {
        const TriangleHit triangleHit = hit.getTriangleHit();
        v = gScene.getVertexData(triangleHit);
        materialID = gScene.getMaterialID(triangleHit.instanceID);
    }
#endif
#if IS_SET(SCENE_PRIMITIVE_TYPE_FLAGS, PRIMITIVE_TYPE_DISPLACED_TRIANGLE_MESH)
    if (hit.getType() == HitType::DisplacedTriangle)
    {
        const DisplacedTriangleHit displacedTriangleHit = hit.getDisplacedTriangleHit();
        v = gScene.getVertexData(displacedTriangleHit, viewDir);
        materialID = gScene.getMaterialID(displacedTriangleHit.instanceID);
    }
#endif
#if IS_SET(SCENE_PRIMITIVE_TYPE_FLAGS, PRIMITIVE_TYPE_CURVE)
    if (hit.getType() == HitType::Curve)
    {
        const CurveHit curveHit = hit.getCurveHit();
        v = gScene.getVertexDataFromCurve(curveHit, curveSphereRadius);
        materialID = gScene.getCurveMaterialID(curveHit.instanceID);
    }
#endif
    return v;
}

/** Adjust ShadingData
    \param[in,out] sd Shading data to update.
    \param[in] hit Hit information.
    \param[in] curveSphereRadius Curve sphere radius.
    \param[in] v Vertex data.
    \param[in] isPrimary True if this is the primary hit point.
*/
void adjustShadingData(inout ShadingData sd, const HitInfo hit, const float curveSphereRadius, const VertexData v, const bool isPrimary)
{
    // Set sphere radius at the curve intersection point.
    // Reuse the metallic field.
#if IS_SET(SCENE_PRIMITIVE_TYPE_FLAGS, PRIMITIVE_TYPE_CURVE)
    if (hit.getType() == HitType::Curve) sd.metallic = curveSphereRadius;
#endif

#if GBUFFER_ADJUST_SHADING_NORMALS
    if (kAdjustShadingNormals || isPrimary) adjustShadingNormal(sd, v);
#else
    if (kAdjustShadingNormals && !isPrimary) adjustShadingNormal(sd, v);
#endif
}

/** Setup ShadingData based on loaded vertex/material attributes for a hit point.
    \param[in] hit Hit information.
    \param[in] viewDir Normalized view direction.
    \param[in] isPrimary True if this is the primary hit point.
    \param[in] lod level of detail bias value.
    \return ShadingData struct.
*/
ShadingData loadShadingData(const HitInfo hit, const float3 viewDir, const bool isPrimary, const float lod = 0.f)
{
    float curveSphereRadius = {};
    uint materialID = {};
    VertexData v = loadVertexData(hit, viewDir, materialID, curveSphereRadius);
    ShadingData sd = prepareShadingData(v, materialID, gScene.materials[materialID], gScene.materialResources[materialID], viewDir, lod);
    adjustShadingData(sd, hit, curveSphereRadius, v, isPrimary);
    return sd;
}

// daqi: currently don't support DisplacedTriangle
ShadingData loadShadingDataWithPrevVertexPosition(const HitInfo hit, const float3 prevVertexPosition, const bool isPrimary, const float lod = 0.f)
{
    float curveSphereRadius = {};
    uint materialID = {};
    VertexData v = loadVertexData(hit, float3(0.f), materialID, curveSphereRadius);
    float3 viewDir = normalize(prevVertexPosition - v.posW);
    ShadingData sd = prepareShadingData(v, materialID, gScene.materials[materialID], gScene.materialResources[materialID], viewDir, lod);
    adjustShadingData(sd, hit, curveSphereRadius, v, isPrimary);
    return sd;
}

/** Setup ShadingData based on loaded vertex/material attributes for a hit point.
    \param[in] hit Hit information.
    \param[in] viewDir Normalized view direction.
    \param[in] isPrimary True if this is the primary hit point.
    \param[in] ddx footprint vector.
    \param[in] ddy footprint vector.
    \return ShadingData struct.
*/
ShadingData loadShadingDataFootprint(const HitInfo hit, const float3 viewDir, const bool isPrimary, const float2 ddx, const float2 ddy)
{
    float curveSphereRadius = {};
    uint materialID = {};
    VertexData v = loadVertexData(hit, viewDir, materialID, curveSphereRadius);
    ShadingData sd = prepareShadingData(v, materialID, gScene.materials[materialID], gScene.materialResources[materialID], viewDir, ddx, ddy);
    adjustShadingData(sd, hit, curveSphereRadius, v, isPrimary);
    return sd;
}
