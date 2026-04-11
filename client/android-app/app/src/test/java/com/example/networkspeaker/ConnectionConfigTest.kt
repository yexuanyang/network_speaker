package com.example.networkspeaker

import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertIs
import kotlin.test.assertTrue

class ConnectionConfigTest {
    @Test
    fun `parsePort accepts valid range`() {
        assertEquals(50000, ConnectionConfig.parsePort("50000"))
        assertEquals(1, ConnectionConfig.parsePort("1"))
        assertEquals(65535, ConnectionConfig.parsePort("65535"))
    }

    @Test
    fun `parsePort rejects invalid values`() {
        assertEquals(null, ConnectionConfig.parsePort(""))
        assertEquals(null, ConnectionConfig.parsePort("0"))
        assertEquals(null, ConnectionConfig.parsePort("65536"))
        assertEquals(null, ConnectionConfig.parsePort("abc"))
    }

    @Test
    fun `isValidIpv4 matches dotted quad only`() {
        assertTrue(ConnectionConfig.isValidIpv4("192.168.1.10"))
        assertTrue(ConnectionConfig.isValidIpv4("0.0.0.0"))
        assertFalse(ConnectionConfig.isValidIpv4("256.1.1.1"))
        assertFalse(ConnectionConfig.isValidIpv4("192.168.1"))
        assertFalse(ConnectionConfig.isValidIpv4("host.local"))
    }

    @Test
    fun `validate trims sender host`() {
        val result = ConnectionConfig(" 192.168.1.10 ", 50000).validate()
        val valid = assertIs<ValidationResult.Valid>(result)
        assertEquals("192.168.1.10", valid.config.senderHost)
        assertEquals(50000, valid.config.listenPort)
    }

    @Test
    fun `validate rejects bad sender host`() {
        val result = ConnectionConfig("300.1.1.1", 50000).validate()
        val invalid = assertIs<ValidationResult.Invalid>(result)
        assertEquals(ValidationError.INVALID_SENDER_IPV4, invalid.error)
    }

    @Test
    fun `describeStatus uses filtered or any sender text`() {
        val strings = object : StatusStrings {
            override fun listeningAny(port: Int): String = "any:$port"
            override fun listeningFiltered(port: Int, senderHost: String): String = "one:$port:$senderHost"
        }

        assertEquals("any:50000", ConnectionConfig("", 50000).describeStatus(strings))
        assertEquals(
            "one:50000:192.168.1.10",
            ConnectionConfig("192.168.1.10", 50000).describeStatus(strings)
        )
    }
}
