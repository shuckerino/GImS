#include "SceneGraphViewerApp.hpp"
#include "RayTracingUtils.hpp"
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

#define MAX_LIGHTS 8

SceneGraphViewerApp::SceneGraphViewerApp(const DX12AppConfig config, const std::filesystem::path pathToScene)
    : DX12App(config)
    , m_examinerController(true)
    , m_scene(SceneGraphFactory::createFromAssImpScene(pathToScene, getDevice(), getCommandQueue()))
    , m_rayTracingUtils(RayTracingUtils::createRayTracingUtils(getDevice(), m_scene, getCommandList(),
                                                               getCommandAllocator(), getCommandQueue(), (*this)))
{
  m_examinerController.setTranslationVector(f32v3(0, -0.25f, 1.5));
  createRootSignatures();
  createSceneConstantBuffer();
  createLightConstantBuffer();
  createPipeline();
}

#pragma region Init

void SceneGraphViewerApp::createRootSignatures()
{
  // graphics root signature
  CD3DX12_ROOT_PARAMETER   rootParameter[6] = {};
  CD3DX12_DESCRIPTOR_RANGE range            = {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0};
  rootParameter[0].InitAsConstantBufferView(0, 0, D3D12_SHADER_VISIBILITY_ALL);   // scene constant buffer
  rootParameter[1].InitAsConstants(32, 1, D3D12_ROOT_SIGNATURE_FLAG_NONE);        // mv matrix
  rootParameter[2].InitAsConstantBufferView(2, 0, D3D12_SHADER_VISIBILITY_PIXEL); // materials
  rootParameter[3].InitAsDescriptorTable(1, &range);                              // textures
  rootParameter[4].InitAsShaderResourceView(5);                                   // TLAS
  rootParameter[5].InitAsConstantBufferView(3, 0, D3D12_SHADER_VISIBILITY_PIXEL); // point lights

  D3D12_STATIC_SAMPLER_DESC sampler = {};
  sampler.Filter                    = D3D12_FILTER_MIN_MAG_MIP_POINT;
  sampler.AddressU                  = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler.AddressV                  = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler.AddressW                  = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
  sampler.MipLODBias                = 0;
  sampler.MaxAnisotropy             = 0;
  sampler.ComparisonFunc            = D3D12_COMPARISON_FUNC_NEVER;
  sampler.BorderColor               = D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK;
  sampler.MinLOD                    = 0.0f;
  sampler.MaxLOD                    = D3D12_FLOAT32_MAX;
  sampler.ShaderRegister            = 0;
  sampler.RegisterSpace             = 0;
  sampler.ShaderVisibility          = D3D12_SHADER_VISIBILITY_ALL;

  CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc = {};
  rootSignatureDesc.Init(_countof(rootParameter), rootParameter, 1, &sampler,
                         D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

  ComPtr<ID3DBlob> rootBlob, errorBlob;
  D3D12SerializeRootSignature(&rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &rootBlob, &errorBlob);

  getDevice()->CreateRootSignature(0, rootBlob->GetBufferPointer(), rootBlob->GetBufferSize(),
                                   IID_PPV_ARGS(&m_graphicsRootSignature));
}

void SceneGraphViewerApp::createPipeline()
{
  waitForGPU();
  const auto inputElementDescs = TriangleMeshD3D12::getInputElementDescriptors();

  const auto vertexShader =
      compileShader(L"../../../Assignments/RayTracing/Shaders/TriangleMesh.hlsl", L"VS_main", L"vs_6_8");
  const auto pixelShader =
      compileShader(L"../../../Assignments/RayTracing/Shaders/TriangleMesh.hlsl", L"PS_main", L"ps_6_8");

  D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
  psoDesc.InputLayout                        = {inputElementDescs.data(), (ui32)inputElementDescs.size()};
  psoDesc.pRootSignature                     = m_graphicsRootSignature.Get();
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

#pragma endregion

#pragma region OnDraw

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
  m_timer.Tick();
  static int    frameCnt    = 0;
  static double elapsedTime = 0.0f;
  double        totalTime   = m_timer.GetTotalSeconds();
  frameCnt++;

  // Compute averages over one second period.
  if ((totalTime - elapsedTime) >= 1.0f)
  {
    float diff = static_cast<float>(totalTime - elapsedTime);
    float fps  = static_cast<float>(frameCnt) / diff; // Normalize to an exact second.

    frameCnt    = 0;
    elapsedTime = totalTime;

    // one ray per pixel and per light
    m_numRaysPerSecond = (getWidth() * getHeight() * fps * m_pointLights.size()) / static_cast<float>(1e6);
  }

  const auto imGuiFlags = m_examinerController.active() ? ImGuiWindowFlags_NoInputs : ImGuiWindowFlags_None;
  ImGui::Begin("Controls", nullptr, imGuiFlags);
  ImGui::Text("Frametime: %f", 1.0f / ImGui::GetIO().Framerate * 1000.0f);
  ImGui::Text("Million Primary Rays/s: %f", m_numRaysPerSecond);
  ImGui::ColorEdit3("Background Color", &m_uiData.m_backgroundColor[0]);
  ImGui::SliderFloat("Shadow bias", &m_uiData.m_shadowBias, 0.0f, 5.0f);

  static i8 selectedLight = 0;
  // List existing lights
  if (ImGui::TreeNode("Point Lights"))
  {
    for (i8 i = 0; i < m_pointLights.size(); ++i)
    {
      ImGui::PushID(static_cast<int>(i));
      if (ImGui::Selectable(("Light " + std::to_string(i+1)).c_str(), selectedLight == i))
      {
        selectedLight = i;
      }
      ImGui::PopID();
    }
    ImGui::TreePop();
  }
  // Add/Remove Lights
  if (ImGui::Button("Add Light (max. 8)"))
  {
    if (m_pointLights.size() >= MAX_LIGHTS)
    {
      //ImGui::OpenPopup("Max number of lights reached!");
    }
    else
    {
      PointLight newLight;
      newLight.position[0] = 0.0f;
      newLight.position[1] = 0.0f;
      newLight.position[2] = 0.0f;
      newLight.color       = f32v3(1.0f, 1.0f, 1.0f);
      newLight.intensity   = 50.0f;

      m_pointLights.push_back(newLight);
      selectedLight = static_cast<i8>(m_pointLights.size() - 1);
    }
  }
  if (selectedLight >= 0 && selectedLight < m_pointLights.size())
  {
    if (ImGui::Button("Remove Selected Light"))
    {
      m_pointLights.erase(m_pointLights.begin() + selectedLight);
      selectedLight = static_cast<i8>(m_pointLights.size() - 1);
      
    }
  }

  // Edit Selected Light
  if (selectedLight >= 0 && selectedLight < m_pointLights.size())
  {
    PointLight& light = m_pointLights[selectedLight];
    ImGui::DragFloat3("Position", light.position, 0.5f, -100.0f, 100.0f, "%.3f", ImGuiSliderFlags_None);
    ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 100.0f);
    ImGui::ColorEdit3("Color", &light.color[0]);
  }

  ImGui::End();
}

