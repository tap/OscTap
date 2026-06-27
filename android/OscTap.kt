/*
    OscTap demo: Kotlin facade for the Android app.

    Part of the Pi 5 <-> Pico 2W <-> Android integration tutorial
    (docs/INTEGRATION_PI5_PICO_ANDROID.md). The OScTap C++ core (via the JNI
    bridge in osctap_jni.cpp -> libosctap_jni.so) builds and parses OSC; the UDP
    transport is pure JVM (java.net.DatagramSocket), which is the idiomatic way
    to do networking in an Android app.

    Requires <uses-permission android:name="android.permission.INTERNET"/> in the
    manifest. Do socket I/O off the main thread (a coroutine / thread).
*/
package org.osctap.demo

import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetAddress

/** Native OSC (de)serialization, backed by the OscTap C++ core through JNI. */
object OscTap {
    init { System.loadLibrary("osctap_jni") }

    /** Build an OSC message. Each arg may be Int, Float, Boolean, or String. */
    external fun buildMessage(address: String, args: Array<Any>): ByteArray

    /** Parse one OSC *message* into a human-readable summary.
     *  Throws IllegalArgumentException on a malformed packet. */
    external fun describe(packet: ByteArray): String
}

/**
 * Minimal UDP OSC endpoint for the app. Construct once; call send() to talk to
 * the Pi 5 hub, and receive() in a loop on a background thread for telemetry.
 */
class OscUdp(private val socket: DatagramSocket = DatagramSocket()) {

    /** Fire one OSC message at host:port (e.g. the Pi 5 hub on 9000). */
    fun send(host: String, port: Int, address: String, vararg args: Any) {
        val bytes = OscTap.buildMessage(address, arrayOf(*args))
        socket.send(DatagramPacket(bytes, bytes.size, InetAddress.getByName(host), port))
    }

    /** Block for one inbound packet and return its parsed summary (or null if
     *  malformed). Call from a background thread bound to your telemetry port. */
    fun receive(): String? {
        val buf = ByteArray(1500)
        val pkt = DatagramPacket(buf, buf.size)
        socket.receive(pkt)
        return try {
            OscTap.describe(pkt.data.copyOf(pkt.length))
        } catch (e: IllegalArgumentException) {
            null // drop malformed datagram
        }
    }

    fun bind(port: Int): OscUdp = OscUdp(DatagramSocket(port))
    fun close() = socket.close()
}

/*
    Usage sketch (in a ViewModel / coroutine):

        val hub = "192.168.1.10"
        val tx = OscUdp()
        tx.send(hub, 9000, "/hub/led", 1)          // turn an LED on via the hub
        tx.send(hub, 9000, "/hub/pwm", 0.75f)       // set a PWM duty

        // telemetry: the hub relays /sensor/* to us re-addressed as /ui/*
        val rx = OscUdp(DatagramSocket(9001))
        thread {
            while (true) rx.receive()?.let { println("telemetry: $it") }
        }
*/
