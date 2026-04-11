package com.example.networkspeaker

import android.content.Context
import android.net.ConnectivityManager
import android.os.Bundle
import android.widget.Button
import android.widget.EditText
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import androidx.core.widget.doAfterTextChanged
import java.net.Inet4Address

class MainActivity : AppCompatActivity() {
    private lateinit var senderHostInput: EditText
    private lateinit var listenPortInput: EditText
    private lateinit var routeHintView: TextView
    private lateinit var statusView: TextView

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        senderHostInput = findViewById(R.id.senderHostInput)
        listenPortInput = findViewById(R.id.listenPortInput)
        routeHintView = findViewById(R.id.routeHintView)
        statusView = findViewById(R.id.statusView)

        val savedConfig = loadConfig()
        senderHostInput.setText(savedConfig.senderHost)
        listenPortInput.setText(savedConfig.listenPort.toString())

        listenPortInput.doAfterTextChanged {
            listenPortInput.error = null
            refreshRouteHint()
        }
        senderHostInput.doAfterTextChanged {
            senderHostInput.error = null
        }

        findViewById<Button>(R.id.startButton).setOnClickListener {
            startReceiver()
        }
        findViewById<Button>(R.id.stopButton).setOnClickListener {
            startService(SpeakerService.createStopIntent(this))
            statusView.text = getString(R.string.status_stopped)
        }

        refreshRouteHint()
        statusView.text = getString(R.string.status_stopped)
    }

    private fun startReceiver() {
        val listenPort = parseListenPort() ?: return
        val config = ConnectionConfig(
            senderHost = senderHostInput.text.toString(),
            listenPort = listenPort
        )
        when (val validation = config.validate()) {
            is ValidationResult.Invalid -> {
                when (validation.error) {
                    ValidationError.INVALID_PORT -> {
                        listenPortInput.error = getString(R.string.invalid_port)
                    }
                    ValidationError.INVALID_SENDER_IPV4 -> {
                        senderHostInput.error = getString(R.string.invalid_sender_ipv4)
                    }
                }
                return
            }
            is ValidationResult.Valid -> {
                saveConfig(validation.config)
                ContextCompat.startForegroundService(
                    this,
                    SpeakerService.createStartIntent(
                        this,
                        validation.config.senderHost,
                        validation.config.listenPort
                    )
                )
                statusView.text = validation.config.describeStatus(resourceStatusStrings)
            }
        }
    }

    private fun parseListenPort(): Int? {
        val value = ConnectionConfig.parsePort(listenPortInput.text.toString())
        if (value == null) {
            listenPortInput.error = getString(R.string.invalid_port)
            return null
        }
        return value
    }

    private fun refreshRouteHint() {
        val port = ConnectionConfig.parsePort(listenPortInput.text.toString())
            ?: ConnectionConfig.DEFAULT_PORT
        val ipv4 = currentIpv4Address()
        routeHintView.text = if (ipv4 != null) {
            getString(R.string.route_hint_ready, ipv4, port)
        } else {
            getString(R.string.route_hint_unavailable, port)
        }
    }

    private fun currentIpv4Address(): String? {
        return runCatching {
            val connectivityManager = getSystemService(ConnectivityManager::class.java) ?: return null
            val activeNetwork = connectivityManager.activeNetwork ?: return null
            val linkProperties = connectivityManager.getLinkProperties(activeNetwork) ?: return null
            linkProperties.linkAddresses
                .asSequence()
                .mapNotNull { it.address as? Inet4Address }
                .firstOrNull { !it.isLoopbackAddress }
                ?.hostAddress
        }.getOrNull()
    }

    private fun loadConfig(): ConnectionConfig {
        val preferences = getSharedPreferences(preferencesName, Context.MODE_PRIVATE)
        return ConnectionConfig(
            senderHost = preferences.getString(senderHostKey, "") ?: "",
            listenPort = preferences.getInt(listenPortKey, ConnectionConfig.DEFAULT_PORT)
        )
    }

    private fun saveConfig(config: ConnectionConfig) {
        getSharedPreferences(preferencesName, Context.MODE_PRIVATE)
            .edit()
            .putString(senderHostKey, config.senderHost)
            .putInt(listenPortKey, config.listenPort)
            .apply()
    }

    private val resourceStatusStrings = object : StatusStrings {
        override fun listeningAny(port: Int): String {
            return getString(R.string.status_listening_any, port)
        }

        override fun listeningFiltered(port: Int, senderHost: String): String {
            return getString(R.string.status_listening_filtered, port, senderHost)
        }
    }

    companion object {
        private const val preferencesName = "connection_settings"
        private const val senderHostKey = "sender_host"
        private const val listenPortKey = "listen_port"
    }
}
