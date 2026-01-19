#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7735.h>
#include <AnimatedGIF.h>
#include <LittleFS.h>
#include <BleGamepad.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/timers.h"


#define TFT_BLK   48
#define TFT_SDA   45
#define TFT_SCL   39
#define TFT_DC    37
#define TFT_RST   40
#define SWITCH_PIN_6   9
#define SWITCH_PIN_5 7
#define SWITCH_PIN_4 5
#define SWITCH_PIN 35
#define SWITCH_PIN_2 41
#define SWITCH_PIN_3 2
#define TFT_CS    38
#define TFT_CS_2  42  
#define TFT_CS_3  4
#define TFT_CS_4  18
#define TFT_CS_5  8
#define TFT_CS_6  11


Adafruit_ST7735* tft[6];
int ekrany[6] = {TFT_CS, TFT_CS_2, TFT_CS_3, TFT_CS_4, TFT_CS_5, TFT_CS_6};
int klawisze[6] = {SWITCH_PIN, SWITCH_PIN_2, SWITCH_PIN_3, SWITCH_PIN_4, SWITCH_PIN_5, SWITCH_PIN_6};


const char* nazwyPlikow[6] = {
  "/1.gif", "/2.gif", "/3.gif", "/4.gif", "/5.gif", "/6.gif"
};

const uint16_t przyciskiGamepada[6] = {
  BUTTON_1, BUTTON_2, BUTTON_3, BUTTON_4, BUTTON_5, BUTTON_6
};

AnimatedGIF gif;
int aktualnyEkranDoGifa = 0;

BleGamepad bleGamepad("esp moje zycie", "Kajmanek", 100);

QueueHandle_t gifQueue;       
TimerHandle_t buttonTimers[6]; 

struct GifRequest {
  int screenIndex;
  const char* filename;
};

// poniższy kod wygenerowany ponieważ o chuj tu chodzi nie wiem
void GIFDraw(GIFDRAW *pDraw) {
  uint8_t *s;
  uint16_t *d, *usPalette, usTemp[320];
  int x, y, iWidth;

  iWidth = pDraw->iWidth;
  if (iWidth > 160) iWidth = 160;

  usPalette = pDraw->pPalette;
  y = pDraw->iY + pDraw->y;
  
  s = pDraw->pPixels;
  if (pDraw->ucDisposalMethod == 2) {
    for (x = 0; x < iWidth; x++) {
      if (s[x] == pDraw->ucTransparent) s[x] = pDraw->ucBackground;
    }
    pDraw->ucHasTransparency = 0;
  }

  if (pDraw->ucHasTransparency) {
    uint8_t *pEnd, c, ucTransparent = pDraw->ucTransparent;
    int iOffset = 0;
    pEnd = s + iWidth;
    while (s < pEnd) {
      c = *s++;
      if (c == ucTransparent) {
        s--;
        d = usTemp;
        while (s < pEnd && *s == ucTransparent) { s++; iOffset++; }
      } else {
        d = usTemp;
        while (s < pEnd && *s != ucTransparent) { *d++ = usPalette[*s++]; }
        tft[aktualnyEkranDoGifa]->drawRGBBitmap(pDraw->iX + iOffset, y, usTemp, (d - usTemp), 1);
        iOffset += (d - usTemp);
      }
    }
  } else {
    s = pDraw->pPixels;
    for (x = 0; x < iWidth; x++) {
      usTemp[x] = usPalette[*s++];
    }
    tft[aktualnyEkranDoGifa]->drawRGBBitmap(pDraw->iX, y, usTemp, iWidth, 1);
  }
}


