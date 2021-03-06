#include "SceneRenderer.hpp"
#include "Engine/Application/Window.hpp"
#include "Engine/Core/Time/Clock.hpp"
#include "Engine/Graphics/RHI/RHIContext.hpp"
#include "Engine/Graphics/Camera.hpp"
#include "Engine/Graphics/Program/Program.hpp"
#include "Engine/Graphics/RHI/PipelineState.hpp"
#include "Engine/Graphics/RHI/VertexLayout.hpp"
#include "Engine/Graphics/Model/Vertex.hpp"
#include "Engine/Renderer/Renderable/Renderable.hpp"
#include "Engine/Graphics/Program/Material.hpp"
#include "Engine/Graphics/Model/Mesh.hpp"
#include "Engine/Graphics/RHI/RHIDevice.hpp"
#include "Engine/Math/MathUtils.hpp"

#include "Renderer/SceneRenderer/GenGBuffer_ps.h"
#include "Renderer/SceneRenderer/GenGBuffer_vs.h"
#include "Renderer/SceneRenderer/SurfelPlacement_cs.h"
#include "Renderer/SceneRenderer/SurfelVisual_cs.h"
#include "Renderer/SceneRenderer/SurfelGI_cs.h"
#include "Renderer/SceneRenderer/GenAccelerationStructure_cs.h"
#include "Renderer/SceneRenderer/GenAO_cs.h"
#include "Renderer/SceneRenderer/DeferredLighting_ComputeIndirect_cs.h"
#include "Renderer/SceneRenderer/DeferredLighting_cs.h"
#include "Renderer/SceneRenderer/PathTracing_cs.h"
#include "Renderer/SceneRenderer/SurfelCoverageCompute_cs.h"

#include "Renderer/SceneRenderer/BvhVisual_vs.h";

#include "Renderer/SceneRenderer/BvhVisual_gs.h";

#include "Renderer/SceneRenderer/BvhVisual_ps.h";

#include "Renderer/SceneRenderer/fxaa_ps.h"
#include "Renderer/SceneRenderer/fxaa_vs.h"

#include "Engine/Framework/Light.hpp"
#include "Engine/Input/Input.hpp"
#include "Engine/Math/Primitives/uvec3.hpp"
#include "Engine/Graphics/Program/ProgramInst.hpp"
#include "Engine/Graphics/Model/BVH.hpp"
#include "Engine/Gui/ImGui.hpp"
#include "Engine/Debug/Profile/Profiler.hpp"
static Program::sptr_t gGenGBufferProgram = nullptr;

static Program::sptr_t gGenAOProgram = nullptr;
static RootSignature::scptr_t gGenAORootSig = nullptr;

static Program::sptr_t gGenAccelerationStructureProgram = nullptr;

static Program::sptr_t gSurfelPlacementProgram = nullptr;
static RootSignature::scptr_t gSurfelPlacementRootSig = nullptr;

static Program::sptr_t gSurfelVisualProgram = nullptr;
static RootSignature::scptr_t gSurfelVisualRootSig = nullptr;

static Program::sptr_t gSurfelCoverageProgram = nullptr;

static Program::sptr_t gDeferredLighting_indirect = nullptr;
static Program::sptr_t gDeferredLighting = nullptr;

static Program::sptr_t gSurfelGIProgram = nullptr;

struct surfel_t {
  vec3 position;
  float __padding0;

  vec3 normal;
  float __padding1;

  vec3 color;
  float __padding2;

  vec3 indirectLighting;
  float age;


  std::string toString() {
    return Stringf("%s, %s, %s, %s",
                   position.toString().c_str(),
                   normal.toString().c_str(),
                   color.toString().c_str(),
                   indirectLighting.toString().c_str());
  }
};

struct surfel_histroy_t {
  static constexpr uint TOTAL_HISTORY = 28;
  vec4 buffer[TOTAL_HISTORY];
  uint32_t nextToWrite;
  uint32_t __padding[3];
  struct {
    vec2 start;
    vec2 end;

    vec2 tangentStart;
    vec2 tangentEnd;

    float smooth;
    float force;
    float scale;
    float _padding;
  } weightCurve;
} ;

struct SurfelBucketInfo {
  uint32_t startIndex;
  uint32_t endIndex;
  uint32_t currentCount = 0;
  uint32_t __padding = 0;
};

static const uint32_t BUCKET_COUNT = 0xf;
void GetSpatialHashComponent(uint32_t hash, uvec3& component) {
  component.x = 0x000f & hash;
  hash >>= 4;
  component.y = 0x000f & hash;
  hash >>= 4;
  component.z = 0x000f & hash;
}



/* { ([0x0000][0x0001]....)([0x0010][0x0011]....)------([0x00f0][0x00f1]....) }
{ ([0x0100][0x0101]....)([0x0110][0x0111]....)------([0x01f0][0x01f1]....) }
{ ([0x0200][0x0201]....)([0x0210][0x0211]....)------([0x02f0][0x02f1]....) }
.....
{ ([0x0f00][0x0f01]....)([0x0f10][0x0f11]....)------([0x0ff0][0x0ff1]....) }

actual element in the bucket: [startIndex, endIndex)
*/
void InitSpatialInfo(uint32_t hash, uint32_t arraySize, SurfelBucketInfo& info) {
  uint32_t bucketCount = BUCKET_COUNT * BUCKET_COUNT * BUCKET_COUNT;
  uint32_t offset = (uint32_t)ceil(float(arraySize) / float(bucketCount));

  uvec3 component;
  GetSpatialHashComponent(hash, component);

  info.startIndex = offset * (component.z * BUCKET_COUNT * BUCKET_COUNT + component.y * BUCKET_COUNT + component.x);
  info.endIndex = info.startIndex + offset;
  info.currentCount = 0;
}

