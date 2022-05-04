/***************************************************************************
 # Copyright (c) 2020, NVIDIA CORPORATION. All rights reserved.
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

/** This file contains BSDF evaluation and importance sampling functions.

    There are two versions of the code:
    1) by default it uses the original BSDF code in this file,
    2) if _USE_LEGACY_SHADING_CODE == 0, it calls into the new functions in BxDF.slang.

    The new code in BxDF.slang is under development.
    TODO: Remove the legacy code when the new code is well-tested.

*/

#include "Utils/Math/MathConstants.slangh"
#include "BxDFConfig.slangh"

import Scene.ShadingData;
import Utils.Math.MathHelpers;
import Utils.Color.ColorHelpers;
import Utils.Debug.PixelDebug;
__exported import Utils.Sampling.SampleGeneratorInterface;
__exported import Rendering.Materials.BxDF;
__exported import Rendering.Materials.MaterialHelpers;

// Run legacy BSDF code by default. The host can override this by defining _USE_LEGACY_SHADING_CODE=0.
// TODO: Remove this when the new code has been properly validated
#ifndef _USE_LEGACY_SHADING_CODE
#define _USE_LEGACY_SHADING_CODE 1
#endif

// By default do not evaluate all lobes at sampleBSDF.
#ifndef _USE_DETERMINISTIC_BSDF
#define _USE_DETERMINISTIC_BSDF 0
#endif

/** Describes a BSDF sample.
*/
struct BSDFSample_
{
    float3  wi;             ///< Incident direction in world space (normalized).
    float   pdf;            ///< pdf with respect to solid angle for sampling incident direction wi.
    float3  weight;         ///< Sample weight f(wo, wi) * dot(wi, n) / pdf(wi).
    uint    lobe;           ///< Sampled lobe. This is a combination of LobeType flags (see BxDFTypes.slang).
    float   pdfSingle;

    bool isLobe(LobeType type)
    {
        return (lobe & uint(type)) != 0;
    }
};

// ----------------------------------------------------------------------------
// Legacy interface wrapper
// ----------------------------------------------------------------------------

/** Evaluates whether the incoming and outgoing directions define a transmission event.
    Otherwise, the event is a reflection.
    \param[in] wi The incoming direction pointing outward from the surface.
    \param[in] wo The outgoing direction pointing outward from the surface.
    \param[in] faceN The face normal.
    \return True if wi and wo point to the opposite sides of the plane defined by faceN.
*/
bool isTransmissionEvent(const float3 wi, const float3 wo, const float3 faceN)
{
    return (dot(wi, faceN) > 0) != (dot(wo, faceN) > 0);
}

/** Returns the set of available BSDF lobes given some shading data.
    \param[in] sd Shading data.
    \return Returns a set of lobes (see LobeType in BxDFTypes.slang).
*/
uint getBSDFLobes(const ShadingData sd)
{
    return FalcorBSDF::getLobes(sd);
}

// TODO: rename to canBeRough
bool hasRoughComponent(const ShadingData sd, float roughnessThreshold)
{
    FalcorBSDF bsdf;
    bsdf.setup(sd);
    return bsdf.hasRoughComponent(sd, roughnessThreshold);
}

bool classifyAsRough(const ShadingData sd, float roughnessThreshold)
{
    FalcorBSDF bsdf;
    bsdf.setup(sd);
    return bsdf.classifyAsRough(sd, roughnessThreshold);
}

/** Evaluates the BSDF multiplied by NdotL for a given incident direction.

    \param[in] sd Shading point data.
    \param[in] L Normalized incident direction from shading point towards light source.
    \return (f_d + f_r) * saturate(dot(N,L))
*/
float3 evalBSDFCosine(const ShadingData sd, float3 L, uint allowedSampledFlags=-1, bool allowDeltaEval = false)
{
    float3 wo = sd.toLocal(sd.V);
    float3 wi = sd.toLocal(L);
    FalcorBSDF bsdf;
    bsdf.setup(sd);

    return bsdf.eval(wo, wi, allowedSampledFlags, allowDeltaEval);
}


float3 evalBSDFCosineWithHalfVector(const ShadingData sd, float3 h, inout float3 wi, uint allowedSampledFlags=-1, bool isRefraction=false, bool allowDeltaEval=false)
{
    float3 wo = sd.toLocal(sd.V);

    FalcorBSDF bsdf;
    bsdf.setup(sd);
    float3 res = bsdf.evalWithHalfVector(wo, h, wi, allowedSampledFlags, isRefraction, allowDeltaEval);

    wi = sd.fromLocal(wi);

    return res;
}

