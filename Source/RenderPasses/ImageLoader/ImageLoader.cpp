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
#include "ImageLoader.h"

namespace
{
    const char kDesc[] = "Load an image into a texture";
    const std::string kDst = "dst";

    const std::string kOutputSize = "outputSize";
    const std::string kOutputFormat = "outputFormat";
    const std::string kImage = "filename";
    const std::string kMips = "mips";
    const std::string kSrgb = "srgb";
    const std::string kArraySlice = "arrayIndex";
    const std::string kMipLevel = "mipLevel";
}

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary& lib)
{
    lib.registerClass("ImageLoader", kDesc, ImageLoader::create);
}

std::string ImageLoader::getDesc() { return kDesc; }

RenderPassReflection ImageLoader::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;
    uint2 fixedSize = mpTex ? uint2(mpTex->getWidth(), mpTex->getHeight()) : uint2(0);
    const uint2 sz = RenderPassHelpers::calculateIOSize(mOutputSizeSelection, fixedSize, compileData.defaultTexDims);

    reflector.addOutput(kDst, "Destination texture").format(mOutputFormat).texture2D(sz.x, sz.y);
    return reflector;
}

ImageLoader::SharedPtr ImageLoader::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new ImageLoader(dict));
}

ImageLoader::ImageLoader(const Dictionary& dict)
{
    for (const auto& [key, value] : dict)
    {
        if (key == kOutputSize) mOutputSizeSelection = value;
        else if (key == kOutputFormat) mOutputFormat = value;
        else if (key == kImage) mImageName = value.operator std::string();
        else if (key == kSrgb) mLoadSRGB = value;
        else if (key == kMips) mGenerateMips = value;
        else if (key == kArraySlice) mArraySlice = value;
        else if (key == kMipLevel) mMipLevel = value;
        else logWarning("Unknown field '" + key + "' in a ImageLoader dictionary");
    }

    if (!mImageName.empty())
    {
        // Find the full path of the specified image.
        // We retain this for later as the search paths may change during execution.
        std::string fullPath;
        if (findFileInDataDirectories(mImageName, fullPath))
        {
            mImageName = fullPath;
            mpTex = Texture::createFromFile(mImageName, mGenerateMips, mLoadSRGB);
        }
        if (!mpTex) throw std::runtime_error("ImageLoader() - Failed to load image file '" + mImageName + "'");
    }
}

Dictionary ImageLoader::getScriptingDictionary()
{
    Dictionary dict;
    dict[kOutputSize] = mOutputSizeSelection;
    if (mOutputFormat != ResourceFormat::Unknown) dict[kOutputFormat] = mOutputFormat;
    dict[kImage] = stripDataDirectories(mImageName);
    dict[kMips] = mGenerateMips;
    dict[kSrgb] = mLoadSRGB;
    dict[kArraySlice] = mArraySlice;
    dict[kMipLevel] = mMipLevel;
    return dict;
}

void ImageLoader::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    if (!mpTex) throw std::runtime_error("ImageLoader::compile() - No image loaded!");
}

void ImageLoader::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    const auto& pDstTex = renderData[kDst]->asTexture();
    assert(pDstTex);
    mOutputFormat = pDstTex->getFormat();
    mOutputSize = { pDstTex->getWidth(), pDstTex->getHeight() };

    if (!mpTex)
    {
        pRenderContext->clearRtv(pDstTex->getRTV().get(), float4(0, 0, 0, 0));
        return;
    }

    mMipLevel = std::min(mMipLevel, mpTex->getMipCount() - 1);
    mArraySlice = std::min(mArraySlice, mpTex->getArraySize() - 1);
    pRenderContext->blit(mpTex->getSRV(mMipLevel, 1, mArraySlice, 1), pDstTex->getRTV());
}

void ImageLoader::renderUI(Gui::Widgets& widget)
{
    // When output size requirements change, we'll trigger a graph recompile to update the render pass I/O sizes.
    if (widget.dropdown("Output size", RenderPassHelpers::kIOSizeList, (uint32_t&)mOutputSizeSelection)) requestRecompile();
    widget.tooltip("Specifies the pass output size.\n"
        "'Default' means that the output is sized based on requirements of connected passes.\n"
        "'Fixed' means the output is always at the image's native size.\n"
        "If the output is of a different size than the native image resolution, the image will be rescaled bilinearly." , true);

    bool reloadImage = widget.textbox("Image File", mImageName);
    reloadImage |= widget.checkbox("Load As SRGB", mLoadSRGB);
    reloadImage |= widget.checkbox("Generate Mipmaps", mGenerateMips);

    if (widget.button("Load File"))
    {
        reloadImage |= openFileDialog({}, mImageName);
    }

    if (mpTex)
    {
        if (mpTex->getMipCount() > 1) widget.slider("Mip Level", mMipLevel, 0u, mpTex->getMipCount() - 1);
        if (mpTex->getArraySize() > 1) widget.slider("Array Slice", mArraySlice, 0u, mpTex->getArraySize() - 1);

        widget.image(mImageName.c_str(), mpTex, { 320, 320 });
        widget.text("Image format: " + to_string(mpTex->getFormat()));
        widget.text("Image size: (" + std::to_string(mpTex->getWidth()) + ", " + std::to_string(mpTex->getHeight()) + ")");
        widget.text("Output format: " + to_string(mOutputFormat));
        widget.text("Output size: (" + std::to_string(mOutputSize.x) + ", " + std::to_string(mOutputSize.y) + ")");
    }

    if (reloadImage && !mImageName.empty())
    {
        uint2 prevSize = {};
        if (mpTex) prevSize = { mpTex->getWidth(), mpTex->getHeight() };

        mImageName = stripDataDirectories(mImageName);
        mpTex = Texture::createFromFile(mImageName, mGenerateMips, mLoadSRGB);

        // If output is set to native size and image dimensions have changed, we'll trigger a graph recompile to update the render pass I/O sizes.
        if (mOutputSizeSelection == RenderPassHelpers::IOSize::Fixed && mpTex != nullptr &&
            (mpTex->getWidth() != prevSize.x || mpTex->getHeight() != prevSize.y))
        {
            requestRecompile();
        }
    }
}