SceneRenderer::SceneRenderer(const RenderScene & target): mTargetScene(target) {

}

SceneRenderer::~SceneRenderer() {
  mSurfelDump.close();
}

void SceneRenderer::onLoad(RHIContext&) {
  if(gGenGBufferProgram == nullptr) {
    gGenGBufferProgram = Program::sptr_t(new Program());
    gGenGBufferProgram->stage(SHADER_TYPE_VERTEX)
      .setFromBinary(gGenGBuffer_vs, sizeof(gGenGBuffer_vs));
    gGenGBufferProgram->stage(SHADER_TYPE_FRAGMENT)
      .setFromBinary(gGenGBuffer_ps, sizeof(gGenGBuffer_ps));
    gGenGBufferProgram->compile();

    gGenAOProgram = Program::sptr_t(new Program());
    gGenAOProgram->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gGenAO_cs, sizeof(gGenAO_cs));
    gGenAOProgram->compile();
    gGenAORootSig = gGenAOProgram->rootSignature();

    gGenAccelerationStructureProgram = Program::sptr_t(new Program());
    gGenAccelerationStructureProgram->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gGenAccelerationStructure_cs, sizeof(gGenAccelerationStructure_cs));
    gGenAccelerationStructureProgram->compile();

    gSurfelPlacementProgram = Program::sptr_t(new Program());
    gSurfelPlacementProgram->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gSurfelPlacement_cs, sizeof(gSurfelPlacement_cs));
    gSurfelPlacementProgram->compile();
    gSurfelPlacementRootSig = gSurfelPlacementProgram->rootSignature();

    gSurfelVisualProgram = Program::sptr_t(new Program());
    gSurfelVisualProgram->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gSurfelVisual_cs, sizeof(gSurfelVisual_cs));
    gSurfelVisualProgram->compile();
    gSurfelVisualRootSig = gSurfelVisualProgram->rootSignature();

    gSurfelCoverageProgram = Program::sptr_t(new Program());
    gSurfelCoverageProgram->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gSurfelCoverageCompute_cs, sizeof(gSurfelCoverageCompute_cs));
    gSurfelCoverageProgram->compile();

    gDeferredLighting = Program::sptr_t(new Program());
    gDeferredLighting->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gDeferredLighting_cs, sizeof(gDeferredLighting_cs));
    gDeferredLighting->compile();

    gDeferredLighting_indirect = Program::sptr_t(new Program());
    gDeferredLighting_indirect->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gDeferredLighting_ComputeIndirect_cs, sizeof(gDeferredLighting_ComputeIndirect_cs));
    gDeferredLighting_indirect->compile();

    gSurfelGIProgram = Program::sptr_t(new Program());
    gSurfelGIProgram->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gSurfelGI_cs, sizeof(gSurfelGI_cs));
    gSurfelGIProgram->compile();
  }
  mFrameData.frameCount = 0;
  mFrameData.time = 0;

  std::string filenameStamped = Stringf("surfel-%s.csv", Timestamp().toString().c_str());
  // mSurfelDump.open(filenameStamped, std::ofstream::out | std::ofstream::app);


  auto size = Window::Get()->bounds().size();
  uint width = (uint)size.x, height = (uint)size.y;

  mAO = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                              RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mAO);
  mGAO = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                              RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mGAO);


  mGAlbedo = Texture2::create(width, height, TEXTURE_FORMAT_RGBA8,
                              RHIResource::BindingFlag::RenderTarget | RHIResource::BindingFlag::ShaderResource);
  NAME_RHIRES(mGAlbedo);

  mGNormal = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                              RHIResource::BindingFlag::RenderTarget | RHIResource::BindingFlag::ShaderResource);
  NAME_RHIRES(mGNormal);

  mGPosition = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                              RHIResource::BindingFlag::RenderTarget | RHIResource::BindingFlag::ShaderResource);
  NAME_RHIRES(mGNormal);

  mGPosition = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                                RHIResource::BindingFlag::RenderTarget | RHIResource::BindingFlag::ShaderResource);
  NAME_RHIRES(mGPosition);

  mGVelocity = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                                RHIResource::BindingFlag::RenderTarget | RHIResource::BindingFlag::ShaderResource);

  NAME_RHIRES(mGVelocity);


  mGDepth = Texture2::create(width, height, TEXTURE_FORMAT_D24S8,
                             RHIResource::BindingFlag::DepthStencil | RHIResource::BindingFlag::ShaderResource);

  NAME_RHIRES(mGDepth);
  mcFrameData = RHIBuffer::create(sizeof(frame_data_t), RHIResource::BindingFlag::ConstantBuffer, RHIBuffer::CPUAccess::Write);
  NAME_RHIRES(mcFrameData);
  mcCamera = RHIBuffer::create(sizeof(camera_t), RHIResource::BindingFlag::ConstantBuffer, RHIBuffer::CPUAccess::Write);
  NAME_RHIRES(mcCamera);
  mcModel = RHIBuffer::create(sizeof(mat44), RHIResource::BindingFlag::ConstantBuffer, RHIBuffer::CPUAccess::Write);
  NAME_RHIRES(mcModel);

  mSurfelsBuffer[0] = TypedBuffer::For<surfel_t>(0xfff * 0xff, RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mSurfelsBuffer[0]);

  mSurfelsBuffer[1] = TypedBuffer::For<surfel_t>(0xfff * 0xff, RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mSurfelsBuffer[1]);

  mSurfelsHistory = TypedBuffer::For<surfel_histroy_t>(0xfff * 0xff, RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mSurfelsHistory);
  {
    mSurfelBuckets = TypedBuffer::For<SurfelBucketInfo>(0xfff + 1, RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
    NAME_RHIRES(mSurfelBuckets);

    SurfelBucketInfo infos[0xfff + 1];

    for(uint i = 0; i <= 0xfff; i++) {
      InitSpatialInfo(i, 0xfff * 0xff, infos[i]);
    }

    mSurfelBuckets->updateData(infos, 0, sizeof(SurfelBucketInfo) * (0xfff + 1));
  }

  mcLight = RHIBuffer::create(sizeof(mat44), RHIResource::BindingFlag::ConstantBuffer, RHIBuffer::CPUAccess::Write);
  NAME_RHIRES(mcLight);

  mSurfelVisual = Texture2::create(width, height, TEXTURE_FORMAT_RGBA8,
                              RHIResource::BindingFlag::RenderTarget | RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mSurfelVisual);

  mSurfelSpawnChance = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                                    RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mSurfelSpawnChance);

  mSurfelCoverage = Texture2::create(width, height, TEXTURE_FORMAT_RGBA16,
                                     RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mSurfelCoverage);

  mIndirectLight = Texture2::create(width >> 1, height >> 1, TEXTURE_FORMAT_RGBA16, 
                                    RHIResource::BindingFlag::RenderTarget | RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess);
  NAME_RHIRES(mIndirectLight);

  mScene = Texture2::create(width, height, TEXTURE_FORMAT_RGBA8,
                            RHIResource::BindingFlag::ShaderResource | RHIResource::BindingFlag::UnorderedAccess | RHIResource::BindingFlag::RenderTarget);
  NAME_RHIRES(mScene);

  mTargetScene.bvh()->uploadNodesToGpu(mBVH);
  NAME_RHIRES(mBVH);
  mTargetScene.bvh()->uploadVerticesToGpu(mVertexData);
  NAME_RHIRES(mVertexData);
}

