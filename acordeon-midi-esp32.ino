/* ====================== MICHAEL ======================
    numShifterToRead 6
    PIN_CLK     32 // (32)ESP32 -> (2)74HC165  (CP)
    PIN_LOAD    18 // (18)ESP32 -> (1)74HC165  (PL)
    PIN_DATA    16 // (16)ESP32 -> (9)74HC165  (QH)  leitura
    PIN_CLKINH  19 // (19)ESP32 -> (15)74HC165 (CE) sempre em zero
    PIN_SDA     21
    PIN_SCL     22
    PIN_LED_BT  25 // bluetooth connection
    PIN_VIN     39 // voltage read

    //36-47 are the bass notes, 48-59 are the chord notes


 ====================== CORDOVOX ======================
    numShifterToRead 6
    PIN_CLK     10 // (10)ESP32 -> (2)74HC165  (CP)
    PIN_LOAD    11 // (11)ESP32 -> (1)74HC165  (PL)
    PIN_DATA    12 // (12)ESP32 -> (9)74HC165  (QH)  leitura
    PIN_CLKINH     // (15)74HC165 (CE) -> GND
    PIN_SDA      8
    PIN_SCL      9
    PIN_LED_BT  36 // bluetooth connection
    PIN_VIN      4
    delayTime    0  // microseconds between clock pulses

    //36-47 are the bass notes, 48-59 are the chord notes
    ==============================================================
*/

#define MICHAEL
// #define CORDOVOX
// #define TAPEJARA
// #define PASSOFUNDO
#define printOutput false

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEserver.h>
#include <BLE2902.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <WiFiAP.h>
#include <WebServer.h>
#include <ESP2SOTA.h>
#include <Update.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "esp_ota_ops.h"
#include "soc/gpio_struct.h"
#include "soc/gpio_reg.h"
#ifdef CORDOVOX
#include "USB.h"
#include "USBMIDI.h"
#endif

// ================= CONFIG =================
#define SERVICE_UUID "03b80e5a-ede8-4b33-a751-6ce34ec4c700"
#define CHARACTERISTIC_UUID "7772e5db-3868-4112-a1a9-f2669d106bf3"

#ifdef MICHAEL
#define numShifterToRead 6 // 6 shift registers 74HC165
#define PIN_CLK 32         // (32)ESP32 -> (2)74HC165  (CP)
#define PIN_LOAD 18        // (18)ESP32 -> (1)74HC165  (PL)
#define PIN_DATA 16        // (16)ESP32 -> (9)74HC165  (QH)  leitura
#define PIN_CLKINH 19      // (19)ESP32 -> (15)74HC165 (CE) sempre em zero
#define PIN_SDA 21
#define PIN_SCL 22
#define PIN_LED_BT 25 // bluetooth connection
#define PIN_VIN 39    // voltage read
#define MENU_TOTAL 7
#endif

#ifdef CORDOVOX
#define numShifterToRead 6 // 6 shift registers 74HC165
#define PIN_CLK 10         // (32)ESP32 -> (2)74HC165  (CP)
#define PIN_LOAD 11        // (18)ESP32 -> (1)74HC165  (PL)
#define PIN_DATA 12        // (16)ESP32 -> (9)74HC165  (QH)  leitura
#define PIN_SDA 8
#define PIN_SCL 9
#define PIN_LED_BT 36 // bluetooth connection
#define PIN_VIN 4     // voltage read
#define MENU_TOTAL 7
#endif

#define READ_GPIO(pin) ((pin < 32) ? ((GPIO.in >> pin) & 0x1) : ((GPIO.in1.val >> (pin - 32)) & 0x1))
#define GPIO_SET(pin) ((pin < 32) ? (GPIO.out_w1ts = (1UL << pin)) : (GPIO.out1_w1ts.val = (1UL << (pin - 32))))
#define GPIO_CLR(pin) ((pin < 32) ? (GPIO.out_w1tc = (1UL << pin)) : (GPIO.out1_w1tc.val = (1UL << (pin - 32))))
#define CLK_HIGH GPIO_SET(PIN_CLK)
#define CLK_LOW GPIO_CLR(PIN_CLK)

#define FIXED_VELOCITY 100

// ================= BLE =================
BLECharacteristic *pCharacteristic;
volatile bool deviceConnected = false;

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    digitalWrite(PIN_LED_BT, HIGH);
    deviceConnected = true;
  }

  void onDisconnect(BLEServer *pServer)
  {
    digitalWrite(PIN_LED_BT, LOW);
    deviceConnected = false;
  }
};

// ================= MIDI =================
#ifdef CORDOVOX
USBMIDI MIDI;
#endif
uint8_t numChannels = 3;
int8_t numTranspTeclado = 0;
int8_t numTranspBaixos = 4;
uint8_t volChannel1 = 9;
uint8_t volChannel2 = 7;
uint8_t volChannel3 = 6;

