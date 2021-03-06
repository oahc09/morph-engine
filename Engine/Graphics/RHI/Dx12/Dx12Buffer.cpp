#include "Engine/Graphics/RHI/RHI.hpp"
#include "Engine/Graphics/RHI/RHIBuffer.hpp"
#include "Engine/Graphics/RHI/Dx12/Dx12Resource.hpp"
#include "Engine/Graphics/RHI/RHIDevice.hpp"
#include "Engine/Debug/ErrorWarningAssert.hpp"

ID3D12ResourcePtr createBuffer(RHIBuffer::State initState, size_t size, const D3D12_HEAP_PROPERTIES& heapProps, RHIBuffer::BindingFlag bindFlags) {
  // Create the buffer
  D3D12_RESOURCE_DESC desc = {};
  desc.Alignment = 0;
  desc.DepthOrArraySize = 1;
  desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
  desc.Flags = asDx12ResourceFlags(bindFlags);
  desc.Format = DXGI_FORMAT_UNKNOWN;
  desc.Height = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
  desc.MipLevels = 1;
  desc.SampleDesc.Count = 1;
  desc.SampleDesc.Quality = 0;
  desc.Width = size;

  D3D12_RESOURCE_STATES dxState = asDx12ResourceState(initState);

  ID3D12ResourcePtr handle;
  d3d_call(RHIDevice::get()->nativeDevice()->CreateCommittedResource(&heapProps, D3D12_HEAP_FLAG_NONE, &desc, dxState, nullptr, IID_PPV_ARGS(&handle)));

  return handle;
}

const ConstantBufferView* RHIBuffer::cbv() const {
  if (!mCbv && is_set(mBindingFlags, BindingFlag::ConstantBuffer)) {
    mCbv = ConstantBufferView::create(shared_from_this());
  }

  return mCbv.get();
}

const UnorderedAccessView* RHIBuffer::uav(uint) const {
  if(!mUav && is_set(mBindingFlags, BindingFlag::UnorderedAccess)) {
    mUav = UnorderedAccessView::create(shared_from_this());
  }

  return mUav.get();
}

bool RHIBuffer::rhiInit(bool hasInitData) {
  if(mBindingFlags == BindingFlag::ConstantBuffer) {
    mSize = align_to(D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT, mSize);
  }

  if(mCpuAccess == CPUAccess::Write) {
    mState.global = true;
    mState.globalState = State::GenericRead;
    if(!hasInitData) {
      mRhiHandle = createBuffer(mState.globalState, mSize, UploadHeapProps, mBindingFlags);

    } else {
      mRhiHandle = createBuffer(mState.globalState, mSize, UploadHeapProps, mBindingFlags);
    }

  } else if(mCpuAccess == CPUAccess::Read && mBindingFlags == BindingFlag::None) {

    mState.globalState = State::CopyDest;
    mRhiHandle = createBuffer(mState.globalState, mSize, ReadbackHeapProps, mBindingFlags);

  } else {
    mState.globalState = State::Common;
#ifdef MORPH_DXR
    if (is_set(mBindingFlags, BindingFlag::AccelerationStructure)) mState = RHIResource::State::AccelerationStructure;
#endif
    mRhiHandle = createBuffer(mState.globalState, mSize, DefaultHeapProps, mBindingFlags);
  }

  mRhiHandle->SetName(L"Buffer");

  return true;
}

u64 RHIBuffer::gpuAddress() const {
  return mRhiHandle->GetGPUVirtualAddress();
}

void* RHIBuffer::map(MapType /*type*/) {
  TODO("switch map type");
  D3D12_RANGE r{ 0, mSize };
  void* data;

  d3d_call(mRhiHandle->Map(0, &r, &data));

  return data;
}

void RHIBuffer::unmap() {
  mRhiHandle->Unmap(0, nullptr);
}