void SceneRenderer::onRenderFrame(RHIContext& ctx) {
  setupFrame();
  setupView(ctx);
  ctx.bindDescriptorHeap();
  ctx.copyResource(*mAO, *mGAO);
  // visualizeBVH(ctx);
  // return;
  ctx.transitionBarrier(mGAO.get(), RHIResource::State::NonPixelShader, TRANSITION_BEGIN);
  ctx.transitionBarrier(mAO.get(), RHIResource::State::UnorderedAccess, TRANSITION_BEGIN);
  genGBuffer(ctx);
  genAO(ctx);

  static bool pt = false;
  if (Input::Get().isKeyJustDown('P')) {
    mFrameData.frameCount = 0;
    ctx.clearRenderTarget(*mScene->rtv(), Rgba::black);
    pt = !pt;
  }


  if (pt) {
    if(Input::Get().anyKeyDown()) {
      mFrameData.frameCount = 0;
      ctx.clearRenderTarget(*mScene->rtv(), Rgba::black);
    }
    pathTracing(ctx);
    return;
  }

  if(shouldRecomputeIndirect()) {
    computeIndirectLighting(ctx);
  }

  accumlateGI(ctx);
  computeSurfelCoverage(ctx);
  accumlateSurfels(ctx);
  
  if(!Input::Get().isKeyDown(KEYBOARD_SPACE)) {
    deferredLighting(ctx);
    fxaa(ctx);

    // visualizeBVH(ctx);
  } else {
    visualizeSurfels(ctx);
  }

  // dumpSurfels(ctx);

}