typedef struct
{
  uint8_t type;
  uint8_t note;
  uint8_t channel;
} MidiEvent;

QueueHandle_t midiQueue;

// ================= DISPLAY =================
Adafruit_SSD1306 display(128, 64, &Wire, -1);

// ================= OTA STA=================
/*
#ifdef LOCAL1
const char *ssid = "REDE1";
const char *password = "senha";
#endif
#ifdef LOCAL2
const char *ssid = "REDE2";
const char *password = "senha";
#endif

struct WiFiRedes
{
  const char *ssid;
  const char *pass;
};

WiFiRedes redes[] = {
    {"REDE1", "senha"},
    {"REDE2", "senha"}};
*/

// WebServer server(80);
WebServer *server = nullptr;

TaskHandle_t taskReadHandle = NULL;
TaskHandle_t taskMidiHandle = NULL;
TaskHandle_t taskDisplayHandle = NULL;
TaskHandle_t taskOTAHandle = NULL;

volatile bool modoOTA = false;
volatile int otaProgresso = 0;
volatile bool reiniciarESP = false;
// bool sistemaOkParaAtualizacao = false;
// bool otaAtivo = false;
char otaStatus[32] = "idle";
// size_t otaTotalSize = 0;
bool otaFalhou = false;

// ================= MENU =================
volatile bool menuAtivo = false;
int menuIndex = 0;
// bool modoEdicao = false;
uint8_t itemSelecionado = 0;
uint8_t menuTop = 0;   // primeiro item visível
uint8_t cursorPos = 0; // posição do cursor na tela (0–2)

const char *menuItens[] = {
    "CANAIS", "TP TEC", "TP BAI", "VOL C1", "VOL C2", "VOL C3", "OTA"};

//============= pagina web ==============
const char html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>

<head>
<meta name="viewport" content="width=device-width, initial-scale=1">

<style>

body{
    font-family: Arial;
    background:#111;
    color:white;
    text-align:center;
    padding-top:40px;
}

.box{
    width:320px;
    margin:auto;
    background:#1e1e1e;
    padding:20px;
    border-radius:12px;
}

.progress{
    width:100%;
    height:28px;
    background:#333;
    border-radius:10px;
    overflow:hidden;
    margin-top:20px;
}

.bar{
    width:0%;
    height:100%;
    background:#00c853;
    transition:0.1s;
}

button{
    margin-top:20px;
    padding:10px 20px;
    border:none;
    border-radius:8px;
    background:#2196f3;
    color:white;
    font-size:16px;
}

input{
    margin-top:20px;
}

</style>
</head>

<body>

<div class="box">

<h2>Acordeon Michael MIDI</h2>

<input type="file" id="file">

<div class="progress">
<div class="bar" id="bar"></div>
</div>

<h3 id="status">0%</h3>

<button onclick="upload()">Atualizar</button>

</div>

<script>

function upload(){

    let file = document.getElementById("file").files[0];

    if(!file){
        alert("Selecione um arquivo");
        return;
    }

    let data = new FormData();
    data.append("update", file);

    let xhr = new XMLHttpRequest();

    xhr.upload.addEventListener("progress", function(e){

        if(e.lengthComputable){

            let percent = Math.round((e.loaded * 100) / e.total);

            document.getElementById("bar").style.width = percent + "%";

            document.getElementById("status").innerHTML =
                percent + "%";

        }

    });

    xhr.onreadystatechange = function(){

        if(xhr.readyState == 4){

            if(xhr.status == 200){

                document.getElementById("status").innerHTML =
                    "Firmware atualizado!";

            }else{

                document.getElementById("status").innerHTML =
                    "Erro no upload";

            }

        }

    };

    xhr.open("POST", "/update", true);

    xhr.send(data);

}

</script>

</body>
</html>
)rawliteral";

// ================= MIDI =================
class midi
{
public:
#ifdef MICHAEL
  uint8_t niveisVol[10] = {0, 85, 90, 95, 100, 105, 110, 115, 120, 127};
#endif

  uint8_t getNote(uint8_t s, uint8_t p)
  {
    return midiNoteDef[s][p];
  }

  uint8_t getChannel(int shifter, int shifterPos)
  {
#ifdef MICHAEL
    if (shifter <= 2 || shifter == 6)
      return 1;
    if (shifter == 5 || (shifter == 4 && shifterPos <= 3))
      return 2;
    return 3;
#endif
#ifdef CORDOVOX
    if (shifter <= 5)
      return 1;
#endif
  }

