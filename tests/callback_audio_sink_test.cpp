#include <gtest/gtest.h>

#include "nspeaker/client/callback_audio_sink.h"

TEST(CallbackAudioSinkTest, InvokesProvidedCallback) {
    bool called = false;
    nspeaker::client::CallbackAudioSink sink([&called](const nspeaker::audio::PcmFrame& frame) {
        called = true;
        return frame.samples_per_channel == 480;
    });

    nspeaker::audio::PcmFrame frame;
    frame.samples_per_channel = 480;
    EXPECT_TRUE(sink.SubmitPcm(frame));
    EXPECT_TRUE(called);
}