void SceneGraphViewerApp::drawScene(const ComPtr<ID3D12GraphicsCommandList>& cmdLst)
{
  const auto cameraMatrix = m_examinerController.getTransformationMatrix();
  updateSceneConstantBuffer();
  updateLightConstantBuffer();

  //  Assignment 6 (normalize scene)
  const auto modelMatrix            = m_scene.getAABB().getNormalizationTransformation();
  const auto cameraAndNormalization = cameraMatrix * modelMatrix;

  cmdLst->SetPipelineState(m_pipelineState.Get());

  cmdLst->SetGraphicsRootSignature(m_graphicsRootSignature.Get());

  // set constant buffer
  const auto sceneCb = m_sceneConstantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
  cmdLst->SetGraphicsRootConstantBufferView(0, sceneCb);
  const auto lightCb = m_lightConstantBuffers[getFrameIndex()].getResource()->GetGPUVirtualAddress();
  cmdLst->SetGraphicsRootConstantBufferView(5, lightCb);

  // ray tracing
  cmdLst->SetGraphicsRootShaderResourceView(4, m_rayTracingUtils.m_topLevelAS->GetGPUVirtualAddress());

  m_scene.addToCommandList(cmdLst, cameraAndNormalization, 1, 2, 3);
}

#pragma endregion

#pragma region Constant Buffer

namespace
{
struct SceneConstantBuffer
{
  f32m4 projectionMatrix;
  f32   shadowBias;
};

struct PointLightConstantBuffer
{
  PointLight pointLights[MAX_LIGHTS];
  ui32       numPointLights;
};

} // namespace

#pragma region Scene constant buffer

void SceneGraphViewerApp::createSceneConstantBuffer()
{
  const SceneConstantBuffer cb         = {};
  const auto                frameCount = getDX12AppConfig().frameCount;
  m_sceneConstantBuffers.resize(frameCount);
  for (ui32 i = 0; i < frameCount; i++)
  {
    m_sceneConstantBuffers[i] = ConstantBufferD3D12(cb, getDevice());
  }
}

void SceneGraphViewerApp::updateSceneConstantBuffer()
{
  SceneConstantBuffer cb;

  cb.shadowBias = m_uiData.m_shadowBias;
  cb.projectionMatrix =
      glm::perspectiveFovLH_ZO<f32>(glm::radians(45.0f), (f32)getWidth(), (f32)getHeight(), 0.01f, 1000.0f);
  m_sceneConstantBuffers[getFrameIndex()].upload(&cb);
}

#pragma endregion

#pragma region Point Light Constant Buffer

void SceneGraphViewerApp::createLightConstantBuffer()
{
  const PointLightConstantBuffer cb         = {};
  const auto                     frameCount = getDX12AppConfig().frameCount;
  m_lightConstantBuffers.resize(frameCount);
  for (ui32 i = 0; i < frameCount; i++)
  {
    m_lightConstantBuffers[i] = ConstantBufferD3D12(cb, getDevice());
  }

  PointLight p1;
  p1.position[0] = -20.0f;
  p1.position[1] = 55.5f;
  p1.position[2] = -30.0f;
  p1.color       = f32v3(1.0f, 0.5f, 0.5f);
  p1.intensity   = 50.0f;

  PointLight p2;
  p2.position[0] = 22.0f;
  p2.position[1] = 11.0f;
  p2.position[2] = -21.0f;
  p2.color       = f32v3(1.0f, 1.0f, 1.0f);
  p2.intensity   = 50.0f;

  m_pointLights.push_back(p1);
  m_pointLights.push_back(p2);
}

void SceneGraphViewerApp::updateLightConstantBuffer()
{
  PointLightConstantBuffer cb;

  cb.numPointLights = static_cast<ui32>(m_pointLights.size());
  for (ui8 i = 0; i < m_pointLights.size(); i++)
  {
    cb.pointLights[i] = m_pointLights.at(i);
  }
  m_lightConstantBuffers[getFrameIndex()].upload(&cb);
}

#pragma endregion

#pragma endregion