  void noteOn(uint8_t note, uint8_t ch)
  {
    // Serial.print("note: ");
    // Serial.print(note);
    // Serial.print("   ch: ");
    // Serial.println(ch);

    uint8_t packetBLE[5] = {
        0x80,
        0x80,
        (uint8_t)(0x90 | (ch - 1)),
        note,
        FIXED_VELOCITY};

#ifdef CORDOVOX
    midiEventPacket_t packetUSB[4] = {
        0x09,
        (uint8_t)(0x90 | (ch - 1)),
        note,
        FIXED_VELOCITY};
#endif

    // if (ch == 1)
    //   p[4] = niveisVol[volChannel1];
    // if (ch == 2)
    //   p[4] = niveisVol[volChannel2];
    // if (ch == 3)
    //   p[4] = niveisVol[volChannel3];
    if (ch == 1)
      note += numTranspTeclado;
    else
      note += numTranspBaixos;
    packetBLE[3] = note;
    pCharacteristic->setValue(packetBLE, 5);
    if (deviceConnected)
    {
      pCharacteristic->notify();
    }
#ifdef CORDOVOX
    MIDI.writePacket(packetUSB);
#endif

    if (numChannels > 3) // se tiver mais de 3 canais envia para os outros
    {
      if (ch > 1) // se for baixos
      {
        for (int x = 0; x < numChannels - 3; x++)
        {
          packetBLE[2] = (0x92 + 1 + x);
// #ifdef MICHAEL
//           packetBLE[4] = niveisVol[volChannel3];
// #endif
#ifdef CORDOVOX
          // packetBLE[4] = { 127 };
          packetUSB[1] = (0x92 + 1 + x);
          // packetUSB[3] = { 127 };
#endif
          pCharacteristic->setValue(packetBLE, 5);
          if (deviceConnected)
          {
            pCharacteristic->notify();
          }
#ifdef CORDOVOX
          MIDI.writePacket(packetUSB);
#endif
        }
      }
    }
  }

  void noteOff(uint8_t note, uint8_t ch)
  {
    uint8_t packetBLE[5] = {
        0x80,
        0x80,
        (uint8_t)(0x90 | (ch - 1)),
        note,
        0};

#ifdef CORDOVOX
    midiEventPacket_t packetUSB[4] = {
        0x08,
        (uint8_t)(0x80 | (ch - 1)),
        note,
        0};
#endif

    if (ch == 1)
      note += numTranspTeclado;
    else
      note += numTranspBaixos;

    packetBLE[3] = note;
#ifdef CORDOVOX
    packetUSB[2] = {note};
#endif

    pCharacteristic->setValue(packetBLE, 5);
    if (deviceConnected)
    {
      pCharacteristic->notify();
    }
#ifdef CORDOVOX
    MIDI.writePacket(packetUSB);
#endif

    if (numChannels > 3) // se tiver mais de 3 canais envia para os outros
    {
      if (ch > 1) // se for os baixos
      {
        for (int x = 0; x < numChannels - 3; x++)
        {
          packetBLE[2] = (0x92 + 1 + x);
#ifdef CORDOVOX
          packetUSB[1] = {(0x92 + 1 + x)};
#endif

          pCharacteristic->setValue(packetBLE, 5);
          if (deviceConnected)
          {
            pCharacteristic->notify();
          }
#ifdef CORDOVOX
          MIDI.writePacket(packetUSB);
#endif
        }
      }
    }
  }

  void controlChange(uint8_t cc, uint8_t value, uint8_t ch)
  {
    uint8_t packetBLE[5] = {
        0x80,
        0x80,
        uint8_t(0xB0 | (ch - 1)),
        cc,
        value};

#ifdef CORDOVOX
    midiEventPacket_t packetUSB[4] = {
        0x0B,
        (uint8_t)(0xB0 | (ch - 1)),
        cc,
        value};
#endif

    pCharacteristic->setValue(packetBLE, 5);

    if (deviceConnected)
    {
      pCharacteristic->notify();
    }
#ifdef CORDOVOX
    MIDI.writePacket(packetUSB);
#endif
  }

  void sendVolumes()
  {
#ifdef MICHAEL
    controlChange(11, niveisVol[volChannel1], 1);
    controlChange(11, niveisVol[volChannel2], 2);
    controlChange(11, niveisVol[volChannel3], 3);
#endif
#ifdef CORDOVOX
    controlChange(11, 127, 1);
    controlChange(11, 127, 2);
    controlChange(11, 127, 3);
#endif

    if (numChannels > 3)
    {
      for (int x = 0; x < numChannels - 3; x++)
      {
#ifdef MICHAEL
        controlChange(11, niveisVol[volChannel3], 4 + x);
#endif
#ifdef CORDOVOX
        controlChange(11, 127, 4 + x);
#endif
      }
    }
  }

private:
#ifdef MICHAEL
  uint8_t midiNoteDef[7][8] = {
      {75, 78, 76, 80, 79, 81, 77, 82},
      {69, 70, 68, 73, 74, 72, 67, 71},
      {60, 59, 61, 62, 66, 65, 64, 63},
      {63, 58, 56, 61, 65, 60, 55, 62},
      {37, 44, 39, 46, 57, 64, 59, 54},
      {47, 38, 45, 43, 41, 48, 40, 42},
      {00, 00, 00, 00, 00, 00, 83, 84}};
#endif
#ifdef CORDOVOX
  uint8_t midiNoteDef[6][8] = {
      {86, 87, 88, 89, 93, 92, 91, 90},
      {81, 80, 79, 78, 85, 84, 83, 82},
      {00, 00, 00, 00, 00, 00, 00, 77},
      {73, 74, 75, 76, 72, 71, 70, 69},
      {61, 63, 64, 65, 68, 67, 66, 62},
      {53, 55, 58, 59, 60, 57, 56, 54}};
#endif
};