void SceneRenderer::updateDescriptors() {

  {
    FrameBuffer::Desc desc;
    desc.defineColorTarget(0, TEXTURE_FORMAT_RGBA8, false); // albedo
    desc.defineColorTarget(1, TEXTURE_FORMAT_RGBA16, false); // normal
    desc.defineColorTarget(2, TEXTURE_FORMAT_RGBA16, false); // position
    desc.defineColorTarget(3, TEXTURE_FORMAT_RGBA16, false); // velocity
    desc.defineDepthTarget(TEXTURE_FORMAT_D24S8, false); // depth

    mGFbo.setDesc(desc);

    mGFbo.setColorTarget(mGAlbedo->rtv(), 0);
    mGFbo.setColorTarget(mGNormal->rtv(), 1);
    mGFbo.setColorTarget(mGPosition->rtv(), 2);
    mGFbo.setColorTarget(mGVelocity->rtv(), 3);
    mGFbo.setDepthStencilTarget(mGDepth->dsv());
  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::Cbv, 0, 4);

    mDSharedDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);

    mDSharedDescriptors->setCbv(0, 0, *mcFrameData->cbv());
    mDSharedDescriptors->setCbv(0, 1, *mcCamera->cbv());
    mDSharedDescriptors->setCbv(0, 2, *mcModel->cbv());
    mDSharedDescriptors->setCbv(0, 3, *mcLight->cbv());
  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::TextureSrv, 10, 6);

    mDGBufferDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);

    mDGBufferDescriptors->setSrv(0, 0, *mGAlbedo->srv());
    mDGBufferDescriptors->setSrv(0, 1, *mGNormal->srv());
    mDGBufferDescriptors->setSrv(0, 2, *mGPosition->srv());
    mDGBufferDescriptors->setSrv(0, 3, *mGDepth->srv());
    mDGBufferDescriptors->setSrv(0, 4, *mVertexData->srv());
    mDGBufferDescriptors->setSrv(0, 5, *mBVH->srv());
  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::TextureUav, 0, 1);

    mDGenAOUavDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);

    mDGenAOUavDescriptors->setUav(0, 0, *mAO->uav());
  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::StructuredBufferUav, 0, 6);

    mDAccumulateSurfelUavDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);

    mDAccumulateSurfelUavDescriptors->setUav(0, 0, *mSurfels[0]->uav());
    mDAccumulateSurfelUavDescriptors->setUav(0, 1, *mSurfels[1]->uav());
    mDAccumulateSurfelUavDescriptors->setUav(0, 2, *mSurfelBuckets->uav());
    mDAccumulateSurfelUavDescriptors->setUav(0, 3, *mSurfelCoverage->uav());
    mDAccumulateSurfelUavDescriptors->setUav(0, 4, *mSurfelSpawnChance->uav());
    mDAccumulateSurfelUavDescriptors->setUav(0, 5, *mSurfelsHistory->uav());

  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::StructuredBufferUav, 0, 3);
    layout.addRange(DescriptorSet::Type::TextureUav, 0, 1);

    mDSurfelVisualDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);

    mDSurfelVisualDescriptors->setUav(0, 0, *mSurfels[0]->uav());
    mDSurfelVisualDescriptors->setUav(0, 1, *mSurfelBuckets->uav());
    mDSurfelVisualDescriptors->setUav(0, 2, *mSurfelsHistory->uav());
    mDSurfelVisualDescriptors->setUav(1, 0, *mSurfelVisual->uav());

  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::StructuredBufferUav, 0, 4);

    mDSurfelGIDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);

    mDSurfelGIDescriptors->setUav(0, 0, *mSurfels[0]->uav());
    mDSurfelGIDescriptors->setUav(0, 1, *mSurfels[1]->uav());
    mDSurfelGIDescriptors->setUav(0, 2, *mSurfelBuckets->uav());
    mDSurfelGIDescriptors->setUav(0, 3, *mSurfelsHistory->uav());

  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::StructuredBufferUav, 0, 3);

    mDDeferredLightingIndirectDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);

    mDDeferredLightingIndirectDescriptors->setUav(0, 0, *mSurfels[0]->uav());
    mDDeferredLightingIndirectDescriptors->setUav(0, 1, *mSurfelBuckets->uav());
    mDDeferredLightingIndirectDescriptors->setUav(0, 2, *mIndirectLight->uav());

  }
  {
    DescriptorSet::Layout layout;
    layout.addRange(DescriptorSet::Type::TextureUav, 0, 3);
    layout.addRange(DescriptorSet::Type::TextureSrv, 0, 1);

    mDDeferredLightingDescriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);


    mDDeferredLightingDescriptors->setUav(0, 0, *mScene->uav());
    mDDeferredLightingDescriptors->setUav(0, 1, *mIndirectLight->uav());
    mDDeferredLightingDescriptors->setUav(0, 2, *mSurfelSpawnChance->uav());
    mDDeferredLightingDescriptors->setSrv(0, 3, *mGAO->srv());
  }
}

void SceneRenderer::genGBuffer(RHIContext& ctx) {
  SCOPED_GPU_EVENT(ctx, "Gen G-Buffer");

  ctx.setFrameBuffer(mGFbo);

  static S<GraphicsProgramInst> prog;

  if(!prog) {
    prog = GraphicsProgramInst::create(gGenGBufferProgram);

    prog->setCbv(*mcFrameData->cbv(), 0);
    prog->setCbv(*mcCamera->cbv(), 1);
    prog->setCbv(*mcModel->cbv(), 2);
    prog->setCbv(*mcLight->cbv(), 3);
    prog->setSrv(*mAO->srv(), 6);
  }

  GraphicsState::Desc desc;
  desc.setProgram(prog->prog());
  desc.setFboDesc(mGFbo.desc());
  desc.setPrimTye(GraphicsState::PrimitiveType::Triangle);
  desc.setRootSignature(gGenGBufferProgram->rootSignature());
  desc.setVertexLayout(VertexLayout::For<vertex_lit_t>());

  GraphicsState::sptr_t gs = GraphicsState::create(desc);
  ctx.setGraphicsState(*gs);

  ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::RenderTarget);
  ctx.transitionBarrier(mGNormal.get(), RHIResource::State::RenderTarget);
  ctx.transitionBarrier(mGPosition.get(), RHIResource::State::RenderTarget);
  ctx.transitionBarrier(mGDepth.get(), RHIResource::State::DepthStencil);

  prog->apply(ctx, true);

  uint totalCount = 0;
  for(Renderable* r: mTargetScene.Renderables()) {
    r->material()->bindForGraphics(ctx, *gGenGBufferProgram->rootSignature(), 1);
    r->mesh()->bindForContext(ctx);
    mcModel->updateData(r->transform()->localToWorld());

    for(const draw_instr_t& inst: r->mesh()->instructions()) {
      totalCount += inst.elementCount;
      ctx.setPrimitiveTopology(inst.prim);
      if(inst.useIndices) {
        ctx.drawIndexed(0, inst.startIndex, inst.elementCount);
      } else {
        ctx.draw(inst.startIndex, inst.elementCount);
      }
    }
  }

  ctx.transitionBarrier(mGVelocity.get(), RHIResource::State::NonPixelShader, TRANSITION_BEGIN);


  // if(!mAccelerationStructure) {
  //   SCOPED_GPU_EVENT("Gen AccelerationStructure");
  //   DescriptorSet::Layout layout;
  //
  //   struct vert_t {
  //     vec4 position;
  //     vec4 color;
  //   };
  //   mAccelerationStructure = TypedBuffer::create(sizeof(vert_t), totalCount, RHIResource::BindingFlag::UnorderedAccess);
  //   NAME_RHIRES(mAccelerationStructure);
  //
  //   // mapping with the vertex layout
  //   layout.addRange(DescriptorPool::Type::TypedBufferSrv, 0, 5);
  //   layout.addRange(DescriptorPool::Type::StructuredBufferUav, 0, 1);
  //
  //   DescriptorSet::sptr_t descriptors = DescriptorSet::create(RHIDevice::get()->gpuDescriptorPool(), layout);
  //
  //   RootSignature::Desc desc;
  //   desc.addDescriptorSet(layout);
  //   static RootSignature::sptr_t sig = RootSignature::create(desc);
  //
  //   ComputeState::Desc pipdesc;
  //
  //   pipdesc.setRootSignature(sig);
  //   pipdesc.setProgram(gGenAccelerationStructureProgram);
  //
  //   static ComputeState::sptr_t computeState = ComputeState::create(pipdesc);
  //
  //   ctx.setComputeState(*computeState);
  //   descriptors->bindForCompute(ctx, *sig, 0);
  //
  //   descriptors->setUav(1, 0, *mAccelerationStructure->uav());
  //   for(uint i = 0; i < mTargetScene.Renderables().size(); i++) {
  //     Renderable* r = mTargetScene.Renderables()[i];
  //     for(auto& attribute: r->mesh()->layout().attributes()) {
  //       ctx.transitionBarrier(&r->mesh()->vertices(attribute.streamIndex)->res(), RHIResource::State::NonPixelShader);
  //       descriptors->setSrv(0, attribute.streamIndex, r->mesh()->vertices(attribute.streamIndex)->srv());
  //     }
  //     //
  //     // uint x = r->mesh()->vertices(0)->size() / 16 + 1;
  //     // uint y = 1;
  //     ctx.dispatch(1, 1, 1);
  //   }
  //
  //   mDGBufferDescriptors->setSrv(0, 4, mAccelerationStructure->srv());
  //
  // }
}

