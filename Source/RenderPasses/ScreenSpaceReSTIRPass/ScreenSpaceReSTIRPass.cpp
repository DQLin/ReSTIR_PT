/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
#include "ScreenSpaceReSTIRPass.h"
#include "RenderGraph/RenderPassHelpers.h"

namespace
{
    const char kDesc[] = "Standalone pass for direct lighting with screen-space ReSTIR.";

    const char kPrepareSurfaceDataFile[] = "RenderPasses/ScreenSpaceReSTIRPass/PrepareSurfaceData.cs.slang";
    const char kFinalShadingFile[] = "RenderPasses/ScreenSpaceReSTIRPass/FinalShading.cs.slang";

    const std::string kInputVBuffer = "vbuffer";
    const std::string kInputMotionVectors = "motionVectors";

    const Falcor::ChannelList kInputChannels =
    {
        { kInputVBuffer, "gVBuffer", "Visibility buffer in packed format", false, ResourceFormat::Unknown },
        { kInputMotionVectors, "gMotionVectors", "Motion vector buffer (float format)", true /* optional */, ResourceFormat::RG32Float },
    };

    const Falcor::ChannelList kOutputChannels =
    {
        { "color",                  "gColor",                   "Final color",              true /* optional */, ResourceFormat::RGBA32Float },
        { "emission",               "gEmission",                "Emissive color",           true /* optional */, ResourceFormat::RGBA32Float },
        { "diffuseIllumination",    "gDiffuseIllumination",     "Diffuse illumination",     true /* optional */, ResourceFormat::RGBA32Float },
        { "diffuseReflectance",     "gDiffuseReflectance",      "Diffuse reflectance",      true /* optional */, ResourceFormat::RGBA32Float },
        { "specularIllumination",   "gSpecularIllumination",    "Specular illumination",    true /* optional */, ResourceFormat::RGBA32Float },
        { "specularReflectance",    "gSpecularReflectance",     "Specular reflectance",     true /* optional */, ResourceFormat::RGBA32Float },
        { "debug",                  "gDebug",                   "Debug output",             true /* optional */, ResourceFormat::RGBA32Float },
    };

    // Scripting options.
    const char* kOptions = "options";
    const char* kNumReSTIRInstances = "NumReSTIRInstances";
}

// Don't remove this. it's required for hot-reload to function properly
extern "C" __declspec(dllexport) const char* getProjDir()
{
    return PROJECT_DIR;
}

extern "C" __declspec(dllexport) void getPasses(Falcor::RenderPassLibrary & lib)
{
    lib.registerClass("ScreenSpaceReSTIRPass", kDesc, ScreenSpaceReSTIRPass::create);
}

std::string ScreenSpaceReSTIRPass::getDesc() { return kDesc; }

ScreenSpaceReSTIRPass::SharedPtr ScreenSpaceReSTIRPass::create(RenderContext* pRenderContext, const Dictionary& dict)
{
    return SharedPtr(new ScreenSpaceReSTIRPass(dict));
}

ScreenSpaceReSTIRPass::ScreenSpaceReSTIRPass(const Dictionary& dict)
{
    parseDictionary(dict);
}

void ScreenSpaceReSTIRPass::parseDictionary(const Dictionary& dict)
{
    ScreenSpaceReSTIR::Options options;
    for (const auto& [key, value] : dict)
    {
        if (key == kOptions) options = value;
        else if (key == kNumReSTIRInstances) mNumReSTIRInstances = value;
        else logWarning("Unknown field '" + key + "' in ScreenSpaceReSTIRPass dictionary");
    }
    mOptions = ScreenSpaceReSTIR::Options::create(options);
    if (!mpScreenSpaceReSTIR.empty() && mpScreenSpaceReSTIR[0]) mpScreenSpaceReSTIR[0]->mOptions = mOptions;
}

Dictionary ScreenSpaceReSTIRPass::getScriptingDictionary()
{
    Dictionary d;
    d[kOptions] = *mOptions.get();// mpScreenSpaceReSTIR->getOptions();
    d[kNumReSTIRInstances] = mNumReSTIRInstances;
    return d;
}

RenderPassReflection ScreenSpaceReSTIRPass::reflect(const CompileData& compileData)
{
    RenderPassReflection reflector;

    addRenderPassOutputs(reflector, kOutputChannels);
    addRenderPassInputs(reflector, kInputChannels);

    return reflector;
}

void ScreenSpaceReSTIRPass::compile(RenderContext* pRenderContext, const CompileData& compileData)
{
    mFrameDim = compileData.defaultTexDims;
}