midi myMidi;

// ================= SHIFTER =================
class shifter
{
public:
#ifdef MICHAEL
  byte notesToTurnOn[numShifterToRead + 1] = {0};
  byte notesToTurnOff[numShifterToRead + 1] = {0};
#endif
#ifdef CORDOVOX
  byte notesToTurnOn[numShifterToRead] = {0};
  byte notesToTurnOff[numShifterToRead] = {0};
#endif

  shifter()
  {
    pinMode(PIN_LOAD, OUTPUT);
    pinMode(PIN_CLK, OUTPUT);
    pinMode(PIN_DATA, INPUT);

#ifdef MICHAEL
    pinMode(PIN_CLKINH, OUTPUT);
    pinMode(34, INPUT);
    pinMode(35, INPUT);
    pinMode(15, INPUT);

    digitalWrite(PIN_CLKINH, LOW);
#endif
    digitalWrite(PIN_LOAD, HIGH);
    digitalWrite(PIN_CLK, LOW);
  }

  void processShift()
  {
    readShift();
#ifdef MICHAEL
    readDirect();
#endif

#ifdef MICHAEL
    for (int i = 0; i < numShifterToRead + 1; i++)
    {
#endif
#ifdef CORDOVOX
      for (int i = 0; i < numShifterToRead; i++)
      {
#endif
        notesToTurnOff[i] = prev[i] & (~now[i]);
        notesToTurnOn[i] = (~prev[i]) & now[i];
        prev[i] = now[i];
      }
    }

    void printByte(byte val)
    {
      // LSB first
      for (byte i = 0; i <= 7; i++)
      {
        Serial.print(val >> i & 1, BIN); // Magic bit shift, if you care look up the <<, >>, and & operators
      }
      Serial.print("\n"); // Go to the next line, do not collect $200
    }

    void printShifter()
    {
      for (int i = 0; i < numShifterToRead; ++i)
      {
        // if (i == 1 || i == 2 || i == 3 || i == 6)
        if (i == 5)
        {
          Serial.print(i);
          Serial.print(": ");
          printByte(now[i]);
        }
      }
      Serial.println();
      // delay(100);
    }

  private:
#ifdef MICHAEL
    byte prev[numShifterToRead + 1] = {0};
    byte now[numShifterToRead + 1] = {0};
#endif
#ifdef CORDOVOX
    byte prev[numShifterToRead] = {0};
    byte now[numShifterToRead] = {0};
#endif

    // byte readOne() {
    //   byte r = 0;
    //   for (int i = 0; i < 8; i++) {
    //     if (!digitalRead(PIN_DATA))
    //       bitSet(r, i);
    //     digitalWrite(PIN_CLK, HIGH);
    //     digitalWrite(PIN_CLK, LOW);
    //   }
    //   return r;
    // }

    // #define READ_GPIO(pin) ((GPIO.in >> pin) & 0x1)
    byte readOne()
    {
      byte r = 0;

      for (int i = 0; i < 8; i++)
      {
        if (!READ_GPIO(PIN_DATA)) // leitura direta
          bitSet(r, i);

        // clock rápido (sem digitalWrite)
        // GPIO.out1_w1ts.val = (1 << (PIN_CLK - 32)); // HIGH
        // __asm__ __volatile__("nop\nnop\nnop\nnop\n"); // pequeno delay
        // GPIO.out1_w1tc.val = (1 << (PIN_CLK - 32)); // LOW

        CLK_HIGH;
        CLK_LOW;
      }

      return r;
    }

    void readShift()
    {
      // digitalWrite(PIN_LOAD, LOW);
      // digitalWrite(PIN_LOAD, HIGH);
      GPIO.out_w1tc = (1UL << PIN_LOAD); // LOW
      // __asm__ __volatile__("nop\nnop\nnop\n");
      GPIO.out_w1ts = (1UL << PIN_LOAD); // HIGH

#ifdef MICHAEL
      for (int i = 0; i < numShifterToRead + 1; i++)
#endif
#ifdef CORDOVOX
        for (int i = 0; i < numShifterToRead; i++)
#endif
          now[i] = readOne();

#ifdef MICHAEL
      // Bits que estão com a leitura invertida
      for (int i = 0; i < 8; i++)
      {
        bitWrite(now[5], i, !bitRead(now[5], i));
      }
      for (int i = 0; i < 4; i++)
      {
        bitWrite(now[4], i, !bitRead(now[4], i));
      }
#endif

#ifdef CORDOVOX
      // Bits que não estão sendo lidos
      for (int i = 0; i < 7; i++)
      {
        bitClear(now[2], i);
      }
#endif
    }

#ifdef MICHAEL
    void readDirect()
    {
      now[6] = 0;
      bitWrite(now[6], 7, !digitalRead(34));
      bitWrite(now[6], 6, !digitalRead(35));
      bitWrite(now[6], 5, !digitalRead(15));
    }
#endif
  };

