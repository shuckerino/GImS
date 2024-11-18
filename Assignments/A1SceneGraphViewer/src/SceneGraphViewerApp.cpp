#include "SceneGraphViewerApp.hpp"
#include "SceneFactory.hpp"
#include <d3dx12/d3dx12.h>
#include <gimslib/contrib/stb/stb_image.h>
#include <gimslib/d3d/DX12Util.hpp>
#include <gimslib/d3d/UploadHelper.hpp>
#include <gimslib/dbg/HrException.hpp>
#include <gimslib/io/CograBinaryMeshFile.hpp>
#include <gimslib/sys/Event.hpp>
#include <imgui.h>
#include <iostream>
#include <vector>
using namespace gims;

SceneGraphViewerApp::SceneGraphViewerApp(const DX12AppConfig config, const std::filesystem::path pathToScene)
    : DX12App(config)
    , m_examinerController(true)
    , m_scene(SceneGraphFactory::createFromAssImpScene(pathToScene, getDevice(), getCommandQueue()))
{
  m_examinerController.setTranslationVector(f32v3(0, -0.25f, 1.5));
  createRootSignature();
  createSceneConstantBuffer();
  createPipeline();
}

void SceneGraphViewerApp::onDraw()
{
  if (!ImGui::GetIO().WantCaptureMouse)
  {
    bool pressed  = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right);
    bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right);
    if (pressed || released)
    {
      bool left = ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Left);
      m_examinerController.click(pressed, left == true ? 1 : 2,
                                 ImGui::IsKeyDown(ImGuiKey_LeftCtrl) || ImGui::IsKeyDown(ImGuiKey_RightCtrl),
                                 getNormalizedMouseCoordinates());
    }
    else
    {
      m_examinerController.move(getNormalizedMouseCoordinates());
    }
  }

  const auto commandList = getCommandList();
  const auto rtvHandle   = getRTVHandle();
  const auto dsvHandle   = getDSVHandle();

  commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

  const float clearColor[] = {m_uiData.m_backgroundColor.x, m_uiData.m_backgroundColor.y, m_uiData.m_backgroundColor.z,
                              1.0f};
  commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
  commandList->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

  commandList->RSSetViewports(1, &getViewport());
  commandList->RSSetScissorRects(1, &getRectScissor());

  drawScene(commandList);
}

void SceneGraphViewerApp::onDrawUI()
{
  const auto imGuiFlags = m_examinerController.active() ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None;
  ImGui::Begin("Information", nullptr, imGuiFlags);
  ImGui::Text("Frametime: %f", 1.0f / ImGui::GetIO().Framerate * 1000.0f);
  ImGui::End();
  ImGui::Begin("Configuration", nullptr, imGuiFlags);
  ImGui::ColorEdit3("Background Color", &m_uiData.m_backgroundColor[0]);
  ImGui::End();
}

void SceneGraphViewerApp::createRootSignature()
{
  CD3DX12_ROOT_PARAMETER rootParameters[1] = {};

  CD3DX12_DESCRIPTOR_RANGE constantBufferTable;
  constantBufferTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);
  rootParameters[0].InitAsDescriptorTable(3, &constantBufferTable);

  CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDescription;
  rootSignatureDescription.Init(_countof(rootParameters), rootParameters, 0, nullptr,
                                D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  // Descriptor Range f�r Constant Buffers (b0, b1, b2)
  // D3D12_DESCRIPTOR_RANGE descriptorRangeCBV            = {};
  // descriptorRangeCBV.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
  // descriptorRangeCBV.NumDescriptors                    = 3; // Drei Constant Buffers
  // descriptorRangeCBV.BaseShaderRegister                = 0; // b0
  // descriptorRangeCBV.RegisterSpace                     = 0;
  // descriptorRangeCBV.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  //
  //// Descriptor Range f�r Texturen (t0 bis t4)
  // D3D12_DESCRIPTOR_RANGE descriptorRangeSRV            = {};
  // descriptorRangeSRV.RangeType                         = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
  // descriptorRangeSRV.NumDescriptors                    = 5; // F�nf Texturen
  // descriptorRangeSRV.BaseShaderRegister                = 0; // t0
  // descriptorRangeSRV.RegisterSpace                     = 0;
  // descriptorRangeSRV.OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;
  //
  //// Root Parameter f�r Constant Buffers
  // D3D12_ROOT_PARAMETER rootParameters[3] = {};
  //
  //// Constant Buffers (Descriptor Table)
  // rootParameters[0].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  // rootParameters[0].DescriptorTable.NumDescriptorRanges = 1;
  // rootParameters[0].DescriptorTable.pDescriptorRanges   = &descriptorRangeCBV;
  // rootParameters[0].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_ALL;
  //
  //// Texturen (Descriptor Table)
  // rootParameters[1].ParameterType                       = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
  // rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
  // rootParameters[1].DescriptorTable.pDescriptorRanges   = &descriptorRangeSRV;
  // rootParameters[1].ShaderVisibility                    = D3D12_SHADER_VISIBILITY_PIXEL;
  //
  //// Sampler (Descriptor Table)
  // D3D12_STATIC_SAMPLER_DESC sampler = {};
  // sampler.Filter                    = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
  // sampler.AddressU                  = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  // sampler.AddressV                  = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  // sampler.AddressW                  = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  // sampler.MipLODBias                = 0;
  // sampler.MaxAnisotropy             = 1;
  // sampler.ComparisonFunc            = D3D12_COMPARISON_FUNC_ALWAYS;
  // sampler.BorderColor               = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
  // sampler.MinLOD                    = 0.0f;
  // sampler.MaxLOD                    = D3D12_FLOAT32_MAX;
  // sampler.ShaderRegister            = 0; // s0
  // sampler.RegisterSpace             = 0;
  // sampler.ShaderVisibility          = D3D12_SHADER_VISIBILITY_PIXEL;
  //
  //// Root Signature Description
  // D3D12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
  // rootSignatureDesc.NumParameters             = _countof(rootParameters);
  // rootSignatureDesc.pParameters               = rootParameters;
  // rootSignatureDesc.NumStaticSamplers         = 1;
  // rootSignatureDesc.pStaticSamplers           = &sampler;
  // rootSignatureDesc.Flags                     = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

  ComPtr<ID3DBlob> rootBlob, errorBlob;
  D3D12SerializeRootSignature(&rootSignatureDescription, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob);

  getDevice()->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&m_rootSignature));
}

