package com.example.networkspeaker

data class ConnectionConfig(
    val senderHost: String,
    val listenPort: Int,
) {
    fun normalized(): ConnectionConfig {
        return copy(senderHost = senderHost.trim())
    }

    fun validate(): ValidationResult {
        val normalizedHost = senderHost.trim()
        if (listenPort !in MIN_PORT..MAX_PORT) {
            return ValidationResult.Invalid(ValidationError.INVALID_PORT)
        }
        if (normalizedHost.isNotEmpty() && !isValidIpv4(normalizedHost)) {
            return ValidationResult.Invalid(ValidationError.INVALID_SENDER_IPV4)
        }
        return ValidationResult.Valid(copy(senderHost = normalizedHost))
    }

    fun describeStatus(strings: StatusStrings): String {
        val normalizedHost = senderHost.trim()
        return if (normalizedHost.isBlank()) {
            strings.listeningAny(listenPort)
        } else {
            strings.listeningFiltered(listenPort, normalizedHost)
        }
    }

    companion object {
        const val DEFAULT_PORT = 50000
        private const val MIN_PORT = 1
        private const val MAX_PORT = 65_535

        fun parsePort(rawValue: String): Int? {
            val value = rawValue.trim().toIntOrNull() ?: return null
            return value.takeIf { it in MIN_PORT..MAX_PORT }
        }

        fun isValidIpv4(value: String): Boolean {
            val parts = value.split('.')
            return parts.size == 4 && parts.all { part ->
                part.isNotEmpty() && part.toIntOrNull()?.let { it in 0..255 } == true
            }
        }
    }
}

sealed interface ValidationResult {
    data class Valid(val config: ConnectionConfig) : ValidationResult
    data class Invalid(val error: ValidationError) : ValidationResult
}

enum class ValidationError {
    INVALID_PORT,
    INVALID_SENDER_IPV4,
}

interface StatusStrings {
    fun listeningAny(port: Int): String
    fun listeningFiltered(port: Int, senderHost: String): String
}
