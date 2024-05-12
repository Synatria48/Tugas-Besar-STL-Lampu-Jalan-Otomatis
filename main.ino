#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <Wire.h>
#include "RTClib.h"

// variabel penentuan pin
const int LED1 = 2; // LED Hijau
const int LED2 = 3; // LED Kuning
const int LED3 = 4; // LED Pink
const int LDR1 = A0; // LDR Hijau
const int LDR2 = A1; // LDR Kuning
const int LDR3 = A2; // LDR Pink


void setup () {
    // init serial communication
    Serial.begin(9600);

    // init LED sebagai output
    pinMode(LED1, OUTPUT);
    pinMode(LED2, OUTPUT);
    pinMode(LED3, OUTPUT);

    /*
    pembuatan task berdasarkan prioritas
    RTC -> Photoresitor -> LED -> Report
    */
    xTaskCreate(Task_LED1, "Task LED & LDR 1", 100, NULL, 2, NULL);
    xTaskCreate(Task_LED2, "Task LED & LDR 2", 100, NULL, 2, NULL);
    xTaskCreate(Task_LED3, "Task LED & LDR 3", 100, NULL, 2, NULL);
    xTaskCreate(Task_Report, "Task Report", 100, NULL, 1, NULL);
    xTaskCreate(Task_RTC, "Task RTC!", 100, NULL, 3, NULL);
}

void loop () {
    // tidak digunakan dalam freeRTOS
}