void SceneRenderer::genAO(RHIContext& ctx) {
  SCOPED_GPU_EVENT(ctx, "Gen AO");

  // mDGBufferDescriptors->setSrv(0, 5, *ShaderResourceView::nullView());

  static ComputeState::sptr_t computeState;
  static S<ComputeProgramInst> prog;
  if(!computeState) {
    ComputeState::Desc desc;
    desc.setRootSignature(gGenAORootSig);
    desc.setProgram(gGenAOProgram);
    computeState = ComputeState::create(desc);


    prog = ComputeProgramInst::create(gGenAOProgram);

    prog->setCbv(*mcFrameData->cbv(), 0);
    prog->setCbv(*mcCamera->cbv(), 1);

    prog->setSrv(*mGNormal->srv(), 11);
    prog->setSrv(*mGPosition->srv(), 12);
    prog->setSrv(*mGDepth->srv(), 13);
    prog->setSrv(*mGVelocity->srv(), 14);
    prog->setSrv(*mVertexData->srv(), 15);
    prog->setSrv(*mBVH->srv(), 16);
    prog->setSrv(*mGAO->srv(), 17);
    prog->setUav(*mAO->uav(), 0);

  }
  ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGVelocity.get(), RHIResource::State::NonPixelShader, TRANSITION_END);
  ctx.transitionBarrier(mVertexData.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGAO.get(), RHIResource::State::NonPixelShader, TRANSITION_END);
  ctx.transitionBarrier(mAO.get(), RHIResource::State::UnorderedAccess, TRANSITION_END);

  ctx.setComputeState(*computeState);

  prog->apply(ctx, true);

  uint x = uint(Window::Get()->bounds().width()) / 16 + 1;
  uint y = uint(Window::Get()->bounds().height()) / 8 + 1;

  ctx.dispatch(x, y, 1);


  // ctx.copyResource(*mAO, *RHIDevice::get()->backBuffer());
}

void SceneRenderer::computeSurfelCoverage(RHIContext& ctx) {

  SCOPED_GPU_EVENT(ctx, "Compute Surfel Coverage");

  static ComputeState::sptr_t computeState;
  if (!computeState) {
    ComputeState::Desc desc;
    desc.setRootSignature(gSurfelCoverageProgram->rootSignature());
    desc.setProgram(gSurfelCoverageProgram);
    computeState = ComputeState::create(desc);
  }
  ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);
  // ctx.transitionBarrier(mAccelerationStructure.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mSurfels[0].get(), RHIResource::State::UnorderedAccess);
  ctx.transitionBarrier(mSurfelCoverage.get(), RHIResource::State::UnorderedAccess);
  ctx.setComputeState(*computeState);

  // mDGenAOUavDescriptors->setUav(0, 1, mTargetScene.accelerationStructure());
  mDSharedDescriptors->bindForCompute(ctx, *gSurfelCoverageProgram->rootSignature(), 0);
  mDGBufferDescriptors->bindForCompute(ctx, *gSurfelCoverageProgram->rootSignature(), 1);
  mDAccumulateSurfelUavDescriptors->bindForCompute(ctx, *gSurfelCoverageProgram->rootSignature(), 2);

  uint x = uint(Window::Get()->bounds().width()) / 16  + 1;
  uint y = uint(Window::Get()->bounds().height()) / 16 + 1;

  ctx.dispatch(x, y, 1);

}

