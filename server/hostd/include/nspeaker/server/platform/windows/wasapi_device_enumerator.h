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

[[nodiscard]] std::vector<AudioDeviceInfo> EnumerateAudioRenderDevices();

}  // namespace nspeaker::server
