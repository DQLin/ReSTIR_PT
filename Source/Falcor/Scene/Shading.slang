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
#include "Scene/Material/MaterialDefines.slangh"
#include "Scene/ScenePrimitiveDefines.slangh"

__exported import Scene.ShadingData;
import Scene.SceneTypes;
import Scene.TextureSampler;
import Scene.Material.MaterialData;
import Scene.Material.AlphaTest;
import Rendering.Materials.IBxDF;
import Rendering.Lights.EnvMapLighting;
import Utils.Math.MathHelpers;
import Utils.Color.ColorHelpers;
import Scene;

#ifndef MONOCHROME
#define MONOCHROME 0
#endif

//#define MONOCHROME          1

/** Convert RGB to normal (unnormalized).
*/
float3 rgbToNormal(float3 rgb)
{
    return rgb * 2.f - 1.f;
}

/** Convert RG to normal (unnormalized).
*/
float3 rgToNormal(float2 rg)
{
    float3 n;
    n.xy = rg * 2 - 1;

    // Saturate because error from BC5 can break the sqrt
    n.z = saturate(dot(rg, rg)); // z = r*r + g*g
    n.z = sqrt(1 - n.z);
    return n;
}

// TODO: this function is broken an may return negative values.
float getMetallic(float3 diffuse, float3 spec)
{
    // This is based on the way that UE4 and Substance Painter 2 converts base+metallic+specular level to diffuse/spec colors
    // We don't have the specular level information, so the assumption is that it is equal to 0.5 (based on the UE4 documentation)
    // Note that I'm using the luminance here instead of the actual colors. The reason is that there's no guaraentee that all RGB channels will end up with the same metallic value
    float d = luminance(diffuse);
    float s = luminance(spec);
    if (s == 0) return 0;
    float a = 0.04;
    float b = s + d - 0.08;
    float c = 0.04 - s;
    float root = sqrt(b*b - 0.16*c);
    float m = (root - b) * 12.5;
    return m;
}

/** Apply normal map

    The `lod` parameter represents the method to use for computing
    texture level of detail, and must implement the `ITextureSampler` interface.
*/
void applyNormalMap<L:ITextureSampler>(MaterialData md, MaterialResources mr, inout ShadingData sd, float4 tangentW, L lod)
{
    uint mapType = EXTRACT_NORMAL_MAP_TYPE(md.flags);
    if (mapType == NormalMapUnused) return;

    float3 mapN = lod.sampleTexture(mr.normalMap, mr.samplerState, sd.uv).xyz;
    switch(mapType)
    {
    case NormalMapRGB:
        mapN = rgbToNormal(mapN);
        break;
    case NormalMapRG:
        mapN = rgToNormal(mapN.rg);
        break;
    default:
        return;
    }

    // Note if the normal ends up being parallel to the tangent, the tangent frame cannot be orthonormalized.
    // That case is rare enough that it is probably not worth the runtime cost to check for it here.
    // If it occurs we should foremost fix the asset, or if problems persist add a check here.

    // Apply the transformation.
    sd.N = normalize(sd.T * mapN.x + sd.B * mapN.y + sd.N * mapN.z);
    sd.T = normalize(tangentW.xyz - sd.N * dot(tangentW.xyz, sd.N));
    sd.B = cross(sd.N, sd.T) * tangentW.w;
}

/** Internal implementation of `alphaTest`.
    The `lod` parameter represents the method to use for computing texture level of detail, and must implement the `ITextureSampler` interface.
    \return True if hit should be ignored/discarded.
*/
bool _alphaTest<L:ITextureSampler>(VertexData v, MaterialData md, MaterialResources mr, L lod)
{
    if (EXTRACT_ALPHA_MODE(md.flags) != AlphaModeMask) return false;

    // Load opacity from the alpha channel of the base color texture.
    float alpha = sampleTexture(mr.baseColor, mr.samplerState, v.texC, md.baseColor, EXTRACT_ALPHA_TYPE(md.flags), lod).a;
    return evalAlphaTest(alpha, md.alphaThreshold, v.posW);
}