void SceneRenderer::accumlateSurfels(RHIContext& ctx) {

  SCOPED_GPU_EVENT(ctx, "Accumlate Surfels");

  static ComputeState::sptr_t computeState;
  if (!computeState) {
    ComputeState::Desc desc;
    desc.setRootSignature(gSurfelPlacementRootSig);
    desc.setProgram(gSurfelPlacementProgram);
    computeState = ComputeState::create(desc);
  }
  ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);
  // ctx.transitionBarrier(mAccelerationStructure.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mSurfels[0].get(), RHIResource::State::UnorderedAccess);
  ctx.transitionBarrier(mSurfelBuckets.get(), RHIResource::State::UnorderedAccess);
  ctx.transitionBarrier(mSurfelCoverage.get(), RHIResource::State::UnorderedAccess);

  ctx.setComputeState(*computeState);

  // mDGenAOUavDescriptors->setUav(0, 1, mTargetScene.accelerationStructure());
  mDSharedDescriptors->bindForCompute(ctx, *gSurfelPlacementRootSig, 0);
  mDGBufferDescriptors->bindForCompute(ctx, *gSurfelPlacementRootSig, 1);
  mDAccumulateSurfelUavDescriptors->bindForCompute(ctx, *gSurfelPlacementRootSig, 2);

  uint x = uint(Window::Get()->bounds().width()) / 16 / 8 + 1;
  uint y = uint(Window::Get()->bounds().height()) / 16 / 8 + 1;

  ctx.dispatch(x, y, 1);

}

void SceneRenderer::accumlateGI(RHIContext& ctx) {

  SCOPED_GPU_EVENT(ctx, "Accumlate GI");

  static ComputeState::sptr_t computeState;
  if (!computeState) {
    ComputeState::Desc desc;
    desc.setProgram(gSurfelGIProgram);
    computeState = ComputeState::create(desc);
  }
  ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mSurfels[0].get(), RHIResource::State::UnorderedAccess);
  ctx.transitionBarrier(mSurfelVisual.get(), RHIResource::State::UnorderedAccess);

  ctx.setComputeState(*computeState);

  // mDGenAOUavDescriptors->setUav(0, 1, mTargetScene.accelerationStructure());
  mDSharedDescriptors->bindForCompute(ctx, *gSurfelGIProgram->rootSignature(), 0);
  mDGBufferDescriptors->bindForCompute(ctx, *gSurfelGIProgram->rootSignature(), 1);
  mDSurfelGIDescriptors->bindForCompute(ctx, *gSurfelGIProgram->rootSignature(), 2);

  ctx.dispatch(16, 1, 1);
  // ctx.copyResource(*mSurfelVisual, *RHIDevice::get()->backBuffer());
}

void SceneRenderer::visualizeSurfels(RHIContext& ctx) {

  SCOPED_GPU_EVENT(ctx, "Visualize Surfels");

  static ComputeState::sptr_t computeState;
  if (!computeState) {
    ComputeState::Desc desc;
    desc.setRootSignature(gSurfelVisualRootSig);
    desc.setProgram(gSurfelVisualProgram);
    computeState = ComputeState::create(desc);
  }
  ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);
  // ctx.transitionBarrier(mAccelerationStructure.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mSurfels[0].get(), RHIResource::State::UnorderedAccess);
  ctx.transitionBarrier(mSurfelVisual.get(), RHIResource::State::UnorderedAccess);

  ctx.setComputeState(*computeState);

  // mDGenAOUavDescriptors->setUav(0, 1, mTargetScene.accelerationStructure());
  mDSharedDescriptors->bindForCompute(ctx, *gSurfelVisualRootSig, 0);
  mDGBufferDescriptors->bindForCompute(ctx, *gSurfelVisualRootSig, 1);
  mDSurfelVisualDescriptors->bindForCompute(ctx, *gSurfelVisualRootSig, 2);

  uint x = uint(Window::Get()->bounds().width()) / 32 + 1;
  uint y = uint(Window::Get()->bounds().height()) / 32 + 1;

  ctx.dispatch(x, y, 1);

  ctx.copyResource(*mSurfelVisual, *RHIDevice::get()->backBuffer());

}

void SceneRenderer::visualizeBVH(RHIContext& ctx) {
  static S<GraphicsProgramInst> progIns;
  static GraphicsState::sptr_t graphicsState;

  if(!progIns) {
    auto prog = Resource<Program>::get("internal/Shader/scene-renderer/bvhVisual");

    progIns = GraphicsProgramInst::create(prog);
    progIns->setSrv(*mBVH->srv(), 0);
    progIns->setCbv(*mcCamera->cbv(), 1);
    GraphicsState::Desc desc;
    desc.setRootSignature(prog->rootSignature());
    desc.setProgram(prog);
    desc.setFboDesc(mGFbo.desc());
    desc.setPrimTye(GraphicsState::PrimitiveType::Point);
    desc.setVertexLayout(VertexLayout::For<vertex_pcu_t>());
    graphicsState = GraphicsState::create(desc);
  }

  FrameBuffer fbo;
  {
    FrameBuffer::Desc desc;
    desc.defineColorTarget(0, TEXTURE_FORMAT_RGBA8, false);
    fbo.setDesc(desc);

    fbo.setColorTarget(RHIDevice::get()->backBuffer()->rtv(), 0);
    fbo.setDepthStencilTarget(mGDepth->dsv());
  }

  ctx.transitionBarrier(&mBVH->res(), RHIResource::State::ShaderResource);
  ctx.setGraphicsState(*graphicsState);
  progIns->apply(ctx, true);
  ctx.setFrameBuffer(fbo);

  ctx.draw(0, mBVH->elementCount());
}

