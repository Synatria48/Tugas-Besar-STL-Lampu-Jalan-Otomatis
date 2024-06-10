/*
Import library
Pastikan hanya memasukan library yang diperlukan
*/
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <Wire.h>
#include "RTClib.h"

/*
Memberikan Task Handle agar bisa disesuaikan
- Dapat menghapus
- Dapat mengubah prioritas
*/
TaskHandle_t TaskLEDHandle = NULL;
TaskHandle_t TaskMonitoringHandle = NULL;
TaskHandle_t TaskPushReportHandle = NULL;
TaskHandle_t TaskRTCHandle = NULL;
TaskHandle_t TaskReadLdrHandle = NULL;

// Insiasi semaphore mutex & queue
SemaphoreHandle_t interruptSemaphore;
SemaphoreHandle_t mutex;
QueueHandle_t ldrQueue;

// Insisasi class RTC & http
RTC_DS1307 rtc;

// Inisiasi PIN LED
int LED1 = 19;
int LED2 = 5;
int LED3 = 4;

// Inisiasi PIN LDR
const int LDR1 = 25;
const int LDR2 = 26;
const int LDR3 = 27; 

// Inisiasi PIN tombol interrupt
const int BUTTON = 2;

/*
Define default value waktu awal dan akhir untuk logika RTC
- strWaktuAwal adalah waktu awal LED mati (menandakan waktu siang)
- strWaktuAkhir adalah waktu awal LED menyala (menandakan waktu malam)
*/
const String strWaktuAwal = "05:30:00";
const String strWaktuAkhir = "19:30:00";

/*
Define default value state
- Siang adalah state untuk RTC (siang/malam)
- forceOff adalah state untuk button (paksa LED mati)
*/
bool siang = false;
bool forceOff = false;

// Inisiasi parameter untuk menghitung nilai LDR
const float GAMMA = 0.7; 
const float RL10 = 50;

volatile unsigned long last_micros = 0;
const long debouncing_time = 150000; // 150 ms dalam satuan microseconds

/*
Fungsi Intterupt
- Bertujuan untuk melakukan interrupt (paksa LED mati/nyala)
- Memiliki waktu debounce agar tidak mengganggu task/program lain yang berjalan
*/
void debounceInterrupt() {
  unsigned long current_micros = micros();
  if((current_micros - last_micros) >= debouncing_time) {
    xSemaphoreTake(mutex, portMAX_DELAY); // Mengambil mutex

    forceOff = !forceOff; // true -> false -> true

    xSemaphoreGive(mutex); // Melepaskan mutex
    xSemaphoreGiveFromISR(interruptSemaphore, NULL); // melepaskan semaphore
    last_micros = current_micros;
  }
}

/*
Fungsi readLDR
- Bertujuan untuk melakukan pembacaan LDR
- Menggunakan formula standar dari nilai analog
*/
float readLDR(int data) {
  int nilaiLDR = map(data, 4095, 0, 1024, 0);
  float voltage = nilaiLDR / 1024.0 * 5;
  float resistance = 2000 * voltage / (1 - voltage / 5);
  return pow(RL10 * 1e3 * pow(10, GAMMA) / resistance, 1 / GAMMA);
}

/*
Insisasi Task
- deklarasi ini bersifat optional
- bertujuan agar dev/pembaca mengetahui lebih awal,
sistem ini memiliki task berapa banyak
*/
void Task_LED(void* pvParameters);
void TaskMonitoring(void* pvParameters);
void TaskPushReport(void* pvParameters);
void Task_RTC(void* pvParameters);
void Task_ReadLDR(void* pvParameters);

// Fungsi utama yang dijalankan pertama kali
void setup() {
  // Menjalankan komunikasi serial
  Serial.begin(9600);

  // Menjalankan Sensor RTC (Real Time Clock)
  rtc.begin();

  // Setup PIN RELAY LED sebagi output
  pinMode(LED1, OUTPUT);
  pinMode(LED2, OUTPUT);
  pinMode(LED3, OUTPUT);

  // Setup Queue untuk data status LED
  ldrQueue = xQueueCreate(10, sizeof(float) * 3);

  // Setup Mutex untuk intterupt button forceOff
  interruptSemaphore = xSemaphoreCreateBinary();
  mutex = xSemaphoreCreateMutex();
  attachInterrupt(digitalPinToInterrupt(BUTTON), debounceInterrupt, RISING);

  /*
  Setup Task berdasarakan Prioritas
  - Prio 4 : Task_RTC & Task_ReadLDR
  - Prio 3 : Task_LED
  - Prio 2 : Task_Monitoring
  - Prio 1 : Task_PushReport
  */
  xTaskCreate(Task_LED, "Task_LED", 1024, NULL, 3, &TaskLEDHandle);
  xTaskCreate(TaskMonitoring, "Task_Monitoring", 1024, NULL, 2, &TaskMonitoringHandle); // dapat di manipulasi
  xTaskCreate(TaskPushReport, "Task_PushReport", 1024, NULL, 1, &TaskPushReportHandle);
  xTaskCreate(Task_RTC, "Task_RTC", 2048, NULL, 4, &TaskRTCHandle);
  xTaskCreate(Task_ReadLDR, "Task_ReadLDR", 2048, NULL, 4, &TaskReadLdrHandle); // dapat di manipulasi
}

