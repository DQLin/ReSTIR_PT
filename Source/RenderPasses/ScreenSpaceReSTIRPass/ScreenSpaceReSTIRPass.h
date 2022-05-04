/***************************************************************************
 # Copyright (c) 2015-21, NVIDIA CORPORATION. All rights reserved.
 #
 # NVIDIA CORPORATION and its licensors retain all intellectual property
 # and proprietary rights in and to this software, related documentation
 # and any modifications thereto.  Any use, reproduction, disclosure or
 # distribution of this software and related documentation without an express
 # license agreement from NVIDIA CORPORATION is strictly prohibited.
 **************************************************************************/
#pragma once
#include "Falcor.h"
#include "RenderGraph/RenderPassHelpers.h"
#include "Experimental/ScreenSpaceReSTIR/ScreenSpaceReSTIR.h"

using namespace Falcor;

/** Direct illumination using screen-space ReSTIR.

    This is similar to the SpatiotemporalReservoirResampling pass but uses the
    ScreenSpaceReSTIR module and serves as an example of how to integrate it.
*/
class ScreenSpaceReSTIRPass : public RenderPass
{
public:
    using SharedPtr = std::shared_ptr<ScreenSpaceReSTIRPass>;

    static SharedPtr create(RenderContext* pRenderContext = nullptr, const Dictionary& dict = {});

    virtual std::string getDesc() override;
    virtual Dictionary getScriptingDictionary() override;
    virtual RenderPassReflection reflect(const CompileData& compileData) override;
    virtual void compile(RenderContext* pRenderContext, const CompileData& compileData) override;
    virtual void execute(RenderContext* pRenderContext, const RenderData& renderData) override;
    virtual void renderUI(Gui::Widgets& widget) override;
    virtual void setScene(RenderContext* pRenderContext, const Scene::SharedPtr& pScene) override;
    virtual bool onMouseEvent(const MouseEvent& mouseEvent) override;
    virtual bool onKeyEvent(const KeyboardEvent& keyEvent) override { return false; }
    virtual void updateDict(const Dictionary& dict) override;

private:
    ScreenSpaceReSTIRPass(const Dictionary& dict);

    void parseDictionary(const Dictionary& dict);

    void prepareSurfaceData(RenderContext* pRenderContext, const Texture::SharedPtr& pVBuffer, int instanceID);
    void finalShading(RenderContext* pRenderContext, const Texture::SharedPtr& pVBuffer, const RenderData& renderData, int instanceID);

    // Internal state
    Scene::SharedPtr mpScene;
    std::vector<ScreenSpaceReSTIR::SharedPtr> mpScreenSpaceReSTIR;
    ScreenSpaceReSTIR::Options::SharedPtr mOptions;
    bool mOptionsChanged = false;
    uint2 mFrameDim = uint2(0);
    bool mGBufferAdjustShadingNormals = false;

    ComputePass::SharedPtr mpPrepareSurfaceData;
    ComputePass::SharedPtr mpFinalShading;

    int mNumReSTIRInstances = 1;
    bool mNeedRecreateReSTIRInstances = false;
};
