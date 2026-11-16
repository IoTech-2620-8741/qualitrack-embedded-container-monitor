/**
 * @file secrets.example.h
 * @brief Settings of one Container Monitor node.
 *
 * Copy this file as `secrets.h` (ignored by Git) and set the values of the node. The sketch uses
 * `secrets.h` when it exists and this file otherwise, so the example values below run in Wokwi.
 *
 * - `DEVICE_ID` is the identifier (sensorExternalId) the quality manager registered for the
 *   container monitor in QualiTrack, and that the Edge registered with
 *   `flask --app app devices register <DEVICE_ID> CONTAINER_MONITOR`.
 * - `DEVICE_API_KEY` is the key that command printed once. Never commit a real key.
 * - `AUTHORIZED_TAGS` are the UIDs of the RFID tags of the staff allowed to open the container.
 */
#ifndef CONTAINER_MONITOR_SECRETS_H
#define CONTAINER_MONITOR_SECRETS_H


/// Wi-Fi network of the local station ("Wokwi-GUEST" in the simulator).
static constexpr const char* WIFI_SSID = "Wokwi-GUEST";
/// Wi-Fi password (empty for "Wokwi-GUEST").
static constexpr const char* WIFI_PASSWORD = "";

/// Base URL of the QualiTrack Edge Service of the facility, without a trailing slash.
static constexpr const char* EDGE_BASE_URL = "http://192.168.1.50:5000";
/// Identifier of the node registered in QualiTrack and in the Edge.
static constexpr const char* DEVICE_ID = "CNT-001";
/// API key the Edge issued for the node.
static constexpr const char* DEVICE_API_KEY = "replace-with-the-api-key-of-the-node";

/// POSIX time zone of the clock; the Edge receives UTC times, so UTC is enough.
static constexpr const char* TIME_ZONE = "UTC0";
/// NTP server of the clock.
static constexpr const char* NTP_SERVER = "pool.ntp.org";

/// UIDs of the tags of the authorized staff, as the RC522 reads them (hexadecimal bytes separated by ':').
static constexpr const char* AUTHORIZED_TAGS[] = {
    "DE:AD:BE:EF",
    "A1:B2:C3:D4"
};

/// Also read tags typed on the serial monitor (one UID per line): the Wokwi circuit has no RC522.
static constexpr bool SERIAL_TAG_INPUT = true;

#endif // CONTAINER_MONITOR_SECRETS_H