void SceneGraphViewerApp::createPipeline()
{
  waitForGPU();
  const auto inputElementDescs = TriangleMeshD3D12::getInputElementDescriptors();

  const auto vertexShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/TriangleMesh.hlsl", L"VS_main", L"vs_6_0");
  const auto pixelShader =
      compileShader(L"../../../Assignments/A1SceneGraphViewer/Shaders/TriangleMesh.hlsl", L"PS_main", L"ps_6_0");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout                        = {inputElementDescs.data(), (ui32)inputElementDescs.size()};
  psoDesc.pRootSignature                     = m_rootSignature.Get();
  psoDesc.VS                                 = HLSLCompiler::convert(vertexShader);
  psoDesc.PS                                 = HLSLCompiler::convert(pixelShader);
  psoDesc.RasterizerState                    = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  psoDesc.RasterizerState.FillMode           = D3D12_FILL_MODE_SOLID;
  psoDesc.RasterizerState.CullMode           = D3D12_CULL_MODE_NONE;
  psoDesc.BlendState                         = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
  psoDesc.DSVFormat                          = getDX12AppConfig().depthBufferFormat;
  psoDesc.DepthStencilState.DepthEnable      = TRUE;
  psoDesc.DepthStencilState.DepthFunc        = D3D12_COMPARISON_FUNC_LESS;
  psoDesc.DepthStencilState.DepthWriteMask   = D3D12_DEPTH_WRITE_MASK_ALL;
  psoDesc.DepthStencilState.StencilEnable    = FALSE;
  psoDesc.SampleMask                         = UINT_MAX;
  psoDesc.PrimitiveTopologyType              = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
  psoDesc.NumRenderTargets                   = 1;
  psoDesc.RTVFormats[0]                      = getDX12AppConfig().renderTargetFormat;
  psoDesc.SampleDesc.Count                   = 1;
  throwIfFailed(getDevice()->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&m_pipelineState)));
}

void SceneGraphViewerApp::drawScene(const ComPtr<ID3D12GraphicsCommandList>& cmdLst)
{
  updateSceneConstantBuffer();
  // Assigment 2: Uncomment after successfull implementation.
  // const auto cb                   = m_constantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
  const auto cameraMatrix = m_examinerController.getTransformationMatrix();

  // Assignment 6

  cmdLst->SetPipelineState(m_pipelineState.Get());

  // Assigment 2: Uncomment after successfull implementation.
  // cmdLst->SetGraphicsRootSignature(m_rootSignature.Get());
  // cmdLst->SetGraphicsRootConstantBufferView(0, cb);

  m_scene.addToCommandList(cmdLst, cameraMatrix, 1, 2, 3);
}

namespace
{
struct ConstantBuffer
{
  f32m4 projectionMatrix;
};

} // namespace

void SceneGraphViewerApp::createSceneConstantBuffer()
{
  const ConstantBuffer cb         = {};
  const auto           frameCount = getDX12AppConfig().frameCount;
  m_constantBuffers.resize(frameCount);
  for (ui32 i = 0; i < frameCount; i++)
  {
    m_constantBuffers[i] = ConstantBufferD3D12(cb, getDevice());
  }
}

void SceneGraphViewerApp::updateSceneConstantBuffer()
{
  ConstantBuffer cb;
  cb.projectionMatrix =
      glm::perspectiveFovLH_ZO<f32>(glm::radians(45.0f), (f32)getWidth(), (f32)getHeight(), 1.0f / 256.0f, 256.0f);
  m_constantBuffers[getFrameIndex()].upload(&cb);
}
