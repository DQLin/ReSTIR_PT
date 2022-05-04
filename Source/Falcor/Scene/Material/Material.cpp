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
#include "Material.h"

namespace Falcor
{
    namespace
    {
        static_assert(static_cast<uint32_t>(MaterialType::Count) <= (1u << kMaterialTypeBits), "MaterialType count exceeds the maximum");
    }

    Material::UpdateFlags Material::sGlobalUpdates = Material::UpdateFlags::None;
    const Material::TextureSlotInfo Material::sUnusedTextureSlotInfo;

    Material::Material(const std::string& name)
        : mName(name)
    {
    }

    bool Material::renderUI(Gui::Widgets& widget)
    {
        widget.text("Type: " + to_string(getType()));
        return false;
    }

    void Material::setTexture(const TextureSlot slot, const Texture::SharedPtr& pTexture)
    {
        logWarning("Material::setTexture() - Material '" + getName() + "' does not have texture slot '" + to_string(slot) + "'. Ignoring call.");
    }

    void Material::loadTexture(const TextureSlot slot, const std::string& filename, bool useSrgb)
    {
        logWarning("Material::loadTexture() - Material '" + getName() + "' does not have texture slot '" + to_string(slot) + "'. Ignoring call.");
    }

    uint2 Material::getMaxTextureDimensions() const
    {
        uint2 dim = uint2(0);
        for (uint32_t i = 0; i < (uint32_t)TextureSlot::Count; i++)
        {
            const auto& pTexture = getTexture((TextureSlot)i);
            if (pTexture) dim = max(dim, uint2(pTexture->getWidth(), pTexture->getHeight()));
        }
        return dim;
    }

    void Material::markUpdates(UpdateFlags updates)
    {
        mUpdates |= updates;
        sGlobalUpdates |= updates;
    }

    void Material::setTextureTransform(const Transform& textureTransform)
    {
        mTextureTransform = textureTransform;
    }

    SCRIPT_BINDING(Material)
    {
        SCRIPT_BINDING_DEPENDENCY(Transform)

        pybind11::enum_<MaterialType> materialType(m, "MaterialType");
        materialType.value("Standard", MaterialType::Standard);
        materialType.value("Cloth", MaterialType::Cloth);
        materialType.value("Hair", MaterialType::Hair);

        pybind11::enum_<Material::TextureSlot> textureSlot(m, "MaterialTextureSlot");
        textureSlot.value("BaseColor", Material::TextureSlot::BaseColor);
        textureSlot.value("Specular", Material::TextureSlot::Specular);
        textureSlot.value("Emissive", Material::TextureSlot::Emissive);
        textureSlot.value("Normal", Material::TextureSlot::Normal);
        textureSlot.value("Transmission", Material::TextureSlot::Transmission);
        textureSlot.value("Displacement", Material::TextureSlot::Displacement);

        // Register Material base class as IMaterial in python to allow deprecated script syntax.
        // TODO: Remove workaround when all scripts have been updated to create derived Material classes.
        pybind11::class_<Material, Material::SharedPtr> material(m, "IMaterial"); // PYTHONDEPRECATED
        material.def_property_readonly("type", &Material::getType);
        material.def_property("name", &Material::getName, &Material::setName);
        material.def_property("textureTransform", pybind11::overload_cast<void>(&Material::getTextureTransform, pybind11::const_), &Material::setTextureTransform);

        material.def("loadTexture", &Material::loadTexture, "slot"_a, "filename"_a, "useSrgb"_a = true);
        material.def("clearTexture", &Material::clearTexture, "slot"_a);
    }
}