/** Importance sampling of the BSDF.

    Note: The evaluated pdf for the generated sample is expensive to compute, as the pdf is a weighted
    combination of two sampling strategies. If the caller doesn't explicitly need the probability, they
    should be careful not to touch the value so that the compiler can do dead code elimination.

    \param[in] sd Shading point data.
    \param[in] sg Sample generator.
    \param[out] result Generated sample. Only valid if true is returned.
    \return True if a sample was generated, false otherwise.
*/
bool sampleBSDF<S : ISampleGenerator>(const ShadingData sd, inout S sg, out BSDFSample_ result, bool useDeterministicBSDF = false)
{
    float3 wo = sd.toLocal(sd.V);
    float3 wi;

    FalcorBSDF bsdf;
    bsdf.setup(sd);
    bool valid = bsdf.sample(wo, wi, result.pdf, result.pdfSingle, result.weight, result.lobe, sg);
    result.wi = sd.fromLocal(wi);

    if (useDeterministicBSDF)
    {
        if (result.pdfSingle == 0.f) result.pdf = 0.f;
        // Ensure consistency with the geometry.
        bool compatibleWithReflection = !isTransmissionEvent(result.wi, sd.V, sd.N);

        uint allowedSampledFlags = 0;
        if (compatibleWithReflection)
        {
            allowedSampledFlags |= (uint)SampledBSDFFlags::DiffuseReflection;
            allowedSampledFlags |= (uint)SampledBSDFFlags::SpecularReflection;
            allowedSampledFlags |= (uint)SampledBSDFFlags::SpecularReflectionTransmission;
        }
        else
        {
            allowedSampledFlags |= (uint)SampledBSDFFlags::DiffuseTransmission;
            allowedSampledFlags |= (uint)SampledBSDFFlags::SpecularReflectionTransmission;
        }

        // Evaluate all lobes and divide by the full PDF. TODO: Optimize.
        result.weight = bsdf.eval(wo, wi, allowedSampledFlags, result.isLobe(LobeType::Delta)) / result.pdf;
    }

    return valid;
}

/** Evaluates the probability density function for both the diffuse and specular sampling strategy.
    \param[in] sd Describes the shading point.
    \param[in] L The normalized incident direction for which to evaluate the pdf.
    \return Probability density with respect to solid angle from the shading point.
*/
float evalPdfBSDF(const ShadingData sd, float3 L, uint allowedSampledFlags = -1, bool allowDeltaEval = false)
{
    float3 wo = sd.toLocal(sd.V);
    float3 wi = sd.toLocal(L);

    FalcorBSDF bsdf;
    bsdf.setup(sd);
    return bsdf.evalPdf(wo, wi, allowedSampledFlags, allowDeltaEval);
}

float evalPdfBSDF(const ShadingData sd, float3 L, out float pdfAll, uint allowedSampledFlags = -1, bool allowDeltaEval = false)
{
    float3 wo = sd.toLocal(sd.V);
    float3 wi = sd.toLocal(L);

    FalcorBSDF bsdf;
    bsdf.setup(sd);
    return bsdf.evalPdfAll(wo, wi, pdfAll, allowedSampledFlags, allowDeltaEval);
}

float evalPdfBSDFWithV(in ShadingData sd, float3 V, float3 L, uint allowedSampledFlags = -1, bool allowDeltaEval = false)
{
    sd.V = V;
    float3 wo = sd.toLocal(V);
    float3 wi = sd.toLocal(L);

    FalcorBSDF bsdf;
    bsdf.setup(sd);
    return bsdf.evalPdf(wo, wi, allowedSampledFlags, allowDeltaEval);
}

/** Reference implementation that uses cosine-weighted hemisphere sampling.
    This is for testing purposes only.
    \param[in] sd Shading point data.
    \param[in] sg Sample generator.
    \param[out] result Generated sample. Only valid if true is returned.
    \return True if a sample was generated, false otherwise.
*/
bool sampleBSDF_Reference<S : ISampleGenerator>(const ShadingData sd, inout S sg, out BSDFSample_ result)
{
    float3 wo = sd.toLocal(sd.V);
    float3 wi = sample_cosine_hemisphere_concentric(sampleNext2D(sg), result.pdf); // pdf = cos(theta) / pi

    if (result.pdf == 0.f) return false;

    FalcorBSDF bsdf;
    bsdf.setup(sd);

    // Ensure consistency with the geometry.
    result.wi = sd.fromLocal(wi);

    bool compatibleWithTransmission = isTransmissionEvent(result.wi, sd.V, sd.N);
    bool compatibleWithReflection = !compatibleWithTransmission;

    uint allowedSampledFlags = 0;
    if (compatibleWithReflection)
    {
        allowedSampledFlags |= (uint)SampledBSDFFlags::DiffuseReflection;
        allowedSampledFlags |= (uint)SampledBSDFFlags::SpecularReflection;
        allowedSampledFlags |= (uint)SampledBSDFFlags::SpecularReflectionTransmission;
    }
    else
    {
        allowedSampledFlags |= (uint)SampledBSDFFlags::DiffuseTransmission;
        allowedSampledFlags |= (uint)SampledBSDFFlags::SpecularReflectionTransmission;
    }

    result.weight = bsdf.eval(wo, wi, allowedSampledFlags) / result.pdf;
    result.lobe = (uint)LobeType::DiffuseReflection;

    return true;
}


