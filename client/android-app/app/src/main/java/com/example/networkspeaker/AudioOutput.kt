package com.example.networkspeaker

import android.util.Log
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack

object AudioOutput {
    private const val logTag = "NetworkSpeaker"
    private const val sampleRate = 48_000
    private const val channels = 2
    private const val writeLogInterval = 50
    private var writeCount = 0

    private val minBufferSize = AudioTrack.getMinBufferSize(
        sampleRate,
        AudioFormat.CHANNEL_OUT_STEREO,
        AudioFormat.ENCODING_PCM_FLOAT
    )

    private val audioTrack: AudioTrack by lazy {
        AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build()
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setEncoding(AudioFormat.ENCODING_PCM_FLOAT)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_STEREO)
                    .build()
            )
            .setTransferMode(AudioTrack.MODE_STREAM)
            .setBufferSizeInBytes(minBufferSize * 4)
            .build()
    }

    @Synchronized
    fun start() {
        if (audioTrack.state == AudioTrack.STATE_INITIALIZED) {
            audioTrack.play()
            Log.i(logTag, "AudioTrack started sampleRate=$sampleRate channels=$channels")
        }
    }

    @Synchronized
    fun stop() {
        if (audioTrack.playState == AudioTrack.PLAYSTATE_PLAYING) {
            audioTrack.pause()
            audioTrack.flush()
            Log.i(logTag, "AudioTrack stopped after writes=$writeCount")
        }
        writeCount = 0
    }

    @Synchronized
    fun write(interleaved: FloatArray, sampleCount: Int) {
        if (audioTrack.playState != AudioTrack.PLAYSTATE_PLAYING) {
            start()
        }
        audioTrack.write(interleaved, 0, sampleCount * channels, AudioTrack.WRITE_BLOCKING)
        writeCount += 1
        if (writeCount == 1 || writeCount % writeLogInterval == 0) {
            Log.i(logTag, "PCM write #$writeCount samplesPerChannel=$sampleCount")
        }
    }
}
