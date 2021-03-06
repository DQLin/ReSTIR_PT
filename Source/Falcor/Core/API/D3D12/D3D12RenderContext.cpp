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
#include "Core/API/RenderContext.h"
#include "Core/API/Device.h"
#include "glm/gtc/type_ptr.hpp"
#include "D3D12State.h"
#include "Raytracing/RtProgram/RtProgram.h"
#include "Raytracing/RtProgramVars.h"
#include "RenderGraph/BasePasses/FullScreenPass.h"

namespace Falcor
{
    namespace
    {
        struct RenderContextApiData
        {
            size_t refCount = 0;

            CommandSignatureHandle pDrawCommandSig;
            CommandSignatureHandle pDrawIndexCommandSig;

            struct
            {
                std::shared_ptr<FullScreenPass> pPass;
                Fbo::SharedPtr pFbo;

                Sampler::SharedPtr pLinearSampler;
                Sampler::SharedPtr pPointSampler;
                Sampler::SharedPtr pLinearMinSampler;
                Sampler::SharedPtr pPointMinSampler;
                Sampler::SharedPtr pLinearMaxSampler;
                Sampler::SharedPtr pPointMaxSampler;

                ParameterBlock::SharedPtr pBlitParamsBuffer;
                float2 prevSrcRectOffset = float2(0, 0);
                float2 prevSrcReftScale = float2(0, 0);

                // Variable offsets in constant buffer
                UniformShaderVarOffset offsetVarOffset;
                UniformShaderVarOffset scaleVarOffset;
                ProgramReflection::BindLocation texBindLoc;

                // Parameters for complex blit
                float4 prevComponentsTransform[4] = { float4(0), float4(0), float4(0), float4(0) };
                UniformShaderVarOffset compTransVarOffset[4];

            } blitData;

            static void init();
            static void release();
        };

        RenderContextApiData sApiData;

        void RenderContextApiData::init()
        {
            assert(gpDevice);
            auto& blitData = sApiData.blitData;
            if (blitData.pPass == nullptr)
            {
                // Init the blit data.
                Program::DefineList defines =
                {
                    { "SAMPLE_COUNT", "1" },
                    { "COMPLEX_BLIT", "0" },
                    { "SRC_INT", "0" },
                    { "DST_INT", "0" },
                };
                Program::Desc d;
                d.addShaderLibrary("Core/API/BlitReduction.slang").vsEntry("vs").psEntry("ps");
                blitData.pPass = FullScreenPass::create(d, defines);
                blitData.pFbo = Fbo::create();
                assert(blitData.pPass && blitData.pFbo);

                blitData.pBlitParamsBuffer = blitData.pPass->getVars()->getParameterBlock("BlitParamsCB");
                blitData.offsetVarOffset = blitData.pBlitParamsBuffer->getVariableOffset("gOffset");
                blitData.scaleVarOffset = blitData.pBlitParamsBuffer->getVariableOffset("gScale");
                blitData.prevSrcRectOffset = float2(-1.0f);
                blitData.prevSrcReftScale = float2(-1.0f);

                Sampler::Desc desc;
                desc.setAddressingMode(Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp, Sampler::AddressMode::Clamp);
                desc.setReductionMode(Sampler::ReductionMode::Standard);
                desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Point);
                blitData.pLinearSampler = Sampler::create(desc);
                desc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
                blitData.pPointSampler = Sampler::create(desc);
                // Min reductions.
                desc.setReductionMode(Sampler::ReductionMode::Min);
                desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Point);
                blitData.pLinearMinSampler = Sampler::create(desc);
                desc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
                blitData.pPointMinSampler = Sampler::create(desc);
                // Max reductions.
                desc.setReductionMode(Sampler::ReductionMode::Max);
                desc.setFilterMode(Sampler::Filter::Linear, Sampler::Filter::Linear, Sampler::Filter::Point);
                blitData.pLinearMaxSampler = Sampler::create(desc);
                desc.setFilterMode(Sampler::Filter::Point, Sampler::Filter::Point, Sampler::Filter::Point);
                blitData.pPointMaxSampler = Sampler::create(desc);

                const auto& pDefaultBlockReflection = blitData.pPass->getProgram()->getReflector()->getDefaultParameterBlock();
                blitData.texBindLoc = pDefaultBlockReflection->getResourceBinding("gTex");