// fungsi ini tidak digunakan dalam FreeRTOS
void loop() {}

/*
Fungsi Task_LED
- Betujuan untuk menghandle semua aktifitas LED
- Menerima data queue nilai LDR
*/
void Task_LED(void* pvParameters) {
  Serial.println("Task LED Created!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  float ldrData[3];
  for (;;) {
    if (xQueueReceive(ldrQueue, &ldrData, portMAX_DELAY) == pdPASS) {
      xSemaphoreTake(mutex, portMAX_DELAY); // Mengambil mutex

      if (forceOff) {
          Serial.println("Force Off!");
          digitalWrite(LED1, LOW);
          digitalWrite(LED2, LOW);
          digitalWrite(LED3, LOW);
      } else {
        if (siang) {
          digitalWrite(LED1, ldrData[0] < 200 ? HIGH : LOW);
          digitalWrite(LED2, ldrData[1] < 200 ? HIGH : LOW);
          digitalWrite(LED3, ldrData[2] < 200 ? HIGH : LOW);
        } else {
          digitalWrite(LED1, HIGH);
          digitalWrite(LED2, HIGH);
          digitalWrite(LED3, HIGH);
        }
      }

      xSemaphoreGive(mutex); // Melepaskan mutex
    }
    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

/*
Fungsi Task_RTC
- Betujuan untuk mengatur waktu real-time
- Menghandle apakah waktu menunjukan siang/malam
- Melakukan penurunan prioritas Task_ReadLDR di waktu malam
- Melakukan penghapusan dan pembuatan Task_Monitoring
*/
void Task_RTC(void* pvParameters) {
  Serial.println("Task RTC Created!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  for (;;) {
    DateTime time = rtc.now();
    String currentTime = time.timestamp(DateTime::TIMESTAMP_TIME);
    siang = (currentTime >= strWaktuAwal && currentTime <= strWaktuAkhir);

    if (siang) {
      vTaskPrioritySet(TaskReadLdrHandle, 4);
      if (TaskMonitoringHandle != NULL) {
        vTaskDelete(TaskMonitoringHandle);
        TaskMonitoringHandle = NULL;
      }
    } else {
      vTaskPrioritySet(TaskReadLdrHandle, 1);
      if (TaskMonitoringHandle == NULL) {
          xTaskCreate(TaskMonitoring, "Task_Monitoring", 1024, NULL, 2, &TaskMonitoringHandle);
      }
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

/*
Fungsi TaskMonitoring
- Betujuan untuk menampilkan status LED dalam model serial
- Berjalan 2 detik sekali
*/
void TaskMonitoring(void* pvParameters) {
  Serial.println("Task Monitoring Created!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  for (;;) {
    Serial.print("LED1: ");
    Serial.print(digitalRead(LED1) == HIGH ? "ON" : "OFF");
    Serial.print(" | LED2: ");
    Serial.print(digitalRead(LED2) == HIGH ? "ON" : "OFF");
    Serial.print(" | LED3: ");
    Serial.println(digitalRead(LED3) == HIGH ? "ON" : "OFF");
    vTaskDelay(2000 / portTICK_PERIOD_MS); 
  }
}

/*
Fungsi TaskPushReport
- Betujuan untuk mengirimkan data ke fungsi sendData
- Proses HTTP request
*/
void TaskPushReport(void* pvParameters) {
  Serial.println("Task Push Report Created!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  for (;;) {
    //sendData()
    Serial.println("Sending HTTP Reports!");
    vTaskDelay(2000 / portTICK_PERIOD_MS); 
  }
}

/*
Fungsi Task_ReadLDR
- Betujuan untuk mengecek nilai di masing masing sensor LDR
- Memasukkan data ke dalam queue secara berkala
*/
void Task_ReadLDR(void* pvParameters) {
  Serial.println("Task LDR Created!");
  vTaskDelay(1000 / portTICK_PERIOD_MS);

  float ldrData[3];
  for (;;) {
    ldrData[0] = readLDR(analogRead(LDR1));
    ldrData[1] = readLDR(analogRead(LDR2));
    ldrData[2] = readLDR(analogRead(LDR3));

    if (xQueueSend(ldrQueue, &ldrData, portMAX_DELAY) != pdPASS) {
      Serial.println("Failed to send to queue");
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}
