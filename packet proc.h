#pragma once
#include "Arduino.h"
#include "config.h"
#include "esp_wifi.h"
#include "wifi.h"

// circular dependency againnnnnnnnn, im tired of this
void insertCSA(uint8_t *buf, unsigned int *len, uint8_t newChannel, uint8_t switchCount);
void removeWPA3RSNTag(uint8_t *buf, unsigned int *len);

QueueHandle_t pktQueue;

const char* parseEapol(uint8_t *frame, AP_Info &ap) {
	Mac macTo;
	Mac macFrom;
	Mac bssid;
	memcpy(macTo.data(), frame + 4, 6);
	memcpy(macFrom.data(), frame + 10, 6);
	memcpy(bssid.data(), frame + 16, 6);

    if (macFrom == bssid) {
        if (memcmp("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", frame + 115, 16)) { // check if mic is all 0
            ap.packetCounts.m3++;
            return "M3";
        } else {
            ap.packetCounts.m1++;
            return "M1";
        }
    } else if (macTo == bssid) {
        if (memcmp("\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", frame + 51, 16)) { // check if nonce is all 0
            ap.packetCounts.m3++;
            return "M2";
        } else {
            ap.packetCounts.m1++;
            return "M4";
        }
    }
    return "M?";
}

void IRAM_ATTR addPkt(wifi_promiscuous_pkt_t *pkt) {
    wifi_promiscuous_pkt_t* packet;
	int totalSize = sizeof(wifi_pkt_rx_ctrl_t) + pkt->rx_ctrl.sig_len;
	packet = (wifi_promiscuous_pkt_t*)malloc(totalSize);
	assert(packet);
	memcpy(&packet->rx_ctrl, &pkt->rx_ctrl, sizeof(wifi_pkt_rx_ctrl_t));
	memcpy(packet->payload, pkt->payload, pkt->rx_ctrl.sig_len);

    if (!xQueueSendFromISR(pktQueue, &packet, NULL)) {
		packetCounts::droppedPackets++;
		if (packet) free(packet);
	}
}

