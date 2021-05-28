#include <Arduino.h>

#include "Utils.h"

#define ARRAY_DIM(a) (sizeof(a) / sizeof((a)[0]))
const static int Battery_Level_Percent_Table[11] = {1500, 2050, 2100, 2140, 2165, 2185, 2200, 2240, 2300, 2380, 2500};

int toPercentage(uint16_t voltage) {
	if(voltage < Battery_Level_Percent_Table[0])
		return 0;

	for(int i = 0; i<ARRAY_DIM(Battery_Level_Percent_Table); i++){
		if(voltage < Battery_Level_Percent_Table[i])
			return i*10 - (10UL * (int)(Battery_Level_Percent_Table[i] - voltage)) / 
			(int)(Battery_Level_Percent_Table[i] - Battery_Level_Percent_Table[i-1]);;
	}

	return 100;
}

float toVoltage(uint16_t reading) {
  return reading / 4096.0 * 7.23;
}