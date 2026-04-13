#pragma once

#include <string>
#include <vector>

namespace nspeaker::server {

struct AudioDeviceInfo {
    std::string id;
    std::string name;
    std::string description;
    bool is_default = false;
};

#ifdef _WIN32
[[nodiscard]] std::vector<AudioDeviceInfo> EnumerateAudioRenderDevices();
#else
inline std::vector<AudioDeviceInfo> EnumerateAudioRenderDevices() { return {}; }
#endif

}  // namespace nspeaker::server