void handle_packet(wifi_promiscuous_pkt_t *pkt) {
	if (pkt->rx_ctrl.rx_state != 0) return;
    uint8_t pktType = pkt->payload[0] & 0x0C;
    uint8_t pktSubtype = pkt->payload[0] & 0xF0;
	uint16_t pktlen = pkt->rx_ctrl.sig_len - 4;
	int rssi = pkt->rx_ctrl.rssi;

	Mac macTo;
	Mac macFrom;
	Mac bssid;
	memcpy(macTo.data(), pkt->payload + 4, 6);
	memcpy(macFrom.data(), pkt->payload + 10, 6);
	memcpy(bssid.data(), pkt->payload + 16, 6);

	const char* macToName = BSSID2NAME(macTo);
	const char* macFromName = BSSID2NAME(macFrom);
	const char* bssidName = BSSID2NAME(bssid);

	packetCounts::packetCount++;
	packetCounts::rssiTmp += rssi;

	addPktPcap(millis() / 1000, micros(), pktlen, pkt->payload);

	for (auto& ap : APs) if (memcmp(ap.bssid.data(), macFrom.data(), 6) == 0) {
		ap.packetCounts.packetCount++;
		ap.rssiTmp += rssi;
		break;
	};

	if (pktType == 0x04) return; // dont parse ctrl
    if (pktType == 0x00 ) { // management
        if (pktSubtype == 0x40) {
			packetCounts::probeCount++;
			if (isBroadcast(macTo)) log1(COLOR_PROBE_REQ, "Probe  Broadcast: %s", formatMac(macFrom));
			else log1(COLOR_PROBE_REQ, "Probe: %s -> %s", macFromName, macToName);

		}
		else if (pktSubtype == 0x50) {
			packetCounts::probeCount++;
			log1(COLOR_PROBE_RES, "Probe: %s -> %s", macFromName, macToName);
		}
		else if (pktSubtype == 0xB0 || pktSubtype == 0x00 || pktSubtype == 0x10) { // auth,assoc
			//remove sta bc its connecting
            for (auto& ap: APs) {
                auto& staList = ap.STAs;
                for (auto it = staList.begin(); it != staList.end(); ++it) {
                    if (it->mac == getStaMac(macTo, macFrom, bssid)) {
                        log1(COLOR_STA_CONNECTING, "~ %s AP: %s", BSSID2NAME(it->mac, false), bssidName);
                        staList.erase(it);
                        return;
                    }
                }
            }
			if (pktSubtype == 0xB0) log1(COLOR_AUTH, "%s Auth: %s -> %s", (pkt->payload[24] == 0x03 && pkt->payload[25] == 0x00) ? "S" : "W", macFromName, macToName);
			else log1(COLOR_ASSOC, "Assoc: %s -> %s", macFromName, macToName);
			return; // return to prevent adding sta when its connecting
		}
		else if (pktSubtype == 0x20 || pktSubtype == 0x30) { // reassoc
            for (auto& ap: APs) {
                auto& staList = ap.STAs;
                for (auto it = staList.begin(); it != staList.end(); ++it) {
                    if (it->mac == getStaMac(macTo, macFrom, bssid)) {
                        log1(COLOR_STA_RECONNECTING, "R~ %s AP: %s", BSSID2NAME(it->mac, false), bssidName);
                        staList.erase(it);
                        return;
                    }
                }
            }
			log1(COLOR_REASSOC, "Reassoc: %s -> %s", macFromName, macToName);
			return; // return to prevent adding sta when its reconnecting
		}
        else if (pktSubtype == 0xC0 || pktSubtype == 0xA0) { //deauth, dissoc
			packetCounts::deauthCount++;
			if (isBroadcast(macTo)) log1(COLOR_DEAUTH, "%s Broadcast: %s", ((pktSubtype == 0xC0) ? "Deauth" : "Dissoc"), macFromName);
			else log1(COLOR_DEAUTH, "%s: %s -> %s", ((pktSubtype == 0xC0) ? "Deauth" : "Dissoc"), macFromName, macToName);
		} 
        else if (pktSubtype == 0x80) {  // beacon 👍
			for (auto& ap: APs) {
				if (ap.wpa3Stuffs.hasWPA3 == 2 || ap.wpa3Stuffs.hasWPA3 == 3 && ap.bssid == bssid) {
					ap.wpa3Stuffs.CSAbeaconLen = pktlen;
					ap.wpa3Stuffs.CSAbeacon = (uint8_t*)malloc(pktlen);
					memcpy(ap.wpa3Stuffs.CSAbeacon, pkt->payload, pktlen);
					insertCSA(ap.wpa3Stuffs.CSAbeacon, &ap.wpa3Stuffs.CSAbeaconLen, ((ap.channel + 3 > 13) ? 1 : ap.channel + 3), 0);
					if (ap.wpa3Stuffs.hasWPA3 == 2) {
						ap.wpa3Stuffs.WPA2beaconLen = pktlen;
						ap.wpa3Stuffs.WPA2beacon = (uint8_t*)malloc(pktlen);
						memcpy(ap.wpa3Stuffs.WPA2beacon, pkt->payload, pktlen);
						removeWPA3RSNTag(ap.wpa3Stuffs.WPA2beacon, &ap.wpa3Stuffs.WPA2beaconLen);
					}
					ap.wpa3Stuffs.hasWPA3 = (ap.wpa3Stuffs.hasWPA3 == 2) ? 4 : 3;
				}
			}
			packetCounts::beaconCount++;
		}
    }

	// Process data frame
	if (pktType != 0x08 || pktlen < 28) return;

    if ((pkt->payload[30] == 0x88 && pkt->payload[31] == 0x8E) || (pkt->payload[32] == 0x88 && pkt->payload[33] == 0x8E)) { // eapol
		packetCounts::eapolCount++;
		for (auto& ap: APs) if ((ap.bssid ==  macFrom) || (ap.bssid ==  macTo)) {
			const char* text = parseEapol(pkt->payload, ap);
			log1(COLOR_EAPOL, "%s: %s -> %s", text, macToName, macFromName);
			log2(COLOR_EAPOL, "%s: %s -> %s", text, macToName, macFromName);
		}
		return; // i dont want it to add sta when its connecting so return
	}

	packetCounts::dataCount++;

	if (macInvalid(macTo) || macInvalid(macFrom)) return;

	// Adding sta
	for (auto& ap: APs) {
		if (bssid == ap.bssid && (ap.bssid == macTo || ap.bssid == macFrom)) {
			Mac targetMac = (ap.bssid == macTo) ? macFrom : macTo;
			for (auto& sta: ap.STAs) if (targetMac == sta.mac) {
				sta.packetCounts.packetCount++;
				(ap.bssid == macTo ? sta.packetCounts.packetsFromAP : sta.packetCounts.packetsToAP)++;
				sta.rssiTmp += rssi;
				return;
			}
			addSTA(ap, targetMac, rssi);
		}
	}
}

void init_packet_processor() {
    pktQueue = xQueueCreate(PACKET_QUEUE_SIZE, sizeof(wifi_promiscuous_pkt_t));
	
    xTaskCreatePinnedToCore([](void* _) {
        wifi_promiscuous_pkt_t* packet;
        while (1) {
            while (xQueueReceive(pktQueue, &packet, 0) == pdTRUE) {
                handle_packet(packet);
                if (packet) free(packet);
            }
            delay(1);
        }
    }, "pkt proc", PACKET_PROCESSOR_STACK_SIZE, NULL, 2, NULL, PACKET_PROCESSOR_CORE);
}