void ScreenSpaceReSTIRPass::setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene)
{
    mpScene = pScene;
    mpPrepareSurfaceData = nullptr;
    mpFinalShading = nullptr;

    if (!mpScreenSpaceReSTIR.empty())
    {
        //mOptions = mpScreenSpaceReSTIR->getOptions();
        mpScreenSpaceReSTIR.clear();
    }

    if (mpScene)
    {
        if (is_set(pScene->getPrimitiveTypes(), PrimitiveTypeFlags::Procedural))
        {
            logError("This render pass does not support procedural primitives such as curves.");
        }

        mpScreenSpaceReSTIR.resize(mNumReSTIRInstances);
        for (int i = 0; i < mNumReSTIRInstances; i++)
        {
            mpScreenSpaceReSTIR[i] = ScreenSpaceReSTIR::create(mpScene, mOptions, mNumReSTIRInstances, i);
        }
    }
}

bool ScreenSpaceReSTIRPass::onMouseEvent(const MouseEvent& mouseEvent)
{
    return !mpScreenSpaceReSTIR.empty() && mpScreenSpaceReSTIR[0] ? mpScreenSpaceReSTIR[0]->getPixelDebug()->onMouseEvent(mouseEvent) : false;
}


void ScreenSpaceReSTIRPass::updateDict(const Dictionary& dict)
{
    parseDictionary(dict);
    mOptionsChanged = true;
    if (!mpScreenSpaceReSTIR.empty() && mpScreenSpaceReSTIR[0])
        mpScreenSpaceReSTIR[0]->resetReservoirCount();
}

void ScreenSpaceReSTIRPass::execute(RenderContext* pRenderContext, const RenderData& renderData)
{
    if (mNeedRecreateReSTIRInstances) setScene(pRenderContext, mpScene);

    const auto& pVBuffer = renderData[kInputVBuffer]->asTexture();
    const auto& pMotionVectors = renderData[kInputMotionVectors]->asTexture();

    // Clear outputs if ReSTIR module is not initialized.
    if (mpScreenSpaceReSTIR.empty())
    {
        auto clear = [&](const ChannelDesc& channel)
        {
            auto pTex = renderData[channel.name]->asTexture();
            if (pTex) pRenderContext->clearUAV(pTex->getUAV().get(), float4(0.f));
        };
        for (const auto& channel : kOutputChannels) clear(channel);
        return;
    }

    auto& dict = renderData.getDictionary();

    if (dict.keyExists("enableScreenSpaceReSTIR"))
    {
        for (int i = 0; i < mpScreenSpaceReSTIR.size(); i++)
            mpScreenSpaceReSTIR[i]->enablePass((bool)dict["enableScreenSpaceReSTIR"]);
    }

    // Update refresh flag if changes that affect the output have occured.
    if (mOptionsChanged)
    {
        auto flags = dict.getValue(kRenderPassRefreshFlags, Falcor::RenderPassRefreshFlags::None);
        flags |= Falcor::RenderPassRefreshFlags::RenderOptionsChanged;
        dict[Falcor::kRenderPassRefreshFlags] = flags;
        mOptionsChanged = false;
    }

    // Check if GBuffer has adjusted shading normals enabled.
    mGBufferAdjustShadingNormals = dict.getValue(Falcor::kRenderPassGBufferAdjustShadingNormals, false);

    for (int i = 0; i < mpScreenSpaceReSTIR.size(); i++)
    {
        mpScreenSpaceReSTIR[i]->beginFrame(pRenderContext, mFrameDim);

        prepareSurfaceData(pRenderContext, pVBuffer, i);

        mpScreenSpaceReSTIR[i]->updateReSTIRDI(pRenderContext, pMotionVectors);

        finalShading(pRenderContext, pVBuffer, renderData, i);

        mpScreenSpaceReSTIR[i]->endFrame(pRenderContext);
    }

    auto copyTexture = [pRenderContext](Texture* pDst, const Texture* pSrc)
    {
        if (pDst && pSrc)
        {
            assert(pDst && pSrc);
            assert(pDst->getFormat() == pSrc->getFormat());
            assert(pDst->getWidth() == pSrc->getWidth() && pDst->getHeight() == pSrc->getHeight());
            pRenderContext->copyResource(pDst, pSrc);
        }
        else if (pDst)
        {
            pRenderContext->clearUAV(pDst->getUAV().get(), uint4(0, 0, 0, 0));
        }
    };
    // Copy debug output if available. (only support first ReSTIR instance for now)
    if (const auto& pDebug = renderData["debug"]->asTexture())
    {
        copyTexture(pDebug.get(), mpScreenSpaceReSTIR[0]->getDebugOutputTexture().get());
    }
}

