/*
 * Copyright (c) Contributors to the Open 3D Engine Project.
 * For complete copyright and license terms please see the LICENSE at the root of this distribution.
 *
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 *
 */

#include <Atom/RPI.Public/Pass/FullscreenTrianglePass.h>
#include <Atom/RPI.Public/Pass/PassUtils.h>
#include <Atom/RPI.Public/RPIUtils.h>
#include <Atom/RPI.Public/Shader/ShaderReloadDebugTracker.h>

#include <Atom/RPI.Reflect/Pass/FullscreenTrianglePassData.h>
#include <Atom/RPI.Reflect/Pass/PassTemplate.h>
#include <Atom/RPI.Reflect/Shader/ShaderAsset.h>

#include <Atom/RHI/Factory.h>
#include <Atom/RHI/FrameScheduler.h>
#include <Atom/RHI/PipelineState.h>

#include <AzCore/Asset/AssetCommon.h>
#include <AzCore/Asset/AssetManagerBus.h>
#include <AzCore/std/algorithm.h>


namespace AZ
{
    namespace RPI
    {
        Ptr<FullscreenTrianglePass> FullscreenTrianglePass::Create(const PassDescriptor& descriptor)
        {
            Ptr<FullscreenTrianglePass> pass = aznew FullscreenTrianglePass(descriptor);
            return pass;
        }

        FullscreenTrianglePass::FullscreenTrianglePass(const PassDescriptor& descriptor)
            : RenderPass(descriptor)
            , m_passDescriptor(descriptor)
        {
            LoadShader();
        }

        FullscreenTrianglePass::~FullscreenTrianglePass()
        {
            ShaderReloadNotificationBus::Handler::BusDisconnect();
        }

        Data::Instance<Shader> FullscreenTrianglePass::GetShader() const
        {
            return m_shader;
        }

        void FullscreenTrianglePass::OnShaderReinitialized(const Shader&)
        {
            UpdateSrgs();
        }

        void FullscreenTrianglePass::OnShaderAssetReinitialized(const Data::Asset<ShaderAsset>&)
        {
            UpdateSrgs();
        }

        void FullscreenTrianglePass::LoadShader()
        {
            AZ_Assert(GetPassState() != PassState::Rendering, "FullscreenTrianglePass - Reloading shader during Rendering phase!");

            // Load FullscreenTrianglePassData
            const FullscreenTrianglePassData* passData = PassUtils::GetPassData<FullscreenTrianglePassData>(m_passDescriptor);
            if (passData == nullptr)
            {
                AZ_Error("PassSystem", false, "[FullscreenTrianglePass '%s']: Trying to construct without valid FullscreenTrianglePassData!",
                    GetPathName().GetCStr());
                return;
            }

            // Load Shader
            Data::Asset<ShaderAsset> shaderAsset;
            if (passData->m_shaderAsset.m_assetId.IsValid())
            {
                shaderAsset = RPI::FindShaderAsset(passData->m_shaderAsset.m_assetId, passData->m_shaderAsset.m_filePath);
            }

            if (!shaderAsset.GetId().IsValid())
            {
                AZ_Error("PassSystem", false, "[FullscreenTrianglePass '%s']: Failed to load shader '%s'!",
                    GetPathName().GetCStr(),
                    passData->m_shaderAsset.m_filePath.data());
                return;
            }

            m_shader = Shader::FindOrCreate(shaderAsset);
            if (m_shader == nullptr)
            {
                AZ_Error("PassSystem", false, "[FullscreenTrianglePass '%s']: Failed to load shader '%s'!",
                    GetPathName().GetCStr(),
                    passData->m_shaderAsset.m_filePath.data());
                return;
            }

            // Store stencil reference value for the draw call
            m_stencilRef = passData->m_stencilRef;

            m_pipelineStateForDraw.Init(m_shader);

            UpdateSrgs();

            QueueForInitialization();

            ShaderReloadNotificationBus::Handler::BusDisconnect();
            ShaderReloadNotificationBus::Handler::BusConnect(shaderAsset.GetId());
        }

        void FullscreenTrianglePass::UpdateSrgs()
        {
            if (!m_shader)
            {
                return;
            }

            // Load Pass SRG
            const auto passSrgLayout = m_shader->FindShaderResourceGroupLayout(SrgBindingSlot::Pass);
            if (passSrgLayout)
            {
                m_shaderResourceGroup = ShaderResourceGroup::Create(m_shader->GetAsset(), m_shader->GetSupervariantIndex(), passSrgLayout->GetName());

                [[maybe_unused]] const FullscreenTrianglePassData* passData = PassUtils::GetPassData<FullscreenTrianglePassData>(m_passDescriptor);

                AZ_Assert(m_shaderResourceGroup, "[FullscreenTrianglePass '%s']: Failed to create SRG from shader asset '%s'",
                    GetPathName().GetCStr(),
                    passData->m_shaderAsset.m_filePath.data());

                PassUtils::BindDataMappingsToSrg(m_passDescriptor, m_shaderResourceGroup.get());
            }

            // Load Draw SRG
            // this is necessary since the shader may have options, which require a default draw SRG
            const bool compileDrawSrg = false; // The SRG will be compiled in CompileResources()
            m_drawShaderResourceGroup = m_shader->CreateDefaultDrawSrg(compileDrawSrg);

            m_pipelineStateForDraw.UpdateSrgVariantFallback(m_shaderResourceGroup);
        }

