package com.example.networkspeaker

import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationCompat
import androidx.core.content.getSystemService

class SpeakerService : Service() {
    private val logTag = "NetworkSpeaker"

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        return when (intent?.action) {
            ACTION_START -> {
                val host = intent?.getStringExtra(EXTRA_HOST).orEmpty().trim()
                val port = intent?.getIntExtra(EXTRA_PORT, DEFAULT_PORT) ?: DEFAULT_PORT
                Log.i(logTag, "Starting receiver hostFilter=${host.ifBlank { "<any>" }} port=$port")
                startForeground(NOTIFICATION_ID, buildNotification(host, port))
                NativeBridge.nativeStop()
                AudioOutput.stop()
                AudioOutput.start()
                NativeBridge.nativeStart(host, port)
                START_REDELIVER_INTENT
            }
            ACTION_STOP -> {
                Log.i(logTag, "Stopping receiver by explicit action")
                stopReceiver()
                START_NOT_STICKY
            }
            else -> START_NOT_STICKY
        }
    }

    override fun onDestroy() {
        stopReceiver()
        super.onDestroy()
    }

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        ensureNotificationChannel()
    }

    private fun stopReceiver() {
        NativeBridge.nativeStop()
        AudioOutput.stop()
        stopForeground(STOP_FOREGROUND_REMOVE)
        Log.i(logTag, "Receiver stopped")
        stopSelf()
    }

    private fun buildNotification(host: String, port: Int): Notification {
        val contentIntent = PendingIntent.getActivity(
            this,
            0,
            Intent(this, MainActivity::class.java).addFlags(Intent.FLAG_ACTIVITY_SINGLE_TOP),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val stopIntent = PendingIntent.getService(
            this,
            1,
            createStopIntent(this),
            PendingIntent.FLAG_IMMUTABLE or PendingIntent.FLAG_UPDATE_CURRENT
        )
        val contentText = if (host.isBlank()) {
            getString(R.string.notification_listening_any, port)
        } else {
            getString(R.string.notification_listening_filtered, port, host)
        }
        return NotificationCompat.Builder(this, NOTIFICATION_CHANNEL_ID)
            .setSmallIcon(android.R.drawable.stat_sys_headset)
            .setContentTitle(getString(R.string.notification_title))
            .setContentText(contentText)
            .setStyle(NotificationCompat.BigTextStyle().bigText(contentText))
            .setContentIntent(contentIntent)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setCategory(NotificationCompat.CATEGORY_SERVICE)
            .addAction(
                0,
                getString(R.string.notification_stop_action),
                stopIntent
            )
            .build()
    }

    private fun ensureNotificationChannel() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) {
            return
        }
        val notificationManager = getSystemService<NotificationManager>() ?: return
        val channel = NotificationChannel(
            NOTIFICATION_CHANNEL_ID,
            getString(R.string.notification_channel_name),
            NotificationManager.IMPORTANCE_LOW
        ).apply {
            description = getString(R.string.notification_channel_description)
        }
        notificationManager.createNotificationChannel(channel)
    }

    companion object {
        private const val ACTION_START = "com.example.networkspeaker.action.START"
        private const val ACTION_STOP = "com.example.networkspeaker.action.STOP"
        private const val EXTRA_HOST = "host"
        private const val EXTRA_PORT = "port"
        private const val DEFAULT_PORT = ConnectionConfig.DEFAULT_PORT
        private const val NOTIFICATION_CHANNEL_ID = "receiver"
        private const val NOTIFICATION_ID = 1

        fun createServiceIntent(context: Context): Intent {
            return Intent(context, SpeakerService::class.java)
        }

        fun createStartIntent(context: Context, host: String, port: Int): Intent {
            return createServiceIntent(context)
                .setAction(ACTION_START)
                .putExtra(EXTRA_HOST, host)
                .putExtra(EXTRA_PORT, port)
        }

        fun createStopIntent(context: Context): Intent {
            return createServiceIntent(context).setAction(ACTION_STOP)
        }
    }
}