void ScreenSpaceReSTIRPass::renderUI(Gui::Widgets& widget)
{
    mNeedRecreateReSTIRInstances = widget.var("Num ReSTIR Instances", mNumReSTIRInstances, 1, 8);

    if (!mpScreenSpaceReSTIR.empty() && mpScreenSpaceReSTIR[0])
    {
        mOptionsChanged = mpScreenSpaceReSTIR[0]->renderUI(widget);
        for (int i = 1; i < mpScreenSpaceReSTIR.size(); i++)
            mpScreenSpaceReSTIR[i]->copyRecompileStateFromOtherInstance(mpScreenSpaceReSTIR[0]);
    }
}

void ScreenSpaceReSTIRPass::prepareSurfaceData(RenderContext* pRenderContext, const Texture::SharedPtr& pVBuffer, int instanceID)
{
    assert(!mpScreenSpaceReSTIR.empty() && mpScreenSpaceReSTIR[instanceID]);

    PROFILE("prepareSurfaceData");

    if (!mpPrepareSurfaceData)
    {
        auto defines = mpScene->getSceneDefines();
        defines.add("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
        mpPrepareSurfaceData = ComputePass::create(kPrepareSurfaceDataFile, "main", defines, false);
        mpPrepareSurfaceData->setVars(nullptr);
    }

    mpPrepareSurfaceData->addDefine("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");

    mpPrepareSurfaceData["gScene"] = mpScene->getParameterBlock();

    auto var = mpPrepareSurfaceData["CB"]["gPrepareSurfaceData"];

    var["vbuffer"] = pVBuffer;
    var["frameDim"] = mFrameDim;
    mpScreenSpaceReSTIR[instanceID]->setShaderData(var["screenSpaceReSTIR"]);

    if (instanceID == 0 && mpFinalShading && mpScreenSpaceReSTIR[0]->mRequestParentRecompile)
    {
        mpFinalShading->setVars(nullptr);
        mpScreenSpaceReSTIR[0]->mRequestParentRecompile = false;
    }

    mpPrepareSurfaceData->execute(pRenderContext, mFrameDim.x, mFrameDim.y);
}

void ScreenSpaceReSTIRPass::finalShading(RenderContext* pRenderContext, const Texture::SharedPtr& pVBuffer, const RenderData& renderData, int instanceID)
{
    assert(!mpScreenSpaceReSTIR.empty() && mpScreenSpaceReSTIR[instanceID]);

    PROFILE("finalShading");

    if (!mpFinalShading)
    {
        auto defines = mpScene->getSceneDefines();
        defines.add("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
        //defines.add("USE_ENV_BACKGROUND", mpScene->useEnvBackground() ? "1" : "0");
        defines.add(getValidResourceDefines(kOutputChannels, renderData));
        mpFinalShading = ComputePass::create(kFinalShadingFile, "main", defines, false);
        mpFinalShading->setVars(nullptr);
    }

    mpFinalShading->addDefine("GBUFFER_ADJUST_SHADING_NORMALS", mGBufferAdjustShadingNormals ? "1" : "0");
    //mpFinalShading->addDefine("USE_ENV_BACKGROUND", mpScene->useEnvBackground() ? "1" : "0");
    mpFinalShading->addDefine("_USE_LEGACY_SHADING_CODE", "0");

    // For optional I/O resources, set 'is_valid_<name>' defines to inform the program of which ones it can access.
    // TODO: This should be moved to a more general mechanism using Slang.
    mpFinalShading->getProgram()->addDefines(getValidResourceDefines(kOutputChannels, renderData));

    mpFinalShading["gScene"] = mpScene->getParameterBlock();

    auto var = mpFinalShading["CB"]["gFinalShading"];

    var["vbuffer"] = pVBuffer;
    var["frameDim"] = mFrameDim;
    var["numReSTIRInstances"] = mNumReSTIRInstances;
    var["ReSTIRInstanceID"] = instanceID;

    mpScreenSpaceReSTIR[instanceID]->setShaderData(var["screenSpaceReSTIR"]);

    // Bind output channels as UAV buffers.
    var = mpFinalShading->getRootVar();
    auto bind = [&](const ChannelDesc& channel)
    {
        Texture::SharedPtr pTex = renderData[channel.name]->asTexture();
        var[channel.texname] = pTex;
    };
    for (const auto& channel : kOutputChannels) bind(channel);

    mpFinalShading->execute(pRenderContext, mFrameDim.x, mFrameDim.y);
}
