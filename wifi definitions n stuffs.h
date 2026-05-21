#pragma once
#include "Arduino.h"
#include "config.h"
#include "vector"
#include "map"
#include "sd stuffs.h"
#include "esp_wifi.h"

// wifi.h is too packed so here we are

using namespace std;
using Mac = array<uint8_t, 6>;

namespace packetCounts {
	unsigned long packetCount, beaconCount, deauthCount, probeCount, dataCount, eapolCount;
	int rssiTmp, rssi;

	unsigned long droppedPackets;
}

struct STA_Info {
	Mac mac;
	int rssiTmp = 0, rssi = 0; // rssiTmp is used to calc avg rssi

	struct packetCounts {
		int packetCount = 0;
		int packetsFromAP = 0;
		int packetsToAP = 0;
		unsigned long lastPacketUpdate = 0;
	} packetCounts;
};

struct AP_Info {
	char ssid[AP_SSID_MAX_LEN + 1] = {0};
	Mac bssid;
	uint8_t channel = 0;
	vector<STA_Info> STAs;
	int rssiTmp = 0, rssi = 0; // rssiTmp is used to calc avg rssi
	
	struct wpa3Stuffs {
		// -1 : open
		// 1: WPA3 only, 2: WPA2+WPA3(possible for wpa2 downgrade attack)
		// 3, 4: finished processing beacons(for convenience and prevent access while processing)
		int hasWPA3 = 0; 
		uint8_t* CSAbeacon = NULL;
		size_t CSAbeaconLen = 0;
		uint8_t* WPA2beacon = NULL;
		size_t WPA2beaconLen = 0;
	} wpa3Stuffs;

	struct packetCounts {
		int packetCount = 0;
		int m1 = 0;
		int m2 = 0;
		int m3 = 0;
		int m4 = 0;
		unsigned long lastPacketUpdate = 0;
	} packetCounts;
};

vector<AP_Info> APs;
vector<uint8_t> channels;
size_t channelIndex = 0;

bool enableSD;
bool debugPauseEnabled;
unsigned long startTime = millis(); // ignore this


bool deauthActive;

extern "C" int ieee80211_raw_frame_sanity_check(int32_t a, int32_t b, int32_t c) { return 0; }

void printDebugWithTime(const char* name) {
	sprite.printf("%s in %lums\n\n", name, millis() - startTime);
	sprite.pushSprite(0, 0);
	startTime = millis();
}

inline uint16_t colorizeRSSI(int rssi) {
	return rssi > -60 ? 0x2FE5 : rssi > -70 ? 0xFFE0 : rssi > -85 ? 0xFDA0 : 0xF800; // TODO:
}

inline uint16_t colorizeAP(int hasWPA3) {
	if (hasWPA3 == 1 || hasWPA3 == 2) return COLOR_AP_BEACON_NOT_CAPTURED;
	else if (hasWPA3 == 3) return COLOR_AP_WPA3_BEACON_CAPTURED;
	else if (hasWPA3 == 4) return COLOR_AP_WPA23_BEACON_CAPTURED;
	else if (hasWPA3 == -1) return COLOR_AP_OPEN;
	else return COLOR_AP_WPA2;
}

void waitForAnyButtonPress() {
	int pressed = 69; // yes this is b4 67 shit

	while (pressed == 69) {
		if (!digitalRead(21)) pressed = 21;
		else if (!digitalRead(22)) pressed = 22;
		delay(20);
	}

	while (!digitalRead(pressed)) delay(10);
}

inline bool isBroadcast(const Mac mac) {
	const Mac broadcastMac = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
	return mac == broadcastMac;
}
inline bool macInvalid(const Mac mac) { // it will always be inverted afterward so...
	const Mac zeroMac = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
	return mac == zeroMac || isBroadcast(mac) || mac[0] & 0x01;
}
void sendDeauth(Mac bssid) {
	uint8_t deauth_frame[26] = {0xC0, 0x00, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, MAC2STR(bssid.data()), MAC2STR(bssid.data()), 0x00, 0x00, 0x02, 0x00};
	ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_80211_tx(WIFI_IF_STA, deauth_frame, sizeof(deauth_frame), false));
}

Mac getStaMac(const Mac macFrom, const Mac macTo, const Mac bssid) { // only mgmt
	if (macTo == bssid) return macFrom; // from sta to ap
	else if (macFrom == bssid) return macTo; // from ap to sta
	else if (isBroadcast(bssid) || isBroadcast(macTo)) return macFrom; // from sta to broadcast
	else return {0xBA, 0xAD, 0xF0, 0x0D, 0x12, 0x34};
}
// @warning ONLY 1 CALL PER STATEMENT!!!
const char* formatMac(Mac mac) {
    thread_local char result[18];
    snprintf(result, sizeof(result), MACSTR, MAC2STR(mac));
    for (int i = 0; result[i]; i++) result[i] = toupper(result[i]);
    return result;
}

// @warning ONLY 1 CALL PER STATEMENT!!!
const char* BSSID2NAME(const Mac& mac, bool checkSta = true) {
    thread_local char buffer1[128];
    thread_local char buffer2[64];
    
	auto it = find_if(APs.begin(), APs.end(), [&](const AP_Info& ap){ return ap.bssid == mac; });
    if (it != APs.end()) {
        const char* ssid = it->ssid;
        size_t len = strlen(ssid);
        if (len >= sizeof(buffer1)) len = sizeof(buffer1) - 1;
        memcpy(buffer1, ssid, len);
        buffer1[len] = 0;
        return buffer1;
    }

    // if (checkSta) {
    //     for (const auto& ap_pair : AP_Map) {
    //         const auto& ap = ap_pair.second;
    //         for (const auto& sta : ap.STAs) {
    //             if (sta.mac == mac) {
    //                 snprintf(buffer2, sizeof(buffer2), MACSTR " ST", MAC2STR(mac));
    //                 for (int i = 0; buffer2[i]; i++) buffer2[i] = toupper(buffer2[i]);
    //                 return buffer2;
    //             }
    //         }
    //     }
    // }
    return formatMac(mac);
}
void addSTA(AP_Info &ap, Mac mac, int rssi) {
	STA_Info sta;
	sta.mac = mac;
	sta.packetCounts.packetCount++;

	sta.packetCounts.packetsToAP = 1; // TODO: impliment proper direction detection

	sta.rssiTmp = rssi;
	ap.STAs.push_back(sta);
	log1(COLOR_STA_ADDED, "+ %s AP: %s", BSSID2NAME(mac, false), ap.ssid);
}