void SceneRenderer::deferredLighting(RHIContext& ctx) {
  SCOPED_GPU_EVENT(ctx, "Deferred Lighting");

  static ComputeState::sptr_t computeState;

  if (!computeState) {
    ComputeState::Desc desc;
    desc.setRootSignature(gDeferredLighting->rootSignature());
    desc.setProgram(gDeferredLighting);
    computeState = ComputeState::create(desc);
  }

  
  {
    SCOPED_GPU_EVENT(ctx, "UpSamle, apply diffuse");
    ctx.setComputeState(*computeState);
  
    ctx.uavBarrier(mIndirectLight.get());
    mDSharedDescriptors->bindForCompute(ctx, *gDeferredLighting->rootSignature(), 0);
    mDGBufferDescriptors->bindForCompute(ctx, *gDeferredLighting->rootSignature(), 1);
    mDDeferredLightingDescriptors->bindForCompute(ctx, *gDeferredLighting->rootSignature(), 2);
  
    uint x = uint(Window::Get()->bounds().width()) / 8 + 1;
    uint y = uint(Window::Get()->bounds().height()) / 8 + 1;
  
    ctx.dispatch(x, y, 1);
    // ctx.resourceBarrier(mAO.get(), RHIResource::State::UnorderedAccess);
    // ctx.copyResource(*mScene, *RHIDevice::get()->backBuffer());
  }
}

void SceneRenderer::computeIndirectLighting(RHIContext & ctx) {
  SCOPED_GPU_EVENT(ctx, "Compute Indirect");
  static ComputeState::sptr_t computeStateIndirect;
  if (!computeStateIndirect) {
    ComputeState::Desc desc;
    desc.setRootSignature(gDeferredLighting_indirect->rootSignature());
    desc.setProgram(gDeferredLighting_indirect);
    computeStateIndirect = ComputeState::create(desc);
  }

  {
    ctx.setComputeState(*computeStateIndirect);
    ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
    ctx.transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
    ctx.transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
    ctx.transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);

    // ctx.resourceBarrier(mIndirectLight.get(), RHIResource::State::UnorderedAccess);
    
    mDSharedDescriptors->bindForCompute(ctx, *gDeferredLighting_indirect->rootSignature(), 0);
    mDGBufferDescriptors->bindForCompute(ctx, *gDeferredLighting_indirect->rootSignature(), 1);

    mDDeferredLightingIndirectDescriptors->bindForCompute(ctx, *gDeferredLighting_indirect->rootSignature(), 2);

    uint x = uint(mIndirectLight->width()) / 64 + 1;
    uint y = uint(mIndirectLight->height()) / 8 + 1;

    ctx.dispatch(x, y, 0xfff / (64));
    // ctx.dispatch(0xfff / (16*8), 1, 1);

  }
  
}

void SceneRenderer::setupFrame() {

  mSurfels[1] = mSurfelsBuffer[uint(mFrameData.frameCount) % 2];
  //  mSurfels[0] = mSurfelsBuffer[0];
  // mSurfels[1] = mSurfelsBuffer[1];

  mFrameData.frameCount++;
  mFrameData.time = (float)GetMainClock().total.second;

  // bool debugFrame = false;
  {
    ImGui::Begin("Configs");
    ImGui::SliderFloat("roughness", &mFrameData.roughness, 0.f, 1.f);
    // ImGui::SliderFloat("metallic", &mFrameData.metallic, 0.f, 1.f);
    // ImGui::Checkbox("Debug Frame", &debugFrame);
    ImGui::End();
  }
  //
  // if(debugFrame) {
  //   Debug::enableGpuRecord();
  // }

  mcFrameData->updateData(mFrameData);

  if (!mTargetScene.lights().empty()) {
    Light* l = mTargetScene.lights()[0];

    mcLight->updateData(l->info());

  }

  mSurfels[0] = mSurfelsBuffer[uint(mFrameData.frameCount) % 2];

  mGDepth = RHIDevice::get()->depthBuffer();


  auto ctx = RHIDevice::get()->defaultRenderContext();

  if(shouldRecomputeIndirect()) {
    ctx->clearRenderTarget(*mIndirectLight->rtv(), Rgba::black);
  }
  
  if(!Input::Get().isKeyDown('B')) {
    ctx->clearRenderTarget(*mGAlbedo->rtv(), Rgba::black);
    ctx->clearRenderTarget(*mGNormal->rtv(), Rgba::gray);
    ctx->clearRenderTarget(*mGPosition->rtv(), vec4{ NAN, NAN, NAN,NAN });
    ctx->clearRenderTarget(*mGVelocity->rtv(), Rgba(0, 0, 0, 0));

    ctx->transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
    ctx->transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
    ctx->transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
    ctx->transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);
  }
  ctx->clearDepthStencilTarget(*mGDepth->dsv(), true, true);

  ctx->transitionBarrier(mScene.get(), RHIResource::State::UnorderedAccess);

  updateDescriptors();
}

void SceneRenderer::setupView(RHIContext& ctx) {
  Camera& cam = *mTargetScene.camera();

  camera_t prev = mCameraData[(uint(mFrameData.frameCount) + 1) % 2];
  mCameraData[0] = cam.ubo();
  mCameraData[1] = prev;

  mcCamera->updateData(mCameraData, 0, sizeof(camera_t) * 2);

  aabb2 bounds = { vec2::zero, { Window::Get()->bounds().width(), Window::Get()->bounds().height()} };
  ctx.setViewport(bounds);
  ctx.setScissorRect(bounds);

}