  shifter myShifter;

  // ================= FUNÇÕES =================
  float readVoltage()
  {
    static float voltageFiltrado = 0;
    static bool primeiraLeitura = true;

    float leitura = 0;

    for (int i = 0; i < 10; i++)
    {
      leitura += analogRead(PIN_VIN);
    }
    leitura /= 10.0;

    leitura = (leitura / 4095.0) * 8.4;

    if (primeiraLeitura)
    {
      voltageFiltrado = leitura;
      primeiraLeitura = false;
      return voltageFiltrado;
    }

    float deltaMax = 0.015; // mais suave ainda

    float diff = leitura - voltageFiltrado;

    if (diff > deltaMax)
      diff = deltaMax;
    if (diff < -deltaMax)
      diff = -deltaMax;

    voltageFiltrado += diff;
    return voltageFiltrado;
  }

  void iniciarOTA()
  {
    Serial.println("iniciarOTA");
    // if (otaAtivo)
    //   return;

    // otaAtivo = true;
    modoOTA = true;

    BLEDevice ::getAdvertising()->stop();
    btStop();

    digitalWrite(PIN_LED_BT, LOW);
    deviceConnected = false;
    // xQueueReset(midiQueue);
    // vTaskSuspend(taskReadHandle);
    // vTaskSuspend(taskMidiHandle);
    // vTaskSuspend(taskDisplayHandle);
    // display.ssd1306_command(SSD1306_DISPLAYOFF);

    // if (otaAtivo) return;

    // BLEDevice::getAdvertising()->stop();

    // BLEDevice::deinit(true);
    // btStop();
    // esp_bt_controller_disable();
    // esp_bt_controller_deinit();
    // esp_bt_mem_release(ESP_BT_MODE_BTDM);

    // WiFi.disconnect(true);
    // WiFi.mode(WIFI_OFF);
    // delay(300);

    // xTaskCreatePinnedToCore(taskOTA, "OTA", 12288, NULL, 1, &taskOTAHandle, 1);
    xTaskCreatePinnedToCore(taskOTA, "OTA", 16384, NULL, 1, &taskOTAHandle, 1);
  }

  void drawOTA()
  {
    display.clearDisplay();
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    display.setCursor(0, 0);
    display.print("MODO OTA");

    display.setCursor(0, 25);
    display.print(otaStatus);

    display.setCursor(0, 45);
    // display.print(WiFi.localIP());
    display.print(WiFi.softAPIP());

    // barra
    // int w = map(otaProgresso, 0, 100, 0, 120);

    // display.drawRect(0, 50, 120, 10, SSD1306_WHITE);
    // display.fillRect(0, 50, w, 10, SSD1306_WHITE);

    display.display();
  }

  void drawMain()
  {

    display.clearDisplay();
    display.setTextColor(SSD1306_WHITE);

    // =========================
    // 🔶 TOPO (AMARELO)
    // =========================

    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(2);

    // CH
    display.setCursor(0, 2);
    display.print(numChannels);
    display.print("CH");

    // BLE status
    int bleX = 128 - (3 * 12);

    display.setCursor(bleX, 2);

    // controle de pisca
    static bool blink = false;
    static unsigned long lastBlink = 0;

    if (millis() - lastBlink > 400)
    { // velocidade do pisca
      blink = !blink;
      lastBlink = millis();
    }

    if (deviceConnected)
    {
      display.print("BLE");
    }
    else
    {
      if (blink)
      {
        display.print("BLE");
      }
      else
      {
        display.print("   "); // apaga (efeito piscando)
      }
    }

    // =========================
    // 🔵 PARTE AZUL (REFINADA)
    // =========================

    float v = readVoltage();

    int percent = map(v * 100, 600, 840, 0, 100);
    percent = constrain(percent, 0, 100);

    // === DIMENSÕES ===
    int x = 0;
    int y = 20;
    int h = 22;

    // espaço pra tensão "8.4V"
    int textWidth = 48; // tamanho para fonte 2
    int spacing = 2;

    // largura da barra
    int w = 128 - textWidth - spacing - x;

    // === BARRA ===
    display.drawRect(x, y, w, h, SSD1306_WHITE);

    // terminal
    // display.fillRect(x + w, y + 5, 3, 8, SSD1306_WHITE);

    // preenchimento
    int fill = map(percent, 0, 100, 0, w - 4);

    for (int i = 0; i < fill; i += 4)
    {
      display.fillRect(x + 2 + i, y + 3, 3, h - 6, SSD1306_WHITE);
    }

    // === TENSÃO NA MESMA LINHA ===
    display.setTextSize(2);
    display.setTextColor(SSD1306_WHITE);

    // alinhamento vertical com a barra
    int textY = y + 4;

    // posição à direita da barra
    int textX = x + w + spacing;

    display.setCursor(textX, textY);
    display.print(v, 1);
    display.print("v");

    // volta cor
    display.setTextColor(SSD1306_WHITE);

    // ==== LINHA DE INFO ====
    display.setTextSize(2);

    // tensão (esquerda)
    // display.setCursor(0,48);
    // display.print("V:");
    // display.print(v,1);

    // transposição teclado
    display.setCursor(10, 48);
    display.print("T:");
    display.print(numTranspTeclado);

    // baixos
    display.setCursor(70, 48);
    display.print("B:");
    display.print(numTranspBaixos);

    display.display();
  }

