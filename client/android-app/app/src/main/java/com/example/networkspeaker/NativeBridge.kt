package com.example.networkspeaker

object NativeBridge {
    init {
        System.loadLibrary("networkspeaker_android")
    }

    @JvmStatic
    external fun nativeStart(host: String, port: Int): Boolean
    @JvmStatic
    external fun nativeStop()

    @JvmStatic
    fun onPcmReady(interleaved: FloatArray, sampleCount: Int) {
        AudioOutput.write(interleaved, sampleCount)
    }
}