        void FullscreenTrianglePass::BuildDrawItem()
        {
            m_pipelineStateForDraw.SetOutputFromPass(this);

            // No streams required
            RHI::InputStreamLayout inputStreamLayout;
            inputStreamLayout.SetTopology(RHI::PrimitiveTopology::TriangleList);
            inputStreamLayout.Finalize();

            m_pipelineStateForDraw.SetInputStreamLayout(inputStreamLayout);

            // This draw item purposefully does not reference any geometry buffers.
            // Instead it's expected that the extended class uses a vertex shader 
            // that generates a full-screen triangle completely from vertex ids.
            RHI::DrawLinear draw = RHI::DrawLinear();
            draw.m_vertexCount = 3;

            m_item.m_arguments = RHI::DrawArguments(draw);
            m_item.m_pipelineState = m_pipelineStateForDraw.Finalize();
            m_item.m_stencilRef = static_cast<uint8_t>(m_stencilRef);
        }

        void FullscreenTrianglePass::UpdateShaderOptions(const ShaderOptionList& shaderOptions)
        {
            if (m_shader)
            {
                m_pipelineStateForDraw.Init(m_shader, &shaderOptions);
                m_pipelineStateForDraw.UpdateSrgVariantFallback(m_shaderResourceGroup);
                BuildDrawItem();
            }
        }

        void FullscreenTrianglePass::InitializeInternal()
        {
            RenderPass::InitializeInternal();
            
            ShaderReloadDebugTracker::ScopedSection reloadSection("{%p}->FullscreenTrianglePass::InitializeInternal", this);

            if (m_shader == nullptr)
            {
                AZ_Error("PassSystem", false, "[FullscreenTrianglePass]: Shader not loaded!");
                return;
            }

            BuildDrawItem();
        }

        void FullscreenTrianglePass::FrameBeginInternal(FramePrepareParams params)
        {
            const PassAttachment* outputAttachment = nullptr;
            
            if (GetOutputCount() > 0)
            {
                outputAttachment = GetOutputBinding(0).GetAttachment().get();
            }
            else if(GetInputOutputCount() > 0)
            {
                outputAttachment = GetInputOutputBinding(0).GetAttachment().get();
            }

            AZ_Assert(outputAttachment != nullptr, "[FullscreenTrianglePass %s] has no valid output or input/output attachments.", GetPathName().GetCStr());

            AZ_Assert(outputAttachment->GetAttachmentType() == RHI::AttachmentType::Image,
                "[FullscreenTrianglePass %s] output of FullScreenTrianglePass must be an image", GetPathName().GetCStr());

            RHI::Size targetImageSize = outputAttachment->m_descriptor.m_image.m_size;

            
            m_viewportState.m_maxX = static_cast<float>(AZStd::min(static_cast<uint32_t>(params.m_viewportState.m_maxX), targetImageSize.m_width));
            m_viewportState.m_maxY = static_cast<float>(AZStd::min(static_cast<uint32_t>(params.m_viewportState.m_maxY), targetImageSize.m_height));
            m_viewportState.m_minX = static_cast<float>(AZStd::min(params.m_viewportState.m_minX, m_viewportState.m_maxX));
            m_viewportState.m_minY = static_cast<float>(AZStd::min(params.m_viewportState.m_minY, m_viewportState.m_maxY));

            m_scissorState.m_maxX = AZStd::min(static_cast<uint32_t>(params.m_scissorState.m_maxX), targetImageSize.m_width);
            m_scissorState.m_maxY = AZStd::min(static_cast<uint32_t>(params.m_scissorState.m_maxY), targetImageSize.m_height);
            m_scissorState.m_minX = AZStd::min(params.m_scissorState.m_minX, m_scissorState.m_maxX);
            m_scissorState.m_minY = AZStd::min(params.m_scissorState.m_minY, m_scissorState.m_maxY);

            RenderPass::FrameBeginInternal(params);
        }

        // Scope producer functions

        void FullscreenTrianglePass::SetupFrameGraphDependencies(RHI::FrameGraphInterface frameGraph)
        {
            RenderPass::SetupFrameGraphDependencies(frameGraph);
            frameGraph.SetEstimatedItemCount(1);
        }

        void FullscreenTrianglePass::CompileResources(const RHI::FrameGraphCompileContext& context)
        {
            if (m_shaderResourceGroup != nullptr)
            {
                BindPassSrg(context, m_shaderResourceGroup);
                m_shaderResourceGroup->Compile();
            }

            if (m_drawShaderResourceGroup != nullptr)
            {
                m_drawShaderResourceGroup->Compile();
                BindSrg(m_drawShaderResourceGroup->GetRHIShaderResourceGroup());
            }
        }

        void FullscreenTrianglePass::BuildCommandListInternal(const RHI::FrameGraphExecuteContext& context)
        {
            RHI::CommandList* commandList = context.GetCommandList();

            SetSrgsForDraw(commandList);

            commandList->SetViewport(m_viewportState);
            commandList->SetScissor(m_scissorState);

            commandList->Submit(m_item);
        }
        
    }   // namespace RPI
}   // namespace AZ