  void drawMenu()
  {
    display.clearDisplay();

    // ===== TÍTULO CENTRALIZADO =====
    display.setTextSize(2);

    const char *titulo = "MENU";
    // int textWidth = strlen(titulo) * 12;
    // int x = (128 - textWidth) / 2;

    // display.setCursor(x, 0);
    display.setCursor(0, 0);
    display.setTextColor(SSD1306_WHITE);
    display.print(titulo);

    // ===== CONFIG =====
    int alturaLinha = 17;

    // ===== ITENS =====
    for (int i = 0; i < 3; i++)
    {

      int idx = menuTop + i;
      int y = 16 + i * alturaLinha;

      // ===== HIGHLIGHT =====
      if (i == cursorPos)
      {
        display.fillRect(0, y - 1, 128, 17, SSD1306_WHITE);
        display.setTextColor(SSD1306_BLACK);
      }
      else
      {
        display.setTextColor(SSD1306_WHITE);
      }

      display.setCursor(4, y);
      display.setTextSize(2);

      switch (idx)
      {
      case 0:
        display.print("Canais:  ");
        display.print(numChannels);
        break;

      case 1:
        display.print("Teclado: ");
        display.print(numTranspTeclado);
        break;

      case 2:
        display.print("Baixos:  ");
        display.print(numTranspBaixos);
        break;

      case 3:
        display.print("Vol Ch1: ");
        display.print(volChannel1);
        break;

      case 4:
        display.print("Vol Ch2: ");
        display.print(volChannel2);
        break;

      case 5:
        display.print("Vol Ch3: ");
        display.print(volChannel3);
        break;

      case 6:
        display.print("OTA");
        // otaAtivo = true;
        // iniciarOTA();
        break;
      }

      // ===== SCROLL AUTOMÁTICO =====
      if (itemSelecionado < menuTop)
      {
        menuTop = itemSelecionado;
      }

      if (itemSelecionado >= menuTop + 3)
      {
        menuTop = itemSelecionado - 2;
      }

      // posição do cursor na tela
      cursorPos = itemSelecionado - menuTop;
    }

    display.display();
  }

