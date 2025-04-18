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
import Rendering.Materials.Fresnel;
import Rendering.Materials.Microfacet;
import Scene.Scene;
import Scene.ShadingData;

/** Get material reflectance based on the metallic value.
*/
float3 getMaterialReflectanceForDeltaPaths(const MaterialType materialType, const bool hasDeltaLobes, const ShadingData sd)
{
    if (materialType == MaterialType::Standard)
    {
        const MaterialData md = gScene.getMaterial(sd.materialID);
        const float metallic = md.specular.b; // Blue component stores metallic in MetalRough mode.

        if (metallic == 0.f)
        {
            return (1.f - sd.diffuseTransmission) * (1.f - sd.specularTransmission) * sd.diffuse;
        }
        // Handle only non-delta specular lobes.
        else if (metallic == 1.f && !hasDeltaLobes)
        {
            float NdotV = saturate(dot(sd.N, sd.V));
            float ggxAlpha = sd.linearRoughness * sd.linearRoughness;
            float3 specularReflectionAlbedo = (1.f - sd.specularTransmission) * sd.specular;
            return approxSpecularIntegralGGX(specularReflectionAlbedo, ggxAlpha, NdotV);
        }
    }
    else if (materialType == MaterialType::Hair)
    {
        return (1.f - sd.diffuseTransmission) * (1.f - sd.specularTransmission) * sd.diffuse;
    }

    return 1.f;
}

bool isDeltaReflectionAllowedAlongDeltaTransmissionPath(const ShadingData sd)
{
    const MaterialData md = gScene.getMaterial(sd.materialID);
    const float metallic = md.specular.b; // Blue component stores metallic in MetalRough mode.
    //const float insideIoR = gScene.materials.evalIoR(sd.materialID);

    const float eta = sd.frontFacing ? 1.f / sd.IoR : sd.IoR;
    bool totalInternalReflection = evalFresnelDielectric(eta, sd.toLocal(sd.V).z) == 1.f;

    bool nonTransmissiveMirror = (md.specularTransmission == 0.f) && (metallic == 1.f);

    return totalInternalReflection || nonTransmissiveMirror;
}
