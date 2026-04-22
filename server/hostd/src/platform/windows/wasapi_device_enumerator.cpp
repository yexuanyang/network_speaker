#include "nspeaker/server/platform/windows/wasapi_device_enumerator.h"

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <functiondiscoverykeys_devpkey.h>
#include <mmdeviceapi.h>
#include <windows.h>
#include <wrl/client.h>
#include <wtypes.h>  // DEFINE_PROPERTYKEY, PROPERTYKEY

#include <memory>

namespace nspeaker::server {
namespace {

using Microsoft::WRL::ComPtr;

struct CoTaskMemDeleter {
    void operator()(LPWSTR ptr) const noexcept {
        if (ptr != nullptr) {
            CoTaskMemFree(ptr);
        }
    }
};

using CoTaskWString = std::unique_ptr<WCHAR, CoTaskMemDeleter>;

std::string WideToUtf8(const WCHAR* wide) {
    if (wide == nullptr || wide[0] == L'\0') {
        return {};
    }
    const int len = WideCharToMultiByte(CP_UTF8, 0, wide, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(len - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, result.data(), len, nullptr, nullptr);
    return result;
}

std::string GetDeviceProperty(IMMDevice* device, const PROPERTYKEY& key) {
    ComPtr<IPropertyStore> store;
    if (FAILED(device->OpenPropertyStore(STGM_READ, store.GetAddressOf()))) {
        return {};
    }
    PROPVARIANT pv;
    PropVariantInit(&pv);
    if (FAILED(store->GetValue(key, &pv))) {
        PropVariantClear(&pv);
        return {};
    }
    std::string result;
    if (pv.vt == VT_LPWSTR && pv.pwszVal != nullptr) {
        result = WideToUtf8(pv.pwszVal);
    }
    PropVariantClear(&pv);
    return result;
}

std::string GetDeviceId(IMMDevice* device) {
    LPWSTR raw_id = nullptr;
    if (FAILED(device->GetId(&raw_id))) {
        return {};
    }
    CoTaskWString id_guard(raw_id);
    return WideToUtf8(raw_id);
}

}  // namespace

std::vector<AudioDeviceInfo> EnumerateAudioRenderDevices() {
    std::vector<AudioDeviceInfo> devices;

    const HRESULT init_result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(init_result) && init_result != RPC_E_CHANGED_MODE) {
        return devices;
    }
    const bool should_uninitialize = init_result == S_OK || init_result == S_FALSE;

    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                                IID_PPV_ARGS(enumerator.GetAddressOf())))) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        return devices;
    }

    std::string default_id;
    {
        ComPtr<IMMDevice> default_device;
        if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eMultimedia,
                                                          default_device.GetAddressOf()))) {
            default_id = GetDeviceId(default_device.Get());
        }
    }

    ComPtr<IMMDeviceCollection> collection;
    if (FAILED(enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE,
                                              collection.GetAddressOf()))) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        return devices;
    }

    UINT count = 0;
    if (FAILED(collection->GetCount(&count))) {
        if (should_uninitialize) {
            CoUninitialize();
        }
        return devices;
    }

    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        if (FAILED(collection->Item(i, device.GetAddressOf()))) {
            continue;
        }

        AudioDeviceInfo info;
        info.id = GetDeviceId(device.Get());
        if (info.id.empty()) {
            continue;
        }
        info.name = GetDeviceProperty(device.Get(), PKEY_Device_FriendlyName);
        info.description = GetDeviceProperty(device.Get(), PKEY_DeviceInterface_FriendlyName);
        info.is_default = (info.id == default_id);
        devices.push_back(std::move(info));
    }

    if (should_uninitialize) {
        CoUninitialize();
    }
    return devices;
}

}  // namespace nspeaker::server

#endif  // _WIN32