/** Apply alpha test and return the result.
    This version samples alpha using implicit gradients and only works in pixel shaders.
    \return True if hit should be ignored/discarded.
*/
bool alphaTest(VertexData v, MaterialData md, MaterialResources mr)
{
    ImplicitLodTextureSampler lod = {};
    return _alphaTest(v, md, mr, lod);
}

/** Apply alpha test and return the result.
    This version samples alpha at a level of detail specified by the `lod` parameter.
    \return True if hit should be ignored/discarded.
*/
bool alphaTest(VertexData v, MaterialData md, MaterialResources mr, float lod)
{
    ExplicitLodTextureSampler explicitLOD = { lod };
    return _alphaTest(v, md, mr, explicitLOD);
}

/** Apply alpha test and return the result.
    This version samples alpha at a level of detail computed from screen-space gradients `gradX` and `gradY`.
    \return True if hit should be ignored/discarded.
*/
bool alphaTest(VertexData v, MaterialData md, MaterialResources mr, float2 gradX, float2 gradY)
{
    ExplicitGradientTextureSampler lod = { gradX, gradY };
    return _alphaTest(v, md, mr, lod);
}

/** Internal implementation of `prepareShadingData`

    The `lod` parameter represents the method to use for computing
    texture level of detail, and must implement the `ITextureSampler` interface.
*/
ShadingData _prepareShadingData<L:ITextureSampler>(VertexData v, uint materialID, MaterialData md, MaterialResources mr, float3 viewDir, L lod, bool useNormalMap)
{
    ShadingData sd = {};

    // Load base color and opacity. These are sampled from the same texture but either one can be uniform parameters.
    // TODO: Rewrite as single texture fetch followed by extraction of color and opacity to make it easier to optimize.
    float3 baseColor = sampleTexture(mr.baseColor, mr.samplerState, v.texC, md.baseColor, EXTRACT_BASE_COLOR_TYPE(md.flags), lod).rgb;
    sd.opacity = sampleTexture(mr.baseColor, mr.samplerState, v.texC, md.baseColor, EXTRACT_ALPHA_TYPE(md.flags), lod).a;

    sd.posW = v.posW;
    sd.uv = v.texC;
    sd.V = viewDir;
    sd.N = v.normalW;

    sd.faceN = v.faceNormalW;
    sd.frontFacing = dot(sd.V, sd.faceN) >= 0.f;

    sd.materialID = materialID;
    sd.setMaterialType(md.getType());
    sd.setActiveLobes((uint)LobeType::All);
    sd.setDoubleSided(EXTRACT_DOUBLE_SIDED(md.flags));
    sd.setThinSurface(EXTRACT_THIN_SURFACE(md.flags));

    sd.IoR = md.IoR;
    sd.diffuseTransmission = md.diffuseTransmission;
    sd.specularTransmission = md.specularTransmission;
    if (sd.diffuseTransmission > 0.f || sd.specularTransmission > 0.f)
    {
        sd.transmission = sampleTexture(mr.transmission, mr.samplerState, v.texC, float4(md.transmission, 0.f), EXTRACT_TRANS_TYPE(md.flags), lod).rgb;
    }
    sd.eta = sd.frontFacing ? (1 / sd.IoR) : sd.IoR;

    // Setup tangent space.
    bool validTangentSpace = computeTangentSpace(sd, v.tangentW);

    // Sample the specular texture.
    // Depending on the shading model this encodes the material parameters differently.
    float4 spec = sampleTexture(mr.specular, mr.samplerState, v.texC, md.specular, EXTRACT_SPECULAR_TYPE(md.flags), lod);

    // Calculate the specular reflectance for dielectrics from the IoR, as in the Disney BSDF [Burley 2015].
    // UE4 uses 0.08 multiplied by a default specular value of 0.5, hence F0=0.04 as default. The default IoR=1.5 gives the same result.
    float f = (sd.IoR - 1.f) / (sd.IoR + 1.f);
    float F0 = f * f;

    if (md.getType() == MaterialType::Standard)
    {
#if SCENE_HAS_SPEC_GLOSS_MATERIALS
        if (EXTRACT_SHADING_MODEL(md.flags) == ShadingModelMetalRough)
#endif
        {
            // G - Roughness; B - Metallic
            sd.diffuse = lerp(baseColor, float3(0), spec.b);
            sd.specular = lerp(float3(F0), baseColor, spec.b);
            sd.linearRoughness = spec.g;
            sd.metallic = spec.b;
        }
#if SCENE_HAS_SPEC_GLOSS_MATERIALS
        else // ShadingModelSpecGloss
        {
            sd.diffuse = baseColor;
            sd.specular = spec.rgb;
            sd.linearRoughness = 1 - spec.a;
            sd.metallic = getMetallic(sd.diffuse, sd.specular);
        }
#endif
    }
#if IS_SET(SCENE_PRIMITIVE_TYPE_FLAGS, PRIMITIVE_TYPE_CURVE)
    else if (md.getType() == MaterialType::Hair)
    {
        sd.diffuse = baseColor;
        sd.specular = spec.rgb;
    }
#endif
    else if (md.getType() == MaterialType::Cloth)
    {
        sd.diffuse = baseColor;
        sd.specular = F0;
        sd.linearRoughness = spec.g;
    }

    sd.linearRoughness = min(1.f, sd.linearRoughness * gScene.materialRoughnessScale);

    // Sample the emissive texture. Note that triangles are emissive only on the front-facing side.
    if (sd.frontFacing)
    {
        sd.emissive = sampleTexture(mr.emissive, mr.samplerState, v.texC, float4(md.emissive, 1), EXTRACT_EMISSIVE_TYPE(md.flags), lod).rgb * md.emissiveFactor;
    }

    // Apply normal mapping only if we have a valid tangent space.
    if (useNormalMap && validTangentSpace) applyNormalMap(md, mr, sd, v.tangentW, lod);
    sd.NdotV = dot(sd.N, sd.V);

    // Flip the shading normal for back-facing hits on double-sided materials.
    if (!sd.frontFacing && sd.isDoubleSided())
    {
        sd.N = -sd.N;
        sd.NdotV = -sd.NdotV;
    }

#if MONOCHROME
    sd.diffuse = luminance(sd.diffuse);
    sd.specular = luminance(sd.specular);
    sd.emissive = luminance(sd.emissive);
    sd.transmission = luminance(sd.transmission);
#endif

    return sd;
}

