#pragma once
#include "Arduino.h"
#include "PCAP.h"
#include "FS.h"
#include "SD.h"
#include "log.h"

extern TFT_eSprite sprite;
using namespace fs;

struct SDpkt {
	uint32_t tS;
	unsigned int tmS;
	uint16_t len;
	uint8_t* payload;
};

bool haveSD; // i cant think of a better name
PCAP pcap = PCAP();
QueueHandle_t SDpktQueue;

// hieu67 said yes sooooooo
// used to sync sd init so it have to finish before proceeding
SemaphoreHandle_t sdInitLock;

void init_sd() {
	sdInitLock = xSemaphoreCreateBinary();
	xSemaphoreGive(sdInitLock); // yes you need to do this
	xSemaphoreTake(sdInitLock, portMAX_DELAY);

	if (!SD.begin()) log1(COLOR_NO_SD, "No SD Card!");
	else {
		haveSD = true;
		uint64_t cardSize = SD.cardSize() / (1024 * 1024);
		log1(COLOR_FOUND_SD, "SD Card Found, Size: %lluMB", cardSize);
		sprite.printf("SD Card Found, Size: %lluMB\n", cardSize);

		File root = SD.open("/");
		int maxNum = 0;

		File file = root.openNextFile();
		while (file) {
			String name = file.name();
			if (name.endsWith(".pcap")) {
				name = name.substring(1, name.length() - 5);
				int num = name.toInt();
				if (num > maxNum) maxNum = num;
			}
			file.close();
			file = root.openNextFile();
		}

        pcap.filename = "/" + String(maxNum) + ".pcap";
        pcap.openFile(SD);
        log1(COLOR_PCAP_FILE_OPENED, "File: %s", pcap.filename.c_str());
		sprite.printf("File: %s\n", pcap.filename.c_str());

		SDpktQueue = xQueueCreate(SD_QUEUE_SIZE, sizeof(SDpkt));
		
		xTaskCreatePinnedToCore([](void* _) {
			SDpkt packet;
			while (haveSD) {
				while (xQueueReceive(SDpktQueue, &packet, 0) == pdTRUE) {
					pcap.newPacketSD(packet.tS, packet.tmS, packet.len, packet.payload);
					free(packet.payload);
				}
				delay(3);
			}
			vTaskDelete(NULL);
		},"sd pkts", SD_TASK_STACK_SIZE, NULL, 3, NULL, SD_TASK_CORE);

		xTaskCreatePinnedToCore([](void* _) {
            while (1) {
                if (haveSD) pcap.flushFile();
                else vTaskDelete(NULL);
                delay(FLUSH_INTERVAL);
            }
        }, "flush", FLUSH_TASK_STACK_SIZE, NULL, 2, NULL, FLUSH_TASK_CORE);
    }
	xSemaphoreGive(sdInitLock);
}

IRAM_ATTR void addPktPcap(uint32_t s, uint32_t us, uint16_t len, uint8_t* payload) {
    if (haveSD) {
        SDpkt packet = {
			.tS = millis() / 1000,
			.tmS = micros() - millis() * 1000,
			.len = len,
			.payload = (uint8_t*)ps_malloc(len)
		};

        if (packet.payload) {
            memcpy(packet.payload, payload, len);
            if (!xQueueSendFromISR(SDpktQueue, &packet, 0)) free(packet.payload);
        }
    }
}
