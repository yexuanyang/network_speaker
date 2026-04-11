#include "nspeaker/server/platform/windows/wasapi_loopback_capture.h"

namespace nspeaker::server {

bool WasapiLoopbackCapture::Start() {
    return false;
}

bool WasapiLoopbackCapture::ReadFrame(audio::PcmFrame& out) {
    static_cast<void>(out);
    return false;
}

}  // namespace nspeaker::server