/** Prepare the hit-point data
*/
ShadingData prepareShadingData(VertexData v, uint materialID, MaterialData md, MaterialResources mr, float3 viewDir)
{
    ImplicitLodTextureSampler lod = { };
    return _prepareShadingData(v, materialID, md, mr, viewDir, lod, true);
}

/** Prepare the hit-point data

    The `lod` parameter represents the level of detail to use for all material
    texture fetches.
*/
ShadingData prepareShadingData(VertexData v, uint materialID, MaterialData md, MaterialResources mr, float3 viewDir, float lod)
{
    ExplicitLodTextureSampler explicitLOD = { lod };
    return _prepareShadingData(v, materialID, md, mr, viewDir, explicitLOD, true);
}

/** Prepare the hit-point data when using ray cones for texture LOD.
    \param[in] v Vertex data.
    \param[in] materialID Material identifier.
    \param[in] md Material data.
    \param[in] mr Material resources.
    \param[in] camPosW Position of camera in world space.
    \param[in] lodMinusTexDim Texture level-of-details for ray cones with the texture dimensions subtracted out. See TextureSampler and TexLODHelpers.
*/
ShadingData prepareShadingDataUsingRayConesLOD(VertexData v, uint materialID, MaterialData md, MaterialResources mr, float3 camPosW, float lodMinusTexDim)
{
    ExplicitRayConesLodTextureSampler explicitRayConesLOD = { lodMinusTexDim };
    return _prepareShadingData(v, materialID, md, mr, camPosW, explicitRayConesLOD, true);
}

