﻿#include "RenderNodeContext.hpp"
#include "Engine/Debug/Log.hpp"
#include "Engine/Graphics/Program/ProgramInst.hpp"
#include "Engine/Graphics/RHI/VertexLayout.hpp"
#include "Engine/Graphics/Model/Vertex.hpp"
#include "Engine/Graphics/RHI/RHIContext.hpp"
#include "Engine/Renderer/RenderGraph/RenderNode.hpp"

RenderNodeContext::~RenderNodeContext() {
}

void RenderNodeContext::readSrv(std::string_view name, RHIResource::scptr_t res, uint registerIndex, uint registerSpace) {
  EXPECTS(mTargetProgram != nullptr);

  mTargetProgram->setSrv(*res->srv(), registerIndex, registerSpace);

  addBindingInfo(name, res, 
                 mForCompute 
                 ? RHIResource::State::NonPixelShader
                 : RHIResource::State::ShaderResource,
                 registerIndex, registerSpace);

}

void RenderNodeContext::readSrv(std::string_view name, ShaderResourceView& srv, uint registerIndex, uint registerSpace) {
  EXPECTS(mTargetProgram != nullptr);

  mTargetProgram->setSrv(srv, registerIndex, registerSpace);

  addBindingInfo(name, srv.res().lock(), 
                 mForCompute 
                 ? RHIResource::State::NonPixelShader
                 : RHIResource::State::ShaderResource,
                 registerIndex, registerSpace);
}

void RenderNodeContext::readCbv(std::string_view name, RHIResource::scptr_t res, uint registerIndex, uint registerSpace) {
  EXPECTS(mTargetProgram != nullptr);

  mTargetProgram->setCbv(*res->cbv(), registerIndex, registerSpace);

  addBindingInfo(name, res, 
                 RHIResource::State::ConstantBuffer,
                 registerIndex, registerSpace);
}

void RenderNodeContext::writeRtv(std::string_view name, RHIResource::scptr_t res, uint registerIndex) {
  EXPECTS(mTargetProgram != nullptr);

  addBindingInfo(name, res,
                 RHIResource::State::RenderTarget,
                 registerIndex, 0);
}

void RenderNodeContext::writeDsv(std::string_view name, RHIResource::scptr_t res) {
  EXPECTS(mTargetProgram != nullptr);
  addBindingInfo(name, res,
                 RHIResource::State::DepthStencil,
                 0, 0);
}

void RenderNodeContext::readWriteUav(std::string_view name, RHIResource::scptr_t tex, uint registerIndex, uint registerSpace) {
  EXPECTS(mTargetProgram != nullptr);

  mTargetProgram->setUav(*tex->uav(), registerIndex, registerSpace);

  addBindingInfo(name, tex, 
                 RHIResource::State::UnorderedAccess,
                 registerIndex, registerSpace);
}

void RenderNodeContext::reset(Program::scptr_t prog, bool forCompute) {
  mPiplineState = std::monostate();
  if(!mForCompute) {
    mFrameBuffer = FrameBuffer();
  }

  ProgramInst* inst = nullptr;

  if(forCompute) {
    inst = new ComputeProgramInst(prog);
  } else {
    inst = new GraphicsProgramInst(prog);
  }

  mForCompute = forCompute;
  mTargetProgram.reset(inst);
  mBindingInfos.clear();
}

RenderEdge::BindingInfo* RenderNodeContext::find(std::string_view name) {
  if(auto kv = mBindingInfos.find(name); kv != mBindingInfos.end()) {
    return &(kv->second);
  }
  Log::logf("Fail to find resource `%s`", name);
  DEBUGBREAK;
  return nullptr;
}

bool RenderNodeContext::exists(RenderEdge::BindingInfo* info) const {
  for(const auto& [k, v]: mBindingInfos) {
    if(&v == info) return true;
  }

  return false;
}

void RenderNodeContext::compile() {
  if(mForCompute) {
    compileForCompute();
  } else {
    compileForGraphics();
  }
}

void RenderNodeContext::apply(RHIContext& ctx) const {
  if(mForCompute) {
    ctx.setComputeState(*std::get<ComputeState::sptr_t>(mPiplineState));
  } else {
    ctx.setGraphicsState(*std::get<GraphicsState::sptr_t>(mPiplineState));
    ctx.setFrameBuffer(mFrameBuffer);
  }
  mTargetProgram->apply(ctx, true);
}

void RenderNodeContext::addBindingInfo(std::string_view name, RHIResource::scptr_t res, RHIResource::State state, uint registerIndex, uint registerSpace) {
  
  RenderEdge::BindingInfo info;

  info.res = res;
  info.state = state;

  info.regIndex = registerIndex;
  info.regSpace = registerSpace;

  info.cbv = nullptr;
  switch(state) { 
    case RHIResource::State::ConstantBuffer:
      info.cbv = res->cbv();
    break;
    case RHIResource::State::RenderTarget:
      info.rtv = res->rtv();
    break;
    case RHIResource::State::UnorderedAccess:
      info.uav = res->uav();
    break;
    case RHIResource::State::DepthStencil:
      info.dsv = res->dsv();
    break;
    case RHIResource::State::ShaderResource: 
    case RHIResource::State::NonPixelShader: 
      info.srv = res->srv();
    break;
    
    case RHIResource::State::PreInitialized:
    case RHIResource::State::Undefined:
    case RHIResource::State::Common:
    case RHIResource::State::VertexBuffer:
    case RHIResource::State::IndexBuffer:
    case RHIResource::State::StreamOut:
    case RHIResource::State::IndirectArg:
    case RHIResource::State::CopyDest:
    case RHIResource::State::CopySource:
    case RHIResource::State::ResolveDest:
    case RHIResource::State::ResolveSource:
    case RHIResource::State::Present:
    case RHIResource::State::GenericRead:
    case RHIResource::State::Predication:
      BAD_CODE_PATH();
    break;
  }

  if(mBindingInfos.find(name) != mBindingInfos.end()) {
    Log::logf("the resource already exists", name.data());
  }

  mBindingInfos[std::string(name)] = info;
}

void RenderNodeContext::compileForCompute() {
  ComputeState::Desc desc;

  desc.setProgram(mTargetProgram->prog());
  desc.setRootSignature(mTargetProgram->prog()->rootSignature());

  auto computeState = ComputeState::create(desc);
  mPiplineState = computeState;
  computeState->handle()->SetName(make_wstring(mOwner->name()).c_str());
}

void RenderNodeContext::compileForGraphics() {
  GraphicsState::Desc desc;

  desc.setProgram(mTargetProgram->prog());
  desc.setRootSignature(mTargetProgram->prog()->rootSignature());

  // TODO: this is hard coded here, I actually need bind Vbo/Mesh here
  desc.setPrimTye(GraphicsState::PrimitiveType::Triangle);
  desc.setVertexLayout(VertexLayout::For<vertex_lit_t>());

  for(auto& [name, info]: mBindingInfos) {
    if(info.state == RHIResource::State::RenderTarget) {
      // TODO: Making too specific assumption here
      mFrameBuffer.defineColorTarget(
        std::static_pointer_cast<const Texture2>(info.res), info.regIndex);
    }
    if(info.state == RHIResource::State::DepthStencil) {
      mFrameBuffer.defineDepthStencilTarget(
        std::static_pointer_cast<const Texture2>(info.res));
    }
  }
  desc.setFboDesc(mFrameBuffer.desc());

  auto graphicsState = GraphicsState::create(desc);
  mPiplineState = graphicsState;
  graphicsState->handle()->SetName(make_wstring(mOwner->name()).c_str());
  
}