  // ================= TASKS =================
  void taskRead(void *pv)
  {
    while (true)
    {
      if (modoOTA)
      {
        vTaskDelay(pdMS_TO_TICKS(50));
        continue;
      }
      myShifter.processShift();

      // ===== CONTROLES =====
#ifdef MICHAEL
      bool botaoAr = bitRead(myShifter.notesToTurnOn[6], 5);
      bool navUp = bitRead(myShifter.notesToTurnOn[2], 1);
      bool navDown = bitRead(myShifter.notesToTurnOn[2], 0);
      bool valUp = bitRead(myShifter.notesToTurnOn[2], 2);
      bool valDown = bitRead(myShifter.notesToTurnOn[2], 7);
      bool alterarVol = false;

#endif
#ifdef CORDOVOX
      bool botaoAr = bitRead(myShifter.notesToTurnOn[5], 0);
      bool navUp = bitRead(myShifter.notesToTurnOn[5], 7);
      bool navDown = bitRead(myShifter.notesToTurnOn[5], 1);
      bool valUp = bitRead(myShifter.notesToTurnOn[5], 6);
      bool valDown = bitRead(myShifter.notesToTurnOn[5], 1);
      bool alterarVol = false;
      // Serial.print("botaoAr: ");
      // Serial.println(botaoAr);
      // if(botaoAr) iniciarOTA();
#endif
      // ===== TOGGLE MENU =====
      static bool lastAr = false;
      // Serial.print("modoOTA: ");
      // Serial.print(modoOTA);
      // Serial.print("   menuAtivo: ");
      // Serial.println(!menuAtivo);
      if (botaoAr && !lastAr)
      {
        if (modoOTA)
        {
          modoOTA = false;
        }
        else
        {
          menuAtivo = !menuAtivo;
        }
      }
      lastAr = botaoAr;
      // ===== CONTROLE COM DEBOUNCE =====
      static unsigned long lastPress = 0;

      if (menuAtivo && millis() - lastPress > 120)
      {

        // ===== NAVEGAÇÃO =====
        if (navUp && itemSelecionado > 0)
        {
          itemSelecionado--;
          lastPress = millis();
        }

        if (navDown && itemSelecionado < MENU_TOTAL - 1)
        {
          itemSelecionado++;
          lastPress = millis();
        }
        // ===== ALTERAÇÃO DIRETA =====
        if (valUp)
        {
          switch (itemSelecionado)
          {
          case 0:
            numChannels++;
            break;
          case 1:
            numTranspTeclado++;
            break;
          case 2:
            numTranspBaixos++;
            break;
          case 3:
            volChannel1++;
            alterarVol = true;
            break;
          case 4:
            volChannel2++;
            alterarVol = true;
            break;
          case 5:
            volChannel3++;
            alterarVol = true;
            break;
          case 6:
            // modoOTA = true;
            // Serial.printf("Heap: %u\n", ESP.getFreeHeap());
            // Serial.printf("Maior bloco: %u\n", ESP.getMaxAllocHeap());
            iniciarOTA();
            break;
          }
          lastPress = millis();
        }

        if (valDown)
        {
          switch (itemSelecionado)
          {
          case 0:
            numChannels--;
            break;
          case 1:
            numTranspTeclado--;
            break;
          case 2:
            numTranspBaixos--;
            break;
          case 3:
            volChannel1--;
            alterarVol = true;
            break;
          case 4:
            volChannel2--;
            alterarVol = true;
            break;
          case 5:
            volChannel3--;
            alterarVol = true;
            break;
          }
          lastPress = millis();
        }

        // ===== LIMITES =====
        numChannels = constrain(numChannels, 2, 5);
        numTranspTeclado = constrain(numTranspTeclado, -11, 11);
        numTranspBaixos = constrain(numTranspBaixos, 0, 11);
        volChannel1 = constrain(volChannel1, 0, 9);
        volChannel2 = constrain(volChannel2, 0, 9);
        volChannel3 = constrain(volChannel3, 0, 9);

        if (alterarVol)
        {
          myMidi.sendVolumes();
          // alterarVol = false;
        }
      }

#ifdef MICHAEL
      for (int i = 0; i < numShifterToRead + 1; i++)
      {
#endif
#ifdef CORDOVOX
        for (int i = 0; i < numShifterToRead; i++)
        {
#endif
          for (int b = 0; b < 8; b++)
          {
            if (bitRead(myShifter.notesToTurnOn[i], b))
            {
              MidiEvent ev = {1, myMidi.getNote(i, b), myMidi.getChannel(i, b)};
              xQueueSend(midiQueue, &ev, 0);
              // xQueueSend(midiQueue, &ev, portMAX_DELAY);
            }
            if (bitRead(myShifter.notesToTurnOff[i], b))
            {
              MidiEvent ev = {0, myMidi.getNote(i, b), myMidi.getChannel(i, b)};
              xQueueSend(midiQueue, &ev, 0);
              // xQueueSend(midiQueue, &ev, portMAX_DELAY);
            }
          }
        }

        // Serial.print("print: ");
        // Serial.println(printOutput);

        if (printOutput)
        {
          myShifter.printShifter();
        }

        vTaskDelay(pdMS_TO_TICKS(4));
      }
    }

    void taskMidi(void *pv)
    {
      MidiEvent ev;

      while (true)
      {
        if (modoOTA)
        {
          vTaskDelay(pdMS_TO_TICKS(100));
          continue;
        }

        if (xQueueReceive(midiQueue, &ev, pdMS_TO_TICKS(20)))
        {
          // Serial.print("menuAtivo: ");
          // Serial.println(menuAtivo);
          if (!menuAtivo)
          {
            // Serial.println("depois do menuAtivo");
            if (ev.type)
              myMidi.noteOn(ev.note, ev.channel);
            else
              myMidi.noteOff(ev.note, ev.channel);
          }
        }
      }
    }

    void taskDisplay(void *pv)
    {
      static bool apagou = false;

      vTaskDelay(500 / portTICK_PERIOD_MS);

      while (true)
      {
        if (modoOTA)
        {
          drawOTA();

          // if(!apagou){
          //   display.clearDisplay();
          //   display.display();
          //   apagou = true;
          // }

          vTaskDelay(pdMS_TO_TICKS(100));
          continue;
        }
        apagou = false;

        if (menuAtivo)
          drawMenu();
        else
          drawMain();

        vTaskDelay(pdMS_TO_TICKS(70));
      }
    }

    void taskOTA(void *pv)
    {
      Serial.println("OTA Start");
      strcpy(otaStatus, "Conectando...");

      // server = new WebServer(80);
      WebServer server(80);
      // WiFi.disconnect(true);
      // WiFi.mode(WIFI_OFF);
      // delay(300);
      // vTaskDelay(pdMS_TO_TICKS(500));

      // WiFi.mode(WIFI_STA);
      WiFi.mode(WIFI_AP);
      WiFi.softAP("ESP2OTA", "9989");

      delay(1000);
      IPAddress IP = IPAddress (10, 10, 10, 1);
      IPAddress NMask = IPAddress (255, 255, 255, 0);
      WiFi.softAPConfig (IP, IP, NMask);
      IPAddress myIP = WiFi.softAPIP();
      Serial.print("AP IP Adress: ");
      Serial.println(myIP);

      // WiFi.persistent(false);
      // WiFi.setSleep(false);
      WiFi.setTxPower(WIFI_POWER_19_5dBm);
      // WiFi.setAutoReconnect(false);

      // WiFi.begin(ssid, password);

      Serial.println("Conectando WiFi...");
/*
      const int numRedes = sizeof(redes) / sizeof(redes[0]);
      for (int i = 0; i < numRedes; i++)
      {
        WiFi.begin(redes[i].ssid, redes[i].pass);

        unsigned long start = millis();

        while (WiFi.status() != WL_CONNECTED && millis() - start < 8000)
        {
          vTaskDelay(100);
        }

        if (WiFi.status() == WL_CONNECTED)
        {
          break;
        }
        // }
        // Serial.println(WiFi.status());

        vTaskDelay(pdMS_TO_TICKS(300));
      }
*/
      ESP2SOTA.begin(&server);
      server.begin();

      strcpy(otaStatus, "Conectado");
      Serial.println("Conectado!");
      Serial.println(WiFi.status());
      //   Serial.printf("Heap: %u\n", ESP.getFreeHeap());
      // Serial.printf("Maior bloco: %u\n", ESP.getMaxAllocHeap());

      // Serial.println(WiFi.localIP());
      // Serial.println(WiFi.softAPIP());

      server.on("/", HTTP_GET, [&]()
                 {
                   server.sendHeader("Connection", "close");
                   server.send(200, "text/plain", "Bom dia!");
                 });

      while (modoOTA)
      {
        server.handleClient();
        delay(5);
      }
    }

    // ================= SETUP =================
    void setup()
    {
      Serial.begin(115200);

      pinMode(PIN_LED_BT, OUTPUT);

#ifdef MICHAEL
      BLEDevice::init("ACORDEON MIDI MICHAEL");
#endif
#ifdef CORDOVOX
      BLEDevice::init("ACORDEON MIDI CORDOVOX");
      MIDI.begin();
      USB.productName("ESP2SOTA");
      USB.manufacturerName("Rodrigo");
      USB.begin();
#endif
      BLEServer *pServer = BLEDevice::createServer();
      pServer->setCallbacks(new MyServerCallbacks());

      BLEService *pService = pServer->createService(SERVICE_UUID);

      pCharacteristic = pService->createCharacteristic(
          BLEUUID(CHARACTERISTIC_UUID),
          BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_WRITE_NR);

      pCharacteristic->addDescriptor(new BLE2902());
      pService->start();
      // pServer->getAdvertising()->start();
      BLEAdvertising *pAdvertising = pServer->getAdvertising();
      pAdvertising->addServiceUUID(pService->getUUID());
      pAdvertising->start();
      Wire.begin(PIN_SDA, PIN_SCL);
      Wire.setClock(400000);
      display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
      display.clearDisplay();
      display.display();

      midiQueue = xQueueCreate(128, sizeof(MidiEvent));
      const esp_partition_t *running = esp_ota_get_running_partition();
      esp_ota_img_states_t state;

      if (esp_ota_get_state_partition(running, &state) == ESP_OK)
      {
        if (state == ESP_OTA_IMG_PENDING_VERIFY)
        {
          //      Serial.println("Confirmando firmware...");
          //      if (sistemaOkParaAtualizacao && millis() > 5000) {
          esp_ota_mark_app_valid_cancel_rollback();
          //        Serial.println("Firmware confirmado!!");
          //     } else {
          //        Serial.println("Erro na atualização. Retornando firmware anterior...");
          //      }
        }
      }

      xTaskCreatePinnedToCore(taskRead, "READ", 4096, NULL, 2, &taskReadHandle, 1);
      xTaskCreatePinnedToCore(taskMidi, "MIDI", 4096, NULL, 1, &taskMidiHandle, 0);
      xTaskCreatePinnedToCore(taskDisplay, "DISP", 4096, NULL, 1, &taskDisplayHandle, 1);
      // xTaskCreatePinnedToCore(taskOTA, "OTA", 8192, NULL, 1, NULL, 1);
    }

    void loop()
    {
      vTaskDelay(1000);
    }