                // Init the draw signature
                D3D12_COMMAND_SIGNATURE_DESC sigDesc;
                sigDesc.NumArgumentDescs = 1;
                sigDesc.NodeMask = 0;
                D3D12_INDIRECT_ARGUMENT_DESC argDesc;

                // Draw
                sigDesc.ByteStride = sizeof(D3D12_DRAW_ARGUMENTS);
                argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;
                sigDesc.pArgumentDescs = &argDesc;
                d3d_call(gpDevice->getApiHandle()->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&sApiData.pDrawCommandSig)));

                // Draw index
                sigDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
                argDesc.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
                sigDesc.pArgumentDescs = &argDesc;
                d3d_call(gpDevice->getApiHandle()->CreateCommandSignature(&sigDesc, nullptr, IID_PPV_ARGS(&sApiData.pDrawIndexCommandSig)));

                // Complex blit parameters

                blitData.compTransVarOffset[0] = blitData.pBlitParamsBuffer->getVariableOffset("gCompTransformR");
                blitData.compTransVarOffset[1] = blitData.pBlitParamsBuffer->getVariableOffset("gCompTransformG");
                blitData.compTransVarOffset[2] = blitData.pBlitParamsBuffer->getVariableOffset("gCompTransformB");
                blitData.compTransVarOffset[3] = blitData.pBlitParamsBuffer->getVariableOffset("gCompTransformA");
                blitData.prevComponentsTransform[0] = float4(1.0f, 0.0f, 0.0f, 0.0f);
                blitData.prevComponentsTransform[1] = float4(0.0f, 1.0f, 0.0f, 0.0f);
                blitData.prevComponentsTransform[2] = float4(0.0f, 0.0f, 1.0f, 0.0f);
                blitData.prevComponentsTransform[3] = float4(0.0f, 0.0f, 0.0f, 1.0f);
                for (uint32_t i = 0; i < 4; i++) blitData.pBlitParamsBuffer->setVariable(blitData.compTransVarOffset[i], blitData.prevComponentsTransform[i]);
            }

            sApiData.refCount++;
        }

        void RenderContextApiData::release()
        {
            sApiData.refCount--;
            if (sApiData.refCount == 0) sApiData = {};
        }
    }

    RenderContext::RenderContext(CommandQueueHandle queue)
        : ComputeContext(LowLevelContextData::CommandQueueType::Direct, queue)
    {
        RenderContextApiData::init();
    }

    RenderContext::~RenderContext()
    {
        RenderContextApiData::release();
    }

    void RenderContext::clearRtv(const RenderTargetView* pRtv, const float4& color)
    {
        resourceBarrier(pRtv->getResource().get(), Resource::State::RenderTarget);
        mpLowLevelData->getCommandList()->ClearRenderTargetView(pRtv->getApiHandle()->getCpuHandle(0), glm::value_ptr(color), 0, nullptr);
        mCommandsPending = true;
    }

    void RenderContext::clearDsv(const DepthStencilView* pDsv, float depth, uint8_t stencil, bool clearDepth, bool clearStencil)
    {
        uint32_t flags = clearDepth ? D3D12_CLEAR_FLAG_DEPTH : 0;
        flags |= clearStencil ? D3D12_CLEAR_FLAG_STENCIL : 0;

        resourceBarrier(pDsv->getResource().get(), Resource::State::DepthStencil);
        mpLowLevelData->getCommandList()->ClearDepthStencilView(pDsv->getApiHandle()->getCpuHandle(0), D3D12_CLEAR_FLAGS(flags), depth, stencil, 0, nullptr);
        mCommandsPending = true;
    }

    static void D3D12SetVao(RenderContext* pCtx, ID3D12GraphicsCommandList* pList, const Vao* pVao)
    {
        D3D12_VERTEX_BUFFER_VIEW vb[D3D12_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT] = {};
        D3D12_INDEX_BUFFER_VIEW ib = {};

        if (pVao)
        {
            // Get the vertex buffers
            for (uint32_t i = 0; i < pVao->getVertexBuffersCount(); i++)
            {
                const Buffer* pVB = pVao->getVertexBuffer(i).get();
                if (pVB)
                {
                    vb[i].BufferLocation = pVB->getGpuAddress();
                    vb[i].SizeInBytes = (uint32_t)pVB->getSize();
                    vb[i].StrideInBytes = pVao->getVertexLayout()->getBufferLayout(i)->getStride();
                    pCtx->resourceBarrier(pVB, Resource::State::VertexBuffer);
                }
            }

            const Buffer* pIB = pVao->getIndexBuffer().get();
            if (pIB)
            {
                ib.BufferLocation = pIB->getGpuAddress();
                ib.SizeInBytes = (uint32_t)pIB->getSize();
                ib.Format = getDxgiFormat(pVao->getIndexBufferFormat());
                pCtx->resourceBarrier(pIB, Resource::State::IndexBuffer);
            }
        }

        pList->IASetVertexBuffers(0, arraysize(vb), vb);
        pList->IASetIndexBuffer(&ib);
    }

    static void D3D12SetFbo(RenderContext* pCtx, const Fbo* pFbo)
    {
        // We are setting the entire RTV array to make sure everything that was previously bound is detached.
        // We're using 2D null views for any unused slots.
        uint32_t colorTargets = Fbo::getMaxColorTargetCount();
        auto pNullRtv = RenderTargetView::getNullView(RenderTargetView::Dimension::Texture2D);
        std::vector<HeapCpuHandle> pRTV(colorTargets, pNullRtv->getApiHandle()->getCpuHandle(0));
        HeapCpuHandle pDSV = DepthStencilView::getNullView(DepthStencilView::Dimension::Texture2D)->getApiHandle()->getCpuHandle(0);

        if (pFbo)
        {
            for (uint32_t i = 0; i < colorTargets; i++)
            {
                auto pTexture = pFbo->getColorTexture(i);
                if (pTexture)
                {
                    pRTV[i] = pFbo->getRenderTargetView(i)->getApiHandle()->getCpuHandle(0);
                    pCtx->resourceBarrier(pTexture.get(), Resource::State::RenderTarget);
                }
            }

            auto& pTexture = pFbo->getDepthStencilTexture();
            if(pTexture)
            {
                pDSV = pFbo->getDepthStencilView()->getApiHandle()->getCpuHandle(0);
                if (pTexture)
                {
                    pCtx->resourceBarrier(pTexture.get(), Resource::State::DepthStencil);
                }
            }
        }
        ID3D12GraphicsCommandList* pCmdList = pCtx->getLowLevelData()->getCommandList().GetInterfacePtr();
        pCmdList->OMSetRenderTargets(colorTargets, pRTV.data(), FALSE, &pDSV);
    }

    static void D3D12SetSamplePositions(ID3D12GraphicsCommandList* pList, const Fbo* pFbo)
    {
        if (!pFbo) return;
        ID3D12GraphicsCommandList1* pList1;
        pList->QueryInterface(IID_PPV_ARGS(&pList1));

        bool featureSupported = gpDevice->isFeatureSupported(Device::SupportedFeatures::ProgrammableSamplePositionsPartialOnly) ||
                                gpDevice->isFeatureSupported(Device::SupportedFeatures::ProgrammableSamplePositionsFull);

        const auto& samplePos = pFbo->getSamplePositions();

#if _LOG_ENABLED
        if (featureSupported == false && samplePos.size() > 0)
        {
            logError("The FBO specifies programmable sample positions, but the hardware does not support it");
        }
        else if (gpDevice->isFeatureSupported(Device::SupportedFeatures::ProgrammableSamplePositionsPartialOnly) && samplePos.size() > 1)
        {
            logError("The FBO specifies multiple programmable sample positions, but the hardware only supports one");
        }
#endif
        if(featureSupported)
        {
            static_assert(offsetof(Fbo::SamplePosition, xOffset) == offsetof(D3D12_SAMPLE_POSITION, X), "SamplePosition.X");
            static_assert(offsetof(Fbo::SamplePosition, yOffset) == offsetof(D3D12_SAMPLE_POSITION, Y), "SamplePosition.Y");

            if (samplePos.size())
            {
                pList1->SetSamplePositions(pFbo->getSampleCount(), pFbo->getSamplePositionsPixelCount(), (D3D12_SAMPLE_POSITION*)samplePos.data());
            }
            else
            {
                pList1->SetSamplePositions(0, 0, nullptr);
            }
        }
    }

    static void D3D12SetViewports(ID3D12GraphicsCommandList* pList, const GraphicsState::Viewport* vp)
    {
        static_assert(offsetof(GraphicsState::Viewport, originX) == offsetof(D3D12_VIEWPORT, TopLeftX), "VP originX offset");
        static_assert(offsetof(GraphicsState::Viewport, originY) == offsetof(D3D12_VIEWPORT, TopLeftY), "VP originY offset");
        static_assert(offsetof(GraphicsState::Viewport, width) == offsetof(D3D12_VIEWPORT, Width), "VP Width offset");
        static_assert(offsetof(GraphicsState::Viewport, height) == offsetof(D3D12_VIEWPORT, Height), "VP Height offset");
        static_assert(offsetof(GraphicsState::Viewport, minDepth) == offsetof(D3D12_VIEWPORT, MinDepth), "VP MinDepth offset");
        static_assert(offsetof(GraphicsState::Viewport, maxDepth) == offsetof(D3D12_VIEWPORT, MaxDepth), "VP TopLeftX offset");

        pList->RSSetViewports(D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, (D3D12_VIEWPORT*)vp);
    }

    static void D3D12SetScissors(ID3D12GraphicsCommandList* pList, const GraphicsState::Scissor* sc)
    {
        static_assert(offsetof(GraphicsState::Scissor, left) == offsetof(D3D12_RECT, left), "Scissor.left offset");
        static_assert(offsetof(GraphicsState::Scissor, top) == offsetof(D3D12_RECT, top), "Scissor.top offset");
        static_assert(offsetof(GraphicsState::Scissor, right) == offsetof(D3D12_RECT, right), "Scissor.right offset");
        static_assert(offsetof(GraphicsState::Scissor, bottom) == offsetof(D3D12_RECT, bottom), "Scissor.bottom offset");

        pList->RSSetScissorRects(D3D12_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE, (D3D12_RECT*)sc);
    }

    bool RenderContext::prepareForDraw(GraphicsState* pState, GraphicsVars* pVars)
    {
        assert(pState);
        // Vao must be valid so at least primitive topology is known
        assert(pState->getVao().get());

        auto pGSO = pState->getGSO(pVars);

        if (is_set(StateBindFlags::Vars, mBindFlags))
        {
            // Apply the vars. Must be first because applyGraphicsVars() might cause a flush
            if (pVars)
            {
                // TODO(tfoley): Need to find a way to pass the specialization information
                // from computing the GSO down into `applyGraphicsVars` so that parameters
                // can be bound using an appropriate layout.
                //
                if (applyGraphicsVars(pVars, pGSO->getDesc().getRootSignature().get()) == false) return false;
            }
            else mpLowLevelData->getCommandList()->SetGraphicsRootSignature(RootSignature::getEmpty()->getApiHandle());
            mpLastBoundGraphicsVars = pVars;
        }

        ID3D12GraphicsCommandList* pList = mpLowLevelData->getCommandList();


        if (is_set(StateBindFlags::Topology, mBindFlags))           pList->IASetPrimitiveTopology(getD3DPrimitiveTopology(pState->getVao()->getPrimitiveTopology()));
        if (is_set(StateBindFlags::Vao, mBindFlags))                D3D12SetVao(this, pList, pState->getVao().get());
        if (is_set(StateBindFlags::Fbo, mBindFlags))                D3D12SetFbo(this, pState->getFbo().get());
        if (is_set(StateBindFlags::SamplePositions, mBindFlags))    D3D12SetSamplePositions(pList, pState->getFbo().get());
        if (is_set(StateBindFlags::Viewports, mBindFlags))          D3D12SetViewports(pList, &pState->getViewport(0));
        if (is_set(StateBindFlags::Scissors, mBindFlags))           D3D12SetScissors(pList, &pState->getScissors(0));
        if (is_set(StateBindFlags::PipelineState, mBindFlags))      pList->SetPipelineState(pGSO->getApiHandle());

        BlendState::SharedPtr blendState = pState->getBlendState();
        if (blendState != nullptr)  pList->OMSetBlendFactor(glm::value_ptr(blendState->getBlendFactor()));

        const auto pDsState = pState->getDepthStencilState();
        pList->OMSetStencilRef(pDsState == nullptr ? 0 : pDsState->getStencilRef());

        mCommandsPending = true;
        return true;
    }

    void RenderContext::drawInstanced(GraphicsState* pState, GraphicsVars* pVars, uint32_t vertexCount, uint32_t instanceCount, uint32_t startVertexLocation, uint32_t startInstanceLocation)
    {
        if (prepareForDraw(pState, pVars) == false) return;
        mpLowLevelData->getCommandList()->DrawInstanced(vertexCount, instanceCount, startVertexLocation, startInstanceLocation);
    }

    void RenderContext::draw(GraphicsState* pState, GraphicsVars* pVars, uint32_t vertexCount, uint32_t startVertexLocation)
    {
        drawInstanced(pState,pVars, vertexCount, 1, startVertexLocation, 0);
    }

    void RenderContext::drawIndexedInstanced(GraphicsState* pState, GraphicsVars* pVars, uint32_t indexCount, uint32_t instanceCount, uint32_t startIndexLocation, int32_t baseVertexLocation, uint32_t startInstanceLocation)
    {
        if (prepareForDraw(pState, pVars) == false) return;
        mpLowLevelData->getCommandList()->DrawIndexedInstanced(indexCount, instanceCount, startIndexLocation, baseVertexLocation, startInstanceLocation);
    }

    void RenderContext::drawIndexed(GraphicsState* pState, GraphicsVars* pVars, uint32_t indexCount, uint32_t startIndexLocation, int32_t baseVertexLocation)
    {
        drawIndexedInstanced(pState, pVars, indexCount, 1, startIndexLocation, baseVertexLocation, 0);
    }

    void drawIndirectCommon(RenderContext* pContext, const CommandListHandle& pCommandList, ID3D12CommandSignature* pCommandSig, uint32_t maxCommandCount, const Buffer* pArgBuffer, uint64_t argBufferOffset, const Buffer* pCountBuffer, uint64_t countBufferOffset)
    {
        pContext->resourceBarrier(pArgBuffer, Resource::State::IndirectArg);
        if (pCountBuffer != nullptr && pCountBuffer != pArgBuffer) pContext->resourceBarrier(pCountBuffer, Resource::State::IndirectArg);
        pCommandList->ExecuteIndirect(pCommandSig, maxCommandCount, pArgBuffer->getApiHandle(), argBufferOffset, (pCountBuffer != nullptr ? pCountBuffer->getApiHandle() : nullptr), countBufferOffset);
    }

    void RenderContext::drawIndirect(GraphicsState* pState, GraphicsVars* pVars, uint32_t maxCommandCount, const Buffer* pArgBuffer, uint64_t argBufferOffset, const Buffer* pCountBuffer, uint64_t countBufferOffset)
    {
        if (prepareForDraw(pState, pVars) == false) return;
        drawIndirectCommon(this, mpLowLevelData->getCommandList(), sApiData.pDrawCommandSig, maxCommandCount, pArgBuffer, argBufferOffset, pCountBuffer, countBufferOffset);
    }

    void RenderContext::drawIndexedIndirect(GraphicsState* pState, GraphicsVars* pVars, uint32_t maxCommandCount, const Buffer* pArgBuffer, uint64_t argBufferOffset, const Buffer* pCountBuffer, uint64_t countBufferOffset)
    {
        if (prepareForDraw(pState, pVars) == false) return;
        drawIndirectCommon(this, mpLowLevelData->getCommandList(), sApiData.pDrawIndexCommandSig, maxCommandCount, pArgBuffer, argBufferOffset, pCountBuffer, countBufferOffset);
    }

    void RenderContext::raytrace(RtProgram* pProgram, RtProgramVars* pVars, uint32_t width, uint32_t height, uint32_t depth)
    {
        auto pRtso = pProgram->getRtso(pVars);

        pVars->apply(this, pRtso.get());

        const auto& pShaderTable = pVars->getShaderTable();
        resourceBarrier(pShaderTable->getBuffer().get(), Resource::State::NonPixelShader);

        D3D12_GPU_VIRTUAL_ADDRESS startAddress = pShaderTable->getBuffer()->getGpuAddress();

        D3D12_DISPATCH_RAYS_DESC raytraceDesc = {};
        raytraceDesc.Width = width;
        raytraceDesc.Height = height;
        raytraceDesc.Depth = depth;

        // RayGen data
        //
        // TODO: We could easily support specifying the ray-gen program to invoke by an index in
        // the call to `raytrace()`.
        //
        raytraceDesc.RayGenerationShaderRecord.StartAddress = startAddress + pShaderTable->getRayGenTableOffset();
        raytraceDesc.RayGenerationShaderRecord.SizeInBytes = pShaderTable->getRayGenRecordSize();;

        // Miss data
        if (pShaderTable->getMissRecordCount() > 0)
        {
            raytraceDesc.MissShaderTable.StartAddress = startAddress + pShaderTable->getMissTableOffset();
            raytraceDesc.MissShaderTable.StrideInBytes = pShaderTable->getMissRecordSize();
            raytraceDesc.MissShaderTable.SizeInBytes = pShaderTable->getMissRecordSize() * pShaderTable->getMissRecordCount();
        }

        // Hit data
        if (pShaderTable->getHitRecordCount() > 0)
        {
            raytraceDesc.HitGroupTable.StartAddress = startAddress + pShaderTable->getHitTableOffset();
            raytraceDesc.HitGroupTable.StrideInBytes = pShaderTable->getHitRecordSize();
            raytraceDesc.HitGroupTable.SizeInBytes = pShaderTable->getHitRecordSize() * pShaderTable->getHitRecordCount();
        }

        auto pCmdList = getLowLevelData()->getCommandList();
        pCmdList->SetComputeRootSignature(pRtso->getGlobalRootSignature()->getApiHandle().GetInterfacePtr());

        // Dispatch
        GET_COM_INTERFACE(pCmdList, ID3D12GraphicsCommandList4, pList4);
        pList4->SetPipelineState1(pRtso->getApiHandle().GetInterfacePtr());
        pList4->DispatchRays(&raytraceDesc);
    }

    void RenderContext::blit(const ShaderResourceView::SharedPtr& pSrc, const RenderTargetView::SharedPtr& pDst, uint4 srcRect, uint4 dstRect, Sampler::Filter filter)
    {
        const Sampler::ReductionMode componentsReduction[] = { Sampler::ReductionMode::Standard, Sampler::ReductionMode::Standard, Sampler::ReductionMode::Standard, Sampler::ReductionMode::Standard };
        const float4 componentsTransform[] = { float4(1.0f, 0.0f, 0.0f, 0.0f), float4(0.0f, 1.0f, 0.0f, 0.0f), float4(0.0f, 0.0f, 1.0f, 0.0f), float4(0.0f, 0.0f, 0.0f, 1.0f) };

        blit(pSrc, pDst, srcRect, dstRect, filter, componentsReduction, componentsTransform);
    }

    void RenderContext::blit(const ShaderResourceView::SharedPtr& pSrc, const RenderTargetView::SharedPtr& pDst, uint4 srcRect, uint4 dstRect, Sampler::Filter filter, const Sampler::ReductionMode componentsReduction[4], const float4 componentsTransform[4])
    {
        auto& blitData = sApiData.blitData;

        // Fetch textures from views.
        assert(pSrc && pDst);
        auto pSrcResource = pSrc->getResource();
        auto pDstResource = pDst->getResource();
        if (pSrcResource->getType() == Resource::Type::Buffer || pDstResource->getType() == Resource::Type::Buffer)
        {
            throw std::runtime_error("RenderContext::blit does not support buffers");
        }

        const Texture* pSrcTexture = dynamic_cast<const Texture*>(pSrcResource.get());
        const Texture* pDstTexture = dynamic_cast<const Texture*>(pDstResource.get());
        assert(pSrcTexture != nullptr && pDstTexture != nullptr);

        // Clamp rectangles to the dimensions of the source/dest views.
        const uint32_t srcMipLevel = pSrc->getViewInfo().mostDetailedMip;
        const uint32_t dstMipLevel = pDst->getViewInfo().mostDetailedMip;
        const uint2 srcSize(pSrcTexture->getWidth(srcMipLevel), pSrcTexture->getHeight(srcMipLevel));
        const uint2 dstSize(pDstTexture->getWidth(dstMipLevel), pDstTexture->getHeight(dstMipLevel));

        srcRect.z = std::min(srcRect.z, srcSize.x);
        srcRect.w = std::min(srcRect.w, srcSize.y);
        dstRect.z = std::min(dstRect.z, dstSize.x);
        dstRect.w = std::min(dstRect.w, dstSize.y);

        if (srcRect.x >= srcRect.z || srcRect.y >= srcRect.w ||
            dstRect.x >= dstRect.z || dstRect.y >= dstRect.w)
        {
            logDebug("RenderContext::blit() called with out-of-bounds src/dst rectangle");
            return; // No blit necessary
        }

        // Determine the type of blit.
        const uint32_t sampleCount = pSrcTexture->getSampleCount();
        const bool complexBlit =
            !(  (componentsReduction[0] == Sampler::ReductionMode::Standard) && (componentsReduction[1] == Sampler::ReductionMode::Standard) && (componentsReduction[2] == Sampler::ReductionMode::Standard) && (componentsReduction[3] == Sampler::ReductionMode::Standard) &&
                (componentsTransform[0] == float4(1.0f, 0.0f, 0.0f, 0.0f)) && (componentsTransform[1] == float4(0.0f, 1.0f, 0.0f, 0.0f)) && (componentsTransform[2] == float4(0.0f, 0.0f, 1.0f, 0.0f)) && (componentsTransform[3] == float4(0.0f, 0.0f, 0.0f, 1.0f)) );

        auto isFullView = [](const auto& view, const Texture* tex) {
            const auto& info = view->getViewInfo();
            return info.mostDetailedMip == 0 && info.firstArraySlice == 0 && info.mipCount == tex->getMipCount() && info.arraySize == tex->getArraySize();
        };
        const bool srcFullRect = srcRect.x == 0 && srcRect.y == 0 && srcRect.z == srcSize.x && srcRect.w == srcSize.y;
        const bool dstFullRect = dstRect.x == 0 && dstRect.y == 0 && dstRect.z == dstSize.x && dstRect.w == dstSize.y;

        const bool fullCopy =
            !complexBlit &&
            isFullView(pSrc, pSrcTexture) && srcFullRect &&
            isFullView(pDst, pDstTexture) && dstFullRect &&
            pSrcTexture->compareDesc(pDstTexture);

        // Take fast path to copy the entire resource if possible. This has many requirements;
        // the source/dest must have identical size/format/etc. and the views and rects must cover the full resources.
        if (fullCopy)
        {
            copyResource(pDstResource.get(), pSrcResource.get());
            return;
        }

        // At this point, we have to run a shader to perform the blit.
        // The implementation has some limitations. Check that all requirements are fullfilled.

        // Complex blit doesn't work with multi-sampled textures.
        if (complexBlit && sampleCount > 1) throw std::runtime_error("RenderContext::blit() does not support sample count > 1 for complex blit");

        // Validate source format. Only single-sampled basic blit handles integer source format.
        // All variants support casting to integer destination format.
        if (isIntegerFormat(pSrcTexture->getFormat()))
        {
            if (sampleCount > 1) throw std::runtime_error("RenderContext::blit() requires non-integer source format for multi-sampled textures");
            else if (complexBlit) throw std::runtime_error("RenderContext::blit() requires non-integer source format for complex blit");
        }

        // Blit does not support texture arrays or mip maps.
        if (!(pSrc->getViewInfo().arraySize == 1 && pSrc->getViewInfo().mipCount == 1) ||
            !(pDst->getViewInfo().arraySize == 1 && pDst->getViewInfo().mipCount == 1))
        {
            throw std::runtime_error("RenderContext::blit() does not support texture arrays or mip maps");
        }

        // Configure program.
        blitData.pPass->addDefine("SAMPLE_COUNT", std::to_string(sampleCount));
        blitData.pPass->addDefine("COMPLEX_BLIT", complexBlit ? "1" : "0");
        blitData.pPass->addDefine("SRC_INT", isIntegerFormat(pSrcTexture->getFormat()) ? "1" : "0");
        blitData.pPass->addDefine("DST_INT", isIntegerFormat(pDstTexture->getFormat()) ? "1" : "0");

        if (complexBlit)
        {
            assert(sampleCount <= 1);

            Sampler::SharedPtr usedSampler[4];
            for (uint32_t i = 0; i < 4; i++)
            {
                assert(componentsReduction[i] != Sampler::ReductionMode::Comparison);        // Comparison mode not supported.

                if (componentsReduction[i] == Sampler::ReductionMode::Min) usedSampler[i] = (filter == Sampler::Filter::Linear) ? blitData.pLinearMinSampler : blitData.pPointMinSampler;
                else if (componentsReduction[i] == Sampler::ReductionMode::Max) usedSampler[i] = (filter == Sampler::Filter::Linear) ? blitData.pLinearMaxSampler : blitData.pPointMaxSampler;
                else usedSampler[i] = (filter == Sampler::Filter::Linear) ? blitData.pLinearSampler : blitData.pPointSampler;
            }

            blitData.pPass->getVars()->setSampler("gSamplerR", usedSampler[0]);
            blitData.pPass->getVars()->setSampler("gSamplerG", usedSampler[1]);
            blitData.pPass->getVars()->setSampler("gSamplerB", usedSampler[2]);
            blitData.pPass->getVars()->setSampler("gSamplerA", usedSampler[3]);

            // Parameters for complex blit
            for (uint32_t i = 0; i < 4; i++)
            {
                if (blitData.prevComponentsTransform[i] != componentsTransform[i])
                {
                    blitData.pBlitParamsBuffer->setVariable(blitData.compTransVarOffset[i], componentsTransform[i]);
                    blitData.prevComponentsTransform[i] = componentsTransform[i];
                }
            }
        }
        else
        {
            blitData.pPass->getVars()->setSampler("gSampler", (filter == Sampler::Filter::Linear) ? blitData.pLinearSampler : blitData.pPointSampler);
        }

        float2 srcRectOffset(0.0f);
        float2 srcRectScale(1.0f);
        if (!srcFullRect)
        {
            srcRectOffset = float2(srcRect.x, srcRect.y) / float2(srcSize);
            srcRectScale = float2(srcRect.z - srcRect.x, srcRect.w - srcRect.y) / float2(srcSize);
        }

        GraphicsState::Viewport dstViewport(0.0f, 0.0f, (float)dstSize.x, (float)dstSize.y, 0.0f, 1.0f);
        if (!dstFullRect)
        {
            dstViewport = GraphicsState::Viewport((float)dstRect.x, (float)dstRect.y, (float)(dstRect.z - dstRect.x), (float)(dstRect.w - dstRect.y), 0.0f, 1.0f);
        }

        // Update buffer/state
        if (srcRectOffset != blitData.prevSrcRectOffset)
        {
            blitData.pBlitParamsBuffer->setVariable(blitData.offsetVarOffset, srcRectOffset);
            blitData.prevSrcRectOffset = srcRectOffset;
        }

        if (srcRectScale != blitData.prevSrcReftScale)
        {
            blitData.pBlitParamsBuffer->setVariable(blitData.scaleVarOffset, srcRectScale);
            blitData.prevSrcReftScale = srcRectScale;
        }

        Texture::SharedPtr pSharedTex = std::static_pointer_cast<Texture>(pDstResource);
        blitData.pFbo->attachColorTarget(pSharedTex, 0, pDst->getViewInfo().mostDetailedMip, pDst->getViewInfo().firstArraySlice, pDst->getViewInfo().arraySize);
        blitData.pPass->getVars()->setSrv(blitData.texBindLoc, pSrc);
        blitData.pPass->getState()->setViewport(0, dstViewport);
        blitData.pPass->execute(this, blitData.pFbo, false);

        // Release the resources we bound
        blitData.pPass->getVars()->setSrv(blitData.texBindLoc, nullptr);
    }

    void RenderContext::resolveSubresource(const Texture::SharedPtr& pSrc, uint32_t srcSubresource, const Texture::SharedPtr& pDst, uint32_t dstSubresource)
    {
        DXGI_FORMAT format = getDxgiFormat(pDst->getFormat());
        mpLowLevelData->getCommandList()->ResolveSubresource(pDst->getApiHandle(), dstSubresource, pSrc->getApiHandle(), srcSubresource, format);
        mCommandsPending = true;
    }

    void RenderContext::resolveResource(const Texture::SharedPtr& pSrc, const Texture::SharedPtr& pDst)
    {
        bool match = true;
        match = match && (pSrc->getMipCount() == pDst->getMipCount());
        match = match && (pSrc->getArraySize() == pDst->getArraySize());
        if (!match)
        {
            logWarning("Can't resolve a resource. The src and dst textures have a different array-size or mip-count");
        }

        resourceBarrier(pSrc.get(), Resource::State::ResolveSource);
        resourceBarrier(pDst.get(), Resource::State::ResolveDest);

        uint32_t subresourceCount = pSrc->getMipCount() * pSrc->getArraySize();
        for (uint32_t s = 0; s < subresourceCount; s++)
        {
            resolveSubresource(pSrc, s, pDst, s);
        }
    }
}
