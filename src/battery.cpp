#include "battery.h"

int getBatteryPercent() {
    int raw = analogRead(BAT_ADC_PIN);
    float voltage = (raw / 4095.0f) * 3.3f * 2.0f;
    int mv = (int)(voltage * 1000.0f);
    int percent = constrain(map(mv, 3300, 4200, 0, 100), 0, 100);
    return percent;
}