#pragma once
#include "Arduino.h"
#include "log.h"
#include "OneButton.h"
#include "OneButtonTiny.h"
#include "wifi definitions n stuffs.h"
#include "mon thing.h"
#include "display.h"

// fsr i feel like OneButtonTiny is clunky and slower, TODO: test
OneButton button1(21); // change menu
OneButton button2(22); // change submenu, additional function

void init_button() {
	button1.setClickMs(50);
	button1.setPressMs(350);
	button2.setClickMs(50);
	button2.setPressMs(600);
 
	button1.attachClick([]() {
		menuIndex = menuIndex ? 0 : 1;
		display_draw();
	});
	button1.attachDoubleClick([]() {
		deauthActive = !deauthActive;
	});
	button1.attachLongPressStart([]() {
		deauthActive = !deauthActive;
	});
	button2.attachClick([]() {
		if (!menuIndex) {
			channelIndex = (channelIndex + 1) % channels.size();
			esp_wifi_set_channel(channels[channelIndex], WIFI_SECOND_CHAN_NONE);
		}
		else menuIndex = (menuIndex == 1) ? 2 : 1;
		display_draw();
	});
	// button2.attachDoubleClick([]() {
		// haveSD = false;
	// });
	button2.attachLongPressStart([]() {
		haveSD = false;
		pcap.flushFile();
		log1(COLOR_NO_SD, "Stopped SD logging!");
	});
}