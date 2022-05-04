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
import Scene.RaytracingInline;
__exported import Scene.HitInfo;
import Scene.Shading;
import Utils.Math.MathHelpers;
import Rendering.Materials.Microfacet;
import Rendering.Materials.Fresnel;
import Lights;
__exported import SurfaceData;

/** Context for evaluating light samples.
*/
struct EvalContext
{
    // Static configuration.
    static const bool kUseAlphaTest = USE_ALPHA_TEST;

    float3 pos;
    float depth;
    float3 N;
    float3 V;
    float NdotV;
    float diffuseWeight;
    float specularWeight;
    float diffuseSpecularMix;
    float ggxAlpha;
    uint lobes;

    bool valid;

    static EvalContext create(uint2 pixel, uint2 frameDim, SurfaceData surfaceData)
    {
        const Ray ray = gScene.camera.computeRayPinhole(pixel, frameDim);

        EvalContext evalContext = {};
        evalContext.pos = surfaceData.pos;
        evalContext.depth = surfaceData.depth;
        evalContext.N = surfaceData.normal;
        evalContext.V = -ray.dir;
        evalContext.NdotV = saturate(dot(evalContext.N, evalContext.V));
        evalContext.diffuseWeight = surfaceData.diffuseWeight;
        evalContext.specularWeight = surfaceData.specularWeight;
        float weightSum = surfaceData.diffuseWeight + surfaceData.specularWeight;
        evalContext.diffuseSpecularMix = weightSum > 1e-7f ? (surfaceData.diffuseWeight / weightSum) : 1.f;
        // Clamp roughness to avoid numerical instability when evaluating the GGX lobe.
        // This is the same threshold as used in the legacy Falcor BRDF.
        float roughness = max(ROUGHNESS_THRESHOLD, surfaceData.specularRoughness);
        evalContext.ggxAlpha = roughness * roughness;
        evalContext.lobes = surfaceData.lobes;
        evalContext.valid = surfaceData.isValid();
        return evalContext;
    }

    bool isValid()
    {
        return valid;
    }

    /** Compare depth and normal between this and another pixel and determine if other sample is valid for reuse.
        \param[in] otherNormalDepth Normal and depth of other pixel.
        \param[in] normalThreshold Normal cosine threshold.
        \param[in] depthThreshold Relative depth threshold.
        \return Returns true if other sample should be reused.
    */
    bool isValidNeighbor(float4 otherNormalDepth, float normalThreshold, float depthThreshold)
    {
        return
            dot(N, otherNormalDepth.xyz) >= normalThreshold &&
            abs((depth - otherNormalDepth.w)) <= depthThreshold * depth;
    }

    /** Compare depth and normal between this and another pixel and determine if other sample is valid for reuse.
        \param[in] otherContext Other evaluation context.
        \param[in] normalThreshold Normal cosine threshold.
        \param[in] depthThreshold Relative depth threshold.
        \return Returns true if other sample should be reused.
    */
    bool isValidNeighbor(const EvalContext otherContext, float normalThreshold, float depthThreshold)
    {
        return isValidNeighbor(float4(otherContext.N, otherContext.depth), normalThreshold, depthThreshold);
    }

    /** Transform direction from world- to local-space.
        \param[in] w Direction in world-space.
        \return Returns direction in local-space.
    */
    float3 toLocal(float3 w)
    {
        float3 B = perp_stark(N);
        float3 T = cross(B, N);
        return float3(dot(B, w), dot(T, w), dot(N, w));
    }

    /** Transform direction from local- to world-space.
        \param[in] w Direction in local-space.
        \return Returns direction in world-space.
    */
    float3 toGlobal(float3 w)
    {
        float3 B = perp_stark(N);
        float3 T = cross(B, N);
        return B * w.x + T * w.y + N * w.z;
    }

    /** Sample the BRDF.
        \param[in] specularOnly Use specular lobe only.
        \param[in] xi Uniform random numbers.
        \param[out] dir Sampled direction in world-space.
        \return Returns true if valid sample was generated.
    */
    bool sampleBRDF(bool specularOnly, float3 xi, out float3 dir)
    {
        dir = {};

        if (xi.x < diffuseSpecularMix)
        {
            if (specularOnly) return false;
            float pdf;
            dir = toGlobal(sample_cosine_hemisphere_concentric(xi.yz, pdf));
        }
        else
        {
            float pdf;
            // float3 h = sampleGGX_VNDF(ggxAlpha, toLocal(V), xi.yz, pdf);
            float3 h = sampleGGX_NDF(ggxAlpha, xi.yz, pdf);
            dir = reflect(-V, toGlobal(h));
        }

        return dot(N, dir) > 0.f;
    }

