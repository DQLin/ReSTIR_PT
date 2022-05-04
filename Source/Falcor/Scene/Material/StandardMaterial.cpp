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
#include "stdafx.h"
#include "StandardMaterial.h"

namespace Falcor
{
    StandardMaterial::SharedPtr StandardMaterial::create(const std::string& name, uint32_t shadingModel)
    {
        return SharedPtr(new StandardMaterial(name, shadingModel));
    }

    StandardMaterial::StandardMaterial(const std::string& name, uint32_t shadingModel)
        : BasicMaterial(name)
    {
        mData.type = static_cast<uint32_t>(getType());

        setShadingModel(shadingModel);
        bool specGloss = getShadingModel() == ShadingModelSpecGloss;

        mTextureSlotInfo[(uint32_t)TextureSlot::BaseColor] = { specGloss ? "diffuse" : "baseColor", TextureChannelFlags::RGBA, true };
        mTextureSlotInfo[(uint32_t)TextureSlot::Specular] = specGloss ? TextureSlotInfo{ "specular", TextureChannelFlags::RGBA, true } : TextureSlotInfo{ "spec", TextureChannelFlags::Green | TextureChannelFlags::Blue, false };
        mTextureSlotInfo[(uint32_t)TextureSlot::Transmission] = { "transmission", TextureChannelFlags::RGB, true };
    }

    bool StandardMaterial::renderUI(Gui::Widgets& widget)
    {
        widget.text("Shading model:");
        if (getShadingModel() == ShadingModelMetalRough) widget.text("MetalRough", true);
        else if (getShadingModel() == ShadingModelSpecGloss) widget.text("SpecGloss", true);
        else should_not_get_here();

        // Render the base class UI.
        return BasicMaterial::renderUI(widget);
    }

    void StandardMaterial::renderSpecularUI(Gui::Widgets& widget)
    {
        if (getShadingModel() == ShadingModelMetalRough)
        {
            float roughness = getRoughness();
            if (widget.var("Roughness", roughness, 0.f, 1.f, 0.01f)) setRoughness(roughness);

            float metallic = getMetallic();
            if (widget.var("Metallic", metallic, 0.f, 1.f, 0.01f)) setMetallic(metallic);
        }
    }

    void StandardMaterial::setShadingModel(uint32_t model)
    {
        if (model != ShadingModelMetalRough && model != ShadingModelSpecGloss) throw std::runtime_error("StandardMaterial::setShadingModel() - model must be MetalRough or SpecGloss");
        setFlags(PACK_SHADING_MODEL(mData.flags, model));
    }

    void StandardMaterial::setRoughness(float roughness)
    {
        if (getShadingModel() != ShadingModelMetalRough)
        {
            logWarning("Ignoring setRoughness(). Material '" + mName + "' does not use the metallic/roughness shading model.");
            return;
        }

        if (mData.specular.g != roughness)
        {
            mData.specular.g = roughness;
            markUpdates(UpdateFlags::DataChanged);
            updateSpecularType();
        }
    }

    void StandardMaterial::setMetallic(float metallic)
    {
        if (getShadingModel() != ShadingModelMetalRough)
        {
            logWarning("Ignoring setMetallic(). Material '" + mName + "' does not use the metallic/roughness shading model.");
            return;
        }

        if (mData.specular.b != metallic)
        {
            mData.specular.b = metallic;
            markUpdates(UpdateFlags::DataChanged);
            updateSpecularType();
        }
    }

    SCRIPT_BINDING(StandardMaterial)
    {
        SCRIPT_BINDING_DEPENDENCY(BasicMaterial)

        pybind11::class_<StandardMaterial, BasicMaterial, StandardMaterial::SharedPtr> material(m, "StandardMaterial");
        material.def(pybind11::init(&StandardMaterial::create), "name"_a = "", "model"_a = ShadingModelMetalRough);

        material.def_property("roughness", &StandardMaterial::getRoughness, &StandardMaterial::setRoughness);
        material.def_property("metallic", &StandardMaterial::getMetallic, &StandardMaterial::setMetallic);

        // Register alias Material -> StandardMaterial to allow deprecated script syntax.
        // TODO: Remove workaround when all scripts have been updated to create StandardMaterial directly.
        m.attr("Material") = m.attr("StandardMaterial"); // PYTHONDEPRECATED
    }
}
