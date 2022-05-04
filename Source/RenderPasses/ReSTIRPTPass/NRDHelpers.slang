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
import Rendering.Materials.Microfacet;
import RenderPasses.Shared.Denoising.NRDBuffers;
import Scene.ShadingData;
import PathState;

void setNRDPrimaryHitEmission(NRDBuffers outputNRD, const bool useNRDDemodulation, const PathState path, const uint2 pixel, const bool isPrimaryHit, const float3 emission)
{
    if (isPrimaryHit && path.getSampleIdx() == 0)
    {
        if (useNRDDemodulation)
        {
            outputNRD.primaryHitEmission[pixel] = float4(emission, 1.f);
        }
        else
        {
            // Clear buffers on primary hit only if demodulation is disabled.
            outputNRD.primaryHitEmission[pixel] = 0.f;
        }
    }
}

void setNRDPrimaryHitReflectance(NRDBuffers outputNRD, const bool useNRDDemodulation, const PathState path, const uint2 pixel, const bool isPrimaryHit, const ShadingData sd)
{
    if (isPrimaryHit && path.getSampleIdx() == 0)
    {
        if (useNRDDemodulation)
        {
            float3 diffuseReflectionAlbedo = (1.f - sd.diffuseTransmission) * (1.f - sd.specularTransmission) * sd.diffuse;
            outputNRD.primaryHitDiffuseReflectance[pixel] = float4(diffuseReflectionAlbedo, 1.f);

            const float NdotV = saturate(dot(sd.N, sd.V));
            const float ggxAlpha = sd.linearRoughness * sd.linearRoughness;
            float3 specularReflectionAlbedo = (1.f - sd.specularTransmission) * sd.specular;
            const float3 specularReflectance = approxSpecularIntegralGGX(specularReflectionAlbedo, ggxAlpha, NdotV);
            outputNRD.primaryHitSpecularReflectance[pixel] = float4(specularReflectance, 1.f);
        }
        else
        {
            // Clear buffers on primary hit only if demodulation is disabled.
            outputNRD.primaryHitDiffuseReflectance[pixel] = 1.f;
            outputNRD.primaryHitSpecularReflectance[pixel] = 1.f;
        }
    }
}

void setNRDSampleHitDist(inout PathState path)
{
    if (path.length == 1)
    {
        path.hitDist = float(path.sceneLength);
    }
}

void setNRDSampleEmission(const bool useNRDDemodulation, inout PathState path, const bool isPrimaryHit, const float3 emission)
{
    if (useNRDDemodulation)
    {
        // Always demodulate emission on the primary hit (it seconds as a clear).
        if (isPrimaryHit)
        {
            path.rcPrevVertexWo = emission;
        }
    }
    else if (isPrimaryHit)
    {
        path.rcPrevVertexWo = 0.f;
    }
}

void setNRDSampleReflectance(const bool useNRDDemodulation, inout PathState path, const bool isPrimaryHit, const ShadingData sd)
{
    if (useNRDDemodulation)
    {
        // Always demodulate reflectance from diffuse and specular paths on the primary hit (it seconds as a clear).
        if (isPrimaryHit)
        {
            if (path.isDiffusePrimaryHit())
            {
                float3 diffuseReflectionAlbedo = (1.f - sd.diffuseTransmission) * (1.f - sd.specularTransmission) * sd.diffuse;
                path.sharedScatterDir = diffuseReflectionAlbedo;
            }
            else if (path.isSpecularPrimaryHit())
            {
                const float NdotV = saturate(dot(sd.N, sd.V));
                const float ggxAlpha = sd.linearRoughness * sd.linearRoughness;
                float3 specularReflectionAlbedo = (1.f - sd.specularTransmission) * sd.specular;
                const float3 specularReflectance = approxSpecularIntegralGGX(specularReflectionAlbedo, ggxAlpha, NdotV);
                path.sharedScatterDir = specularReflectance;
            }
            else
            {
                path.sharedScatterDir = 1.f;
            }
        }
    }
    else if (isPrimaryHit)
    {
        path.sharedScatterDir = 1.f;
    }
}

float3 computeSampleReflectance(const bool useNRDDemodulation, bool isDiffusePrimaryHit, bool isSpecularPrimaryHit, const ShadingData sd)
{
    if (useNRDDemodulation)
    {
        float3 sampleReflectance;
        if (isDiffusePrimaryHit)
        {
            float3 diffuseReflectionAlbedo = (1.f - sd.diffuseTransmission) * (1.f - sd.specularTransmission) * sd.diffuse;
            sampleReflectance = diffuseReflectionAlbedo;
        }
        else if (isSpecularPrimaryHit)
        {
            const float NdotV = saturate(dot(sd.N, sd.V));
            const float ggxAlpha = sd.linearRoughness * sd.linearRoughness;
            float3 specularReflectionAlbedo = (1.f - sd.specularTransmission) * sd.specular;
            const float3 specularReflectance = approxSpecularIntegralGGX(specularReflectionAlbedo, ggxAlpha, NdotV);
            sampleReflectance = specularReflectance;
        }
        else
        {
            sampleReflectance = 1.f;
        }
        return sampleReflectance;
    }
    else
    {
        return 1.f;
    }
}