    /** Evaluate the BRDF PDF.
        \param[in] specularOnly Use specular lobe only.
        \param[in] dir Direction in world-space.
        \return Returns the PDF.
    */
    float evalPdfBRDF(bool specularOnly, float3 dir)
    {
        float cosTheta = saturate(dot(N, dir));
        float diffusePdf = specularOnly ? 0.f : cosTheta * M_1_PI;
        float3 h = normalize(toLocal(dir + V));
        // float specularPdf = evalPdfGGX_VNDF(ggxAlpha, toLocal(dir), h) / (4.f * saturate(dot(h, toLocal(V))));
        float specularPdf = evalPdfGGX_NDF(ggxAlpha, h.z) / (4.f * saturate(dot(h, toLocal(V))));
        float pdf = cosTheta > 0.f ? lerp(specularPdf, diffusePdf, diffuseSpecularMix) : 0.f;
        return pdf;
    }

    /** Evaluate the BRDF.
        \param[in] L Direction to light in world-space.
        \return Returns the BRDF value.
    */
    float evalBRDF(float3 L)
    {
        float NdotL = saturate(dot(N, L));
        float3 H = normalize(V + L);
        float NdotH = saturate(dot(N, H));
        float LdotH = saturate(dot(L, H));

        float D = evalNdfGGX(ggxAlpha, NdotH);
        float G = evalMaskingSmithGGXSeparable(ggxAlpha, NdotV, NdotL);
        float F = specularWeight < 1e-8f ? 0.f : evalFresnelSchlick(specularWeight, 1.f, LdotH) / specularWeight;

        float diffuse = NdotL * M_1_PI;
        float specular = max(0.f, D * G * F / (4.f * NdotV));
        return NdotL > 0.f ? lerp(specular, diffuse, diffuseSpecularMix) : 0.f;
    }

    /** Evaluate the target function (target pdf) of a light sample.
        \param[in] evaluatedLightSample Evaluated light sample.
        \param[in] withVisibility Evaluate visibility term.
        \return Returns the target function value.
    */
    float evalTargetFunction(const EvaluatedLightSample evaluatedLightSample, const bool withVisibility = false)
    {
        float brdfWeight = evalBRDF(evaluatedLightSample.dir);
        float weight = max(0.f, evaluatedLightSample.emission * evaluatedLightSample.geomFactor * brdfWeight);
        if (withVisibility) weight *= evalVisibility(evaluatedLightSample);
        return weight;
    }

    /** Evaluate the target function (target pdf) of a light sample.
        \param[in] loadedLightSample Loaded light sample.
        \param[in] withVisibility Evaluate visibility term.
        \return Returns the target function value.
    */
    float evalTargetFunction(const LoadedLightSample loadedLightSample, const bool withVisibility = false)
    {
        return evalTargetFunction(loadedLightSample.eval(pos), withVisibility);
    }

    /** Evaluate the visibility of a light sample.
        \param[in] evaluatedLightSample Evaluated light sample.
        \return Returns the visibility, 1.f if visible, 0.f if occluded.
    */
    float evalVisibility(const EvaluatedLightSample evaluatedLightSample)
    {
        RayDesc ray = { pos, 0.f, evaluatedLightSample.dir, max(0.f, evaluatedLightSample.distance - Lights::kRayEpsilon) };
        SceneRayQuery<kUseAlphaTest> sceneRayQuery;
        return sceneRayQuery.traceVisibilityRay(ray, RAY_FLAG_NONE, 0xff) ? 1.f : 0.f;
    }

    /** Evaluate the visibility of a light sample.
        \param[in] loadedLightSample Loaded light sample.
        \return Returns the visibility, 1.f if visible, 0.f if occluded.
    */
    float evalVisibility(const LoadedLightSample loadedLightSample)
    {
        return evalVisibility(loadedLightSample.eval(pos));
    }

    /** Trace a ray and report the hit point.
        \param[in] ray Ray description.
        \param[out] hit Hit info.
        \return Returns true on hit.
    */
    bool traceRay(const RayDesc ray, out HitInfo hit)
    {
        SceneRayQuery<kUseAlphaTest> sceneRayQuery;
        float hitT;
        return sceneRayQuery.traceRay(ray, hit, hitT, RAY_FLAG_NONE, 0xff);
    }
};