void wykonajOdtwarzanie(int numerEkranu, const char* nazwaPliku) {
  aktualnyEkranDoGifa = numerEkranu;
  
  File f = LittleFS.open(nazwaPliku, "r");
  if (!f) {
    Serial.printf("Błąd: Nie znaleziono %s\n", nazwaPliku);
    return;
  }

  size_t rozmiarPliku = f.size();
  if (rozmiarPliku > 150000) { 
      f.close(); return;
  }

  uint8_t *gifBuffer = (uint8_t*)malloc(rozmiarPliku);
  if (gifBuffer == NULL) {
    f.close(); return;
  }

  f.read(gifBuffer, rozmiarPliku);
  f.close();

  if (gif.open(gifBuffer, rozmiarPliku, GIFDraw)) {
    while (gif.playFrame(true, NULL)) {
       vTaskDelay(1); 
    }
    gif.close();
  }
  free(gifBuffer);
}


void onTimerCallback(TimerHandle_t xTimer) {
  int id = (int)pvTimerGetTimerID(xTimer);
  
  GifRequest req;
  req.screenIndex = id;
  req.filename = nazwyPlikow[id];

  xQueueSend(gifQueue, &req, 0);
}

// zajebiscie bo jest tak i kolejka
void taskDisplay(void *parameter) {
  GifRequest receivedReq;
  for(;;) {
    if (xQueueReceive(gifQueue, &receivedReq, portMAX_DELAY) == pdTRUE) {
      wykonajOdtwarzanie(receivedReq.screenIndex, receivedReq.filename);
    }
  }
}

// kolejny świetny task
void taskButtons(void *parameter) {
  bool lastState[6]; 
  for(int i=0; i<6; i++) lastState[i] = HIGH; 

  for(;;) 
  {
    bool isConnected = bleGamepad.isConnected();

    for (int i = 0; i < 6; i++) {

      int currentState = digitalRead(klawisze[i]);

      
      if (currentState != lastState[i]) {
        
        
        if (currentState == LOW) 
        {
          Serial.printf("Przycisk %d wciśnięty\n", i);
          xTimerReset(buttonTimers[i], 0); 

          if (isConnected) {
            bleGamepad.press(przyciskiGamepada[i]);
          }
        }
        else if (currentState == HIGH) 
        {
           if (isConnected) {
             bleGamepad.release(przyciskiGamepada[i]);
           }
        }

        
        lastState[i] = currentState;
      }
    }
    
    vTaskDelay(pdMS_TO_TICKS(15)); 
  }
}


void setup() {
  Serial.begin(9600);
  
  if (!LittleFS.begin(false)) {
    if(!LittleFS.begin(true)) return;
  }

  
  SPI.begin(TFT_SCL, -1, TFT_SDA, -1);
  pinMode(TFT_BLK, OUTPUT); digitalWrite(TFT_BLK, HIGH);
  pinMode(TFT_RST, OUTPUT); digitalWrite(TFT_RST, HIGH); delay(50);
  digitalWrite(TFT_RST, LOW);  delay(50);
  digitalWrite(TFT_RST, HIGH); delay(150);
  
  for (int i = 0; i < 6; i++) {
    pinMode(ekrany[i], OUTPUT);
    digitalWrite(ekrany[i], HIGH);
    tft[i] = new Adafruit_ST7735(ekrany[i], TFT_DC, -1);
    tft[i]->initR(INITR_MINI160x80);
    tft[i]->setRotation(3);
    if (i > 2) tft[i]->setRotation(1);
    tft[i]->fillScreen(ST77XX_BLACK);
  }

  for (int i = 0; i < 6; i++) {
    pinMode(klawisze[i], INPUT_PULLUP);
  }

  gif.begin(GIF_PALETTE_RGB565_LE);

 
  Serial.println("ble jajco 123");
  bleGamepad.begin();

  
  gifQueue = xQueueCreate(5, sizeof(GifRequest));

  for (int i = 0; i < 6; i++) {
    buttonTimers[i] = xTimerCreate("BtnTimer", pdMS_TO_TICKS(500), pdFALSE, (void*)i, onTimerCallback);
  }

  
  xTaskCreate(taskDisplay, "DisplayTask", 8192, NULL, 2, NULL);
  xTaskCreate(taskButtons, "BtnTask", 4096, NULL, 1, NULL);

  Serial.println("System gotowy.");
}

void loop() {
  vTaskDelete(NULL);
}