/** Prepare the hit-point data when using ray differentials for texture LOD for isotropic lookups.
    \param[in] v Vertex data.
    \param[in] materialID Material identifier.
    \param[in] md Material data.
    \param[in] mr Material resources.
    \param[in] camPosW Position of camera in world space.
    \param[in] dUVdx Differential texture coordinates in x.
    \param[in] dUVdy Differential texture coordinates in y.
*/
ShadingData prepareShadingDataUsingRayDiffsIsotropic(VertexData v, uint materialID, MaterialData md, MaterialResources mr, float3 camPosW, float2 dUVdx, float2 dUVdy)
{
    ExplicitRayDiffsIsotropicTextureSampler explicitRayDiffsLOD = { dUVdx, dUVdy };
    return _prepareShadingData(v, materialID, md, mr, camPosW, explicitRayDiffsLOD, true);
}

/** Prepare the hit-point data

    The `gradX` and `gradY` parameters should be the screen-space gradients of
    `v.texC` with respect to screen-space X and Y, respectively. These gradient
    values will be used for all material texture fetches.
*/
ShadingData prepareShadingData(VertexData v, uint materialID, MaterialData md, MaterialResources mr, float3 viewDir, float2 gradX, float2 gradY)
{
    ExplicitGradientTextureSampler lod = { gradX, gradY };
    return _prepareShadingData(v, materialID, md, mr, viewDir, lod, true);
}

/** Computes an orthonormal tangent space based on the normal and given tangent.
    \param[in,out] sd ShadingData struct that is updated.
    \param[in] tangent Interpolated tangent in world space (xyz) and bitangent sign (w). The tangent is *only* valid when w is != 0.
    \return True if a valid tangent space was computed based on the supplied tangent.
*/
bool computeTangentSpace(inout ShadingData sd, const float4 tangentW)
{
    // Check that tangent space exists and can be safely orthonormalized.
    // Otherwise invent a tanget frame based on the normal.
    // We check that:
    //  - Tangent exists, this is indicated by a nonzero sign (w).
    //  - It has nonzero length. Zeros can occur due to interpolation or bad assets.
    //  - It is not parallel to the normal. This can occur due to normal mapping or bad assets.
    //  - It does not have NaNs. These will propagate and trigger the fallback.

    float NdotT = dot(tangentW.xyz, sd.N);
    bool nonParallel = abs(NdotT) < 0.9999f;
    bool nonZero = dot(tangentW.xyz, tangentW.xyz) > 0.f;

    bool valid = tangentW.w != 0.f && nonZero && nonParallel;
    if (valid)
    {
        sd.T = normalize(tangentW.xyz - sd.N * NdotT);
        sd.B = cross(sd.N, sd.T) * tangentW.w;
    }
    else
    {
        sd.T = perp_stark(sd.N);
        sd.B = cross(sd.N, sd.T);
    }

    return valid;
}

/** Helper function to adjust the shading normal to reduce black pixels due to back-facing view direction.
    Note: This breaks the reciprocity of the BSDF!
*/
void adjustShadingNormal(inout ShadingData sd, VertexData v)
{
    float3 Ng = sd.frontFacing ? v.faceNormalW : -v.faceNormalW;
    float3 Ns = sd.N;

#ifdef FALCOR_INTERNAL
    // Algorithm from Appendix A.3 of https://arxiv.org/abs/1705.01263
    // The specular reflection of the view direction is computed.
    // If the reflection vector lies under the horizon, the shading normal is adjusted.
    // This ensures that the majority of the reflection lobe lies on the same side,
    // but it does not guarantee that all directions are valid in the general case.

    // Specular reflection in shading normal
    float3 R = reflect(-sd.V, Ns);
    float a = dot(Ng, R);
    if (a < 0) // Perturb normal
    {
        float b = max(0.001, dot(Ns, Ng));
        sd.N = normalize(sd.V + normalize(R - Ns * a / b));
#else
    // Blend the shading normal towards the geometric normal at grazing angles.
    // This is to avoid the view vector from becoming back-facing.
    const float kCosThetaThreshold = 0.1f;
    float cosTheta = dot(sd.V, Ns);
    if (cosTheta <= kCosThetaThreshold)
    {
        float t = saturate(cosTheta * (1.f / kCosThetaThreshold));
        sd.N = normalize(lerp(Ng, Ns, t));
#endif
        sd.NdotV = dot(sd.N, sd.V);
        computeTangentSpace(sd, v.tangentW);
    }
}