void SceneRenderer::dumpSurfels(RHIContext& ctx) {
  uint32_t numSurfels;
  ctx.readBuffer(*mSurfels[0]->uavCounter(), &numSurfels, sizeof(uint32_t));

  if (numSurfels < 200) return;
  surfel_t* surfels = (surfel_t*)_alloca(sizeof(surfel_t)*numSurfels);

  ctx.readBuffer(*mSurfels[0], surfels, sizeof(surfel_t)*numSurfels);

  for(uint i = 0; i <numSurfels; i++)
    mSurfelDump << surfels[i].toString() << std::endl;

}

void SceneRenderer::pathTracing(RHIContext& ctx) {
  static Program::sptr_t prog;
  static S<ProgramInst> progIns;
  static ComputeState::sptr_t computeState;

  if(!prog) {
    prog = Program::sptr_t(new Program());

    prog->stage(SHADER_TYPE_COMPUTE)
      .setFromBinary(gPathTracing_cs, sizeof(gPathTracing_cs));
    prog->compile();

    progIns = ComputeProgramInst::create(prog);

    progIns->setCbv(*mcFrameData->cbv(), 0);
    progIns->setCbv(*mcCamera->cbv(), 1);
    progIns->setCbv(*mcLight->cbv(), 3);
    progIns->setSrv(*mGAlbedo->srv(), 10);
    progIns->setSrv(*mGNormal->srv(), 11);
    progIns->setSrv(*mGPosition->srv(), 12);
    progIns->setSrv(*mVertexData->srv(), 13);
    progIns->setSrv(*mBVH->srv(), 14);
    progIns->setUav(*mScene->uav(), 0); 


    ComputeState::Desc desc;
    desc.setRootSignature(prog->rootSignature());
    desc.setProgram(prog);
    computeState = ComputeState::create(desc);
  }

  ctx.transitionBarrier(mGAlbedo.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGNormal.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGPosition.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mGDepth.get(), RHIResource::State::NonPixelShader);
  ctx.transitionBarrier(mScene.get(), RHIResource::State::UnorderedAccess);

  ctx.setComputeState(*computeState);
  progIns->apply(ctx, false);

  uint x = uint(Window::Get()->bounds().width()) / 16 + 1;
  uint y = uint(Window::Get()->bounds().height()) / 16 + 1;

  ctx.dispatch(x, y, 1);
  ctx.copyResource(*mScene, *RHIDevice::get()->backBuffer());
}

void SceneRenderer::fxaa(RHIContext & ctx) {
  static Program::sptr_t prog;
  static S<ProgramInst> progIns;
  static GraphicsState::sptr_t graphicsState;

  if (!prog) {
    prog = Program::sptr_t(new Program());

    prog->stage(SHADER_TYPE_VERTEX)
      .setFromBinary(gfxaa_vs, sizeof(gfxaa_vs));
    prog->stage(SHADER_TYPE_FRAGMENT)
      .setFromBinary(gfxaa_ps, sizeof(gfxaa_ps));
    prog->compile();

    progIns = GraphicsProgramInst::create(prog);

    progIns->setSrv(*mScene->srv(), 0);

    GraphicsState::Desc desc;
    desc.setRootSignature(prog->rootSignature());
    desc.setProgram(prog);
    desc.setPrimTye(GraphicsState::PrimitiveType::Triangle);
    desc.setVertexLayout(VertexLayout::For<vertex_lit_t>());

    graphicsState = GraphicsState::create(desc);
  }

  FrameBuffer fbo;
  {
    FrameBuffer::Desc desc;
    desc.defineColorTarget(0, TEXTURE_FORMAT_RGBA8, false);
    fbo.setDesc(desc);

    fbo.setColorTarget(RHIDevice::get()->backBuffer()->rtv(), 0);
  }
  ctx.setFrameBuffer(fbo);

  ctx.transitionBarrier(mScene.get(), RHIResource::State::ShaderResource);

  ctx.setGraphicsState(*graphicsState);
  progIns->apply(ctx, false);

  ctx.draw(0, 3);

}

bool SceneRenderer::shouldRecomputeIndirect() const {
  // return uint(mFrameData.frameCount) % 2 == 0;
  return true;
}

DEF_RESOURCE(Program, "internal/Shader/scene-renderer/bvhVisual") {
  Program::sptr_t prog = Program::sptr_t(new Program());

  prog->stage(SHADER_TYPE_VERTEX).setFromBinary(gBvhVisual_vs, sizeof(gBvhVisual_vs));
  prog->stage(SHADER_TYPE_GEOMETRY).setFromBinary(gBvhVisual_gs, sizeof(gBvhVisual_gs));
  prog->stage(SHADER_TYPE_FRAGMENT).setFromBinary(gBvhVisual_ps, sizeof(gBvhVisual_ps));
  prog->compile();

  RenderState state;
  state.isWriteDepth = FLAG_FALSE;
  state.depthMode = COMPARE_ALWAYS;
  // state.depthMode = COMPARE_LEQUAL;
  state.colorBlendOp = BLEND_OP_ADD;
  state.colorSrcFactor = BLEND_F_SRC_ALPHA;
  state.colorDstFactor = BLEND_F_DST_ALPHA;
  state.alphaBlendOp = BLEND_OP_ADD;
  state.alphaSrcFactor = BLEND_F_SRC_ALPHA;
  state.alphaDstFactor = BLEND_F_DST_ALPHA;
  state.cullMode = CULL_NONE;
  prog->setRenderState(state);

  return S<Program>(prog);
}

