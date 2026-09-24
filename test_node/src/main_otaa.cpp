/*
 * LoRaWAN Test Node - OTAA con RadioLib LoRaWAN
 *
 * Gemello del test node ABP (main.cpp), ma con Over-The-Air Activation:
 * il nodo si unisce alla rete con una join-request/accept invece di avere
 * DevAddr e session key pre-condivise. Serve a verificare che il gateway
 * single-channel gestisca correttamente il timing della join-accept
 * (RX1/RX2 a 5s/6s dalla join-request, diverso dal timing dei downlink dati).
 *
 * IMPORTANTE: Richiede RadioLib 7.4.0+
 *
 * Prima di flashare:
 *  1. Fai partire questo firmware una prima volta e leggi da Serial il
 *     DevEUI/JoinEUI stampati in setup().
 *  2. Registra il device su ChirpStack con quel DevEUI/JoinEUI e con
 *     l'AppKey qui sotto (lo stesso AES test key già usato dal nodo ABP).
 *  3. Riavvia il nodo: al prossimo boot proverà a fare la join.
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <RadioLib.h>
#include "power.h"

// ===========================
// PINOUT - Heltec V4
// ===========================
#define LORA_SCK 9
#define LORA_MISO 11
#define LORA_MOSI 10
#define LORA_CS 8
#define LORA_RESET 12
#define LORA_DIO1 14
#define LORA_DIO2 13
#define LED_PIN 35

// ===========================
// CONFIGURAZIONE LORA
// ===========================
#define LORA_FREQUENCY 868.1
#define LORA_BANDWIDTH 125.0
#define LORA_SPREADING_FACTOR 7
#define LORA_CODING_RATE 5
#define LORA_SYNC_WORD 0x34
#define LORA_OUTPUT_POWER                                                      \
  17 // Minimo per test MOLTO ravvicinati (<1m) - riduce saturazione ricevitore
#define LORA_PREAMBLE_LENGTH 8

// ===========================
// CREDENZIALI OTAA
// ===========================
// JoinEUI (AppEUI) - deve combaciare con quello registrato su ChirpStack.
// 0x0000000000000000 va bene per test locali (ChirpStack non lo verifica
// a meno che tu non usi un join server esterno).
uint64_t joinEUI = 0x0000000000000000;

// DevEUI - generato dal MAC del chip così ogni scheda ne ha uno unico senza
// bisogno di un tool di provisioning esterno (stessa convenzione già usata
// per il Gateway ID: MAC con 0xFFFE inserito al centro). Stampato in setup().
uint64_t devEUI = 0;

// AppKey / NwkKey - stesso AES test key (NIST test vector) già usato dal nodo
// ABP per le session key. Va bene per una rete di test locale; sostituiscilo
// con una chiave generata a caso per qualunque cosa esposta oltre il tuo LAN.
uint8_t appKey[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};
uint8_t nwkKey[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                    0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};

// ===========================
// CUSTOM SINGLE-CHANNEL BAND
// ===========================
// Identica a quella del nodo ABP - vedi i commenti lì per i dettagli del
// workaround BAND_FIXED/numChannels=16/freqStep=0. drJoinRequest=5 fa sì che
// anche la join-request esca sull'unico canale/DR fisso del gateway.
const LoRaWANBand_t EU868_SINGLE_CHANNEL = {
    .bandNum = BandEU868,
    .bandType = RADIOLIB_LORAWAN_BAND_FIXED,
    .freqMin = 8630000,
    .freqMax = 8700000,
    .payloadLenMax = {51, 51, 51, 115, 242, 242, 242, 242, 0, 0, 0, 0, 0, 0, 0},
    .powerMax = 16,
    .powerNumSteps = 7,
    .dutyCycle = 36000,
    .dwellTimeUp = 0,
    .dwellTimeDn = 0,
    .txParamSupported = false,
    .txFreqs =
        {
            RADIOLIB_LORAWAN_CHANNEL_NONE,
            RADIOLIB_LORAWAN_CHANNEL_NONE,
            RADIOLIB_LORAWAN_CHANNEL_NONE,
        },
    .numTxSpans = 1,
    .txSpans =
        {{
             .numChannels = 16,
             .freqStart = 8681000,
             .freqStep = 0,
             .drMin = 5,
             .drMax = 5,
             .drJoinRequest = 5
         },
         RADIOLIB_LORAWAN_CHANNEL_SPAN_NONE},
    .rx1Span =
        {
         .numChannels = 16,
         .freqStart = 8681000,
         .freqStep = 0,
         .drMin = 5,
         .drMax = 5,
         .drJoinRequest = RADIOLIB_LORAWAN_DATA_RATE_UNUSED},
    .rx1DrTable =
        {{5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F}},
    .rx2 = {.idx = 0, .freq = 8681000, .drMin = 5, .drMax = 5, .dr = 5},
    .txWoR = {{.idx = 0, .freq = 8651000, .drMin = 3, .drMax = 3, .dr = 3},
              {.idx = 1, .freq = 8655000, .drMin = 3, .drMax = 3, .dr = 3}},
    .txAck = {{.idx = 0, .freq = 8653000, .drMin = 3, .drMax = 3, .dr = 3},
              {.idx = 1, .freq = 8659000, .drMin = 3, .drMax = 3, .dr = 3}},
    .dataRates = {
        {.modem = RADIOLIB_MODEM_LORA, .dr = {.lora = {12, 125, 5}}, .pc = {.lora = {8, false, true, true}}},
        {.modem = RADIOLIB_MODEM_LORA, .dr = {.lora = {11, 125, 5}}, .pc = {.lora = {8, false, true, true}}},
        {.modem = RADIOLIB_MODEM_LORA, .dr = {.lora = {10, 125, 5}}, .pc = {.lora = {8, false, true, false}}},
        {.modem = RADIOLIB_MODEM_LORA, .dr = {.lora = {9, 125, 5}}, .pc = {.lora = {8, false, true, false}}},
        {.modem = RADIOLIB_MODEM_LORA, .dr = {.lora = {8, 125, 5}}, .pc = {.lora = {8, false, true, false}}},
        {.modem = RADIOLIB_MODEM_LORA, .dr = {.lora = {7, 125, 5}}, .pc = {.lora = {8, false, true, false}}},
        {.modem = RADIOLIB_MODEM_LORA, .dr = {.lora = {7, 250, 5}}, .pc = {.lora = {8, false, true, false}}},
        {.modem = RADIOLIB_MODEM_FSK, .dr = {.fsk = {50, 25}}, .pc = {.fsk = {40, 24, 2}}},
        RADIOLIB_DATARATE_NONE,
        RADIOLIB_DATARATE_NONE,
        RADIOLIB_DATARATE_NONE,
        RADIOLIB_DATARATE_NONE,
        RADIOLIB_DATARATE_NONE,
        RADIOLIB_DATARATE_NONE,
        RADIOLIB_DATARATE_NONE}};

// ===========================
// VARIABILI GLOBALI
// ===========================
SPIClass loraSPI(HSPI);
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RESET, LORA_DIO2, loraSPI);

#if defined(LORA_PA_EN)
// RadioLib commuta questi pin da solo a ogni passaggio TX/RX.
static const uint32_t rfswitch_pins[] = {LORA_PA_EN, LORA_PA_TX_EN, RADIOLIB_NC, RADIOLIB_NC, RADIOLIB_NC};
static const Module::RfSwitchMode_t rfswitch_table[] = {
    {Module::MODE_IDLE, {LOW, LOW}},
    {Module::MODE_RX, {HIGH, LOW}},
    {Module::MODE_TX, {HIGH, HIGH}},
    END_OF_MODE_TABLE,
};
#endif
LoRaWANNode node(&radio, &EU868_SINGLE_CHANNEL, 1);

uint16_t frameCounter = 0;
unsigned long lastTransmission = 0;
const unsigned long TRANSMISSION_INTERVAL = 15000; // 15 secondi

uint8_t downlinkPayload[255];
size_t downlinkLen = 0;
LoRaWANEvent_t downlinkEvent;
LoRaWANEvent_t uplinkEvent;

unsigned long lastPowerCheck = 0;
unsigned long powerCheckInterval = 10000;

Preferences nonceStorage;

// ===========================
// PERSISTENZA DEVNONCE (NVS)
// ===========================
// Il DevNonce si incrementa a ogni tentativo di join, riuscito o no. Il
// network server rifiuta come replay qualunque DevNonce già visto per quel
// DevEUI - senza salvarlo, ogni riavvio del nodo (dopo un successo o dopo un
// fallimento) ripartirebbe da un DevNonce già usato e la join fallirebbe per
// sempre con "Invalid DevNonce", indipendentemente da quanto sia corretto
// tutto il resto (AppKey, timing RX1/RX2, ecc.) - è quello che ci è successo
// in questa sessione di test.
void loadNonces() {
  uint8_t buffer[RADIOLIB_LORAWAN_NONCES_BUF_SIZE];
  nonceStorage.begin("lorawan", false);
  size_t len = nonceStorage.getBytes("nonces", buffer, sizeof(buffer));
  nonceStorage.end();
  if (len == sizeof(buffer)) {
    node.setBufferNonces(buffer);
    Serial.println("[LoRaWAN] Nonces ripristinati da NVS");
  } else {
    Serial.println("[LoRaWAN] Nessun nonces salvato in NVS, parto da zero");
  }
}

void saveNonces() {
  uint8_t *buffer = node.getBufferNonces();
  nonceStorage.begin("lorawan", false);
  nonceStorage.putBytes("nonces", buffer, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
  nonceStorage.end();
}

// helper function to display a byte array
void arrayDump(uint8_t *buffer, uint16_t len) {
  for (uint16_t c = 0; c < len; c++) {
    char b = buffer[c];
    if (b < 0x10) {
      Serial.print('0');
    }
    Serial.print(b, HEX);
  }
  Serial.println();
}

bool checkAndPrintJSON(uint8_t *buffer, uint16_t len, bool printFormatted = true) {
  if (len == 0) {
    return false;
  }

  static char jsonStr[256];
  if (len >= sizeof(jsonStr)) {
    Serial.println("[JSON] Buffer troppo grande per essere JSON");
    return false;
  }

  memcpy(jsonStr, buffer, len);
  jsonStr[len] = '\0';

  bool isPrintable = true;
  for (uint16_t i = 0; i < len; i++) {
    if (buffer[i] < 32 && buffer[i] != 9 && buffer[i] != 10 && buffer[i] != 13) {
      isPrintable = false;
      break;
    }
  }

  if (!isPrintable) {
    Serial.println("[JSON] Buffer contiene caratteri non-ASCII, probabilmente non è JSON");
    return false;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonStr);

  if (error) {
    Serial.printf("[JSON] ❌ JSON non valido: %s\n", error.c_str());
    Serial.printf("[JSON] Stringa ricevuta: %s\n", jsonStr);
    return false;
  }

  Serial.println("[JSON] ✅ JSON valido rilevato!");

  if (printFormatted) {
    Serial.println("[JSON] Contenuto JSON formattato:");
    serializeJsonPretty(doc, Serial);
    Serial.println();
  }

  return true;
}

void clearDownlinkBuffer() {
  memset(downlinkPayload, 0, sizeof(downlinkPayload));
  downlinkLen = 0;
  memset(&downlinkEvent, 0, sizeof(downlinkEvent));
}

void printDownlinkInfo() {
  if (downlinkLen > 0) {
    Serial.println(F("[RX] ---- Downlink data ----"));
    arrayDump(downlinkPayload, downlinkLen);
    checkAndPrintJSON(downlinkPayload, downlinkLen);
  } else {
    Serial.println("[RX] ⚠️ ATTENZIONE: Downlink ricevuto ma payload length è 0!");
  }
  // RSSI/SNR del DOWNLINK, cioe' del segnale del gateway misurato QUI.
  // Serve a separare le due direzioni: l'RSSI che vediamo nei log del gateway
  // dipende da (TX del nodo + RX del gateway), questo da (TX del gateway + RX
  // del nodo). Se sono entrambi pessimi il difetto e' simmetrico - tipico di
  // due antenne scollegate; se uno solo lo e', il colpevole e' su quel lato.
  Serial.printf("[RX] >>> DOWNLINK RSSI: %.2f dBm, SNR: %.2f dB <<<\n", radio.getRSSI(), radio.getSNR());
  Serial.printf("[RX] Downlink require ACK: %s\n", downlinkEvent.confirmed ? "Yes" : "No");
  Serial.printf("[RX] Downlink fPort: %d\n", downlinkEvent.fPort);
  Serial.printf("[RX] Downlink length: %zu bytes\n", downlinkLen);
  Serial.printf("[RX] Downlink datarate: %d\n", downlinkEvent.datarate);
  Serial.printf("[RX] Downlink Frame count:: %d\n", downlinkEvent.fCnt);
  Serial.printf("[RX] Downlink Frequency: %f MHz\n", downlinkEvent.freq);
  Serial.printf("[RX] Downlink Multicast: %s\n", downlinkEvent.multicast ? "Multi" : "Unicast");
}

// ===========================
// SETUP
// ===========================
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 30 && !Serial; i++) {
    delay(100);
  }

  Serial.println("\n\n===================================");
  Serial.println("LoRaWAN Test Node - OTAA (RadioLib)");
  Serial.println("===================================\n");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // Vext alimenta, oltre all'OLED, lo stadio d'antenna LoRa della scheda -
  // lo dice variant.h stesso ("powers the oled display and the lora antenna
  // boost"). E' attivo BASSO. Senza, la radio trasmette comunque ma con lo
  // stadio finale non alimentato, quindi pochissima potenza irradiata: un
  // guasto che non produce alcun errore software.
  //
  // Il GATEWAY lo accendeva gia', ma dentro initDisplay(): qui non c'e'
  // display, e nessuno lo aveva mai acceso. Meshtastic lo fa in setup()
  // (src/main.cpp:462) proprio per questo.
  pinMode(VEXT_ENABLE, OUTPUT);
  digitalWrite(VEXT_ENABLE, LOW);
  delay(10);


#if defined(LORA_PA_POWER)
  pinMode(LORA_PA_POWER, OUTPUT);
  digitalWrite(LORA_PA_POWER, HIGH);
  Serial.printf("[LORA] PA esterno alimentato (pin %d)\n", LORA_PA_POWER);
  radio.setRfSwitchTable(rfswitch_pins, rfswitch_table);
  Serial.println("[LORA] setRfSwitchTable (GC1109) configurata");
#endif

  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  Serial.println("[LORA] Inizializzazione SX1262...");
  int state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                          LORA_CODING_RATE, LORA_SYNC_WORD, LORA_OUTPUT_POWER,
                          LORA_PREAMBLE_LENGTH, SX126X_DIO3_TCXO_VOLTAGE);

  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[LORA] OK!");
    Serial.printf("[LORA] Frequenza: %.1f MHz\n", LORA_FREQUENCY);
    Serial.printf("[LORA] SF: %d\n", LORA_SPREADING_FACTOR);
    Serial.printf("[LORA] BW: %.1f kHz\n", LORA_BANDWIDTH);
  } else {
    Serial.printf("[LORA] ERRORE: %d\n", state);
    while (1)
      delay(1000);
  }

  radio.setDio2AsRfSwitch(true);

  state = radio.setCurrentLimit(140);
  Serial.printf("[RadioLib] Current limit set result %d\n", state);

  // ===========================
  // GENERA DevEUI DAL MAC (stessa convenzione del Gateway ID)
  // ===========================
  uint64_t chipMac = ESP.getEfuseMac(); // 48 bit, LSB-first nei byte bassi
  uint8_t macBytes[6];
  for (int i = 0; i < 6; i++) {
    macBytes[i] = (chipMac >> (8 * i)) & 0xFF;
  }
  // DevEUI = MAC[0:3] + FFFE + MAC[3:6], come il Gateway ID in config.h
  devEUI = ((uint64_t)macBytes[5] << 56) | ((uint64_t)macBytes[4] << 48) |
           ((uint64_t)macBytes[3] << 40) | ((uint64_t)0xFF << 32) |
           ((uint64_t)0xFE << 24) | ((uint64_t)macBytes[2] << 16) |
           ((uint64_t)macBytes[1] << 8) | ((uint64_t)macBytes[0]);

  Serial.println("\n[LoRaWAN] Credenziali OTAA da registrare su ChirpStack:");
  Serial.println("===========================================");
  Serial.printf("[LoRaWAN] DevEUI:  %016llX\n", devEUI);
  Serial.printf("[LoRaWAN] JoinEUI: %016llX\n", joinEUI);
  Serial.print("[LoRaWAN] AppKey:  ");
  for (int i = 0; i < 16; i++) Serial.printf("%02X", appKey[i]);
  Serial.println();
  Serial.println("===========================================");
  Serial.println("[LoRaWAN] Se il device non è ancora registrato su ChirpStack");
  Serial.println("[LoRaWAN] con questi valori, la join fallirà (nessun join-accept).");
  Serial.println("===========================================\n");

  // ===========================
  // CONFIGURAZIONE SINGLE-CHANNEL GATEWAY (prima della join!)
  // ===========================
  // Va fatto prima di beginOTAA()/activateOTAA(): la join-request stessa deve
  // uscire sul canale/DR fisso del gateway, non su un canale a caso.
  node.setADR(false);
  node.setDutyCycle(false); // ⚠️ viola i regolamenti EU - solo per test!
  Serial.println("[LoRaWAN] ✅ ADR disabilitato, duty cycle disabilitato (solo test)");
  Serial.println("[LoRaWAN] 🔒 BAND_FIXED attivo: canali FISSI, MAC commands NewChannel IGNORATI");

  // ===========================
  // JOIN OTAA
  // ===========================
  Serial.println("\n[LoRaWAN] Avvio join OTAA...");
  int16_t beginState = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (beginState != RADIOLIB_ERR_NONE) {
    Serial.printf("[LoRaWAN] ❌ ERRORE beginOTAA(): %d\n", beginState);
    while (1)
      delay(1000);
  }

  // Default di RadioLib (10ms) è troppo stretto tra due schede con clock
  // indipendenti: il gateway calcola il timing sul SUO orologio, il nodo sul
  // PROPRIO, e la differenza (drift + latenze di elaborazione) deve stare
  // dentro questo margine o la Join Accept arriva fuori dalla finestra RX1/2.
  // Stesso valore già usato dal nodo ABP dopo beginABP().
  node.scanGuard = 50;

  loadNonces(); // ripristina il DevNonce dall'ultimo avvio, prima di provare la join

  const uint8_t maxJoinAttempts = 5;
  int16_t joinState = RADIOLIB_ERR_UNKNOWN;
  for (uint8_t attempt = 1; attempt <= maxJoinAttempts; attempt++) {
    Serial.printf("[LoRaWAN] Tentativo di join %d/%d...\n", attempt, maxJoinAttempts);
    joinState = node.activateOTAA();

    // Salva SEMPRE, anche sui tentativi falliti: il DevNonce è già stato
    // incrementato e consumato lato server a prescindere dall'esito.
    saveNonces();

    if (joinState == RADIOLIB_LORAWAN_NEW_SESSION) {
      Serial.println("[LoRaWAN] ✅ Join riuscita! Nuova sessione creata.");
      break;
    }

    Serial.printf("[LoRaWAN] ❌ Join fallita (codice %d), riprovo tra %d secondi...\n",
                  joinState, attempt * 5);
    delay(attempt * 5000UL);
  }

  if (!node.isActivated()) {
    Serial.println("[LoRaWAN] ❌ ERRORE: join non riuscita dopo tutti i tentativi.");
    Serial.println("[LoRaWAN] Controlla: DevEUI/JoinEUI/AppKey su ChirpStack,");
    Serial.println("[LoRaWAN]  timing RX1/RX2 della join-accept sul gateway,");
    Serial.println("[LoRaWAN]  che il gateway sia online e visto da ChirpStack.");
    while (1)
      delay(1000);
  }

  Serial.printf("[LoRaWAN] ✅ Sessione attiva! DevAddr assegnato: 0x%08X\n",
                (unsigned long)node.getDevAddr());

  node.setClass(RADIOLIB_LORAWAN_CLASS_A);

  Serial.println("\n[NODE] Nodo pronto! Invio uplink ogni 15 secondi...\n");
  Serial.println("===================================\n");

  digitalWrite(LED_PIN, LOW);
}

// ===========================
// LOOP
// ===========================
void loop() {
  unsigned long now = millis();

  if (millis() - lastPowerCheck > powerCheckInterval) {
    uint16_t batteryVoltage = analogLevel.getBattVoltage();
    Serial.printf("[POWER] Battery voltage: %d\n", batteryVoltage);
    int batteryPercent = analogLevel.getBatteryPercent();
    Serial.printf("[POWER] Battery percent: %d\n", batteryPercent);
    lastPowerCheck = millis();
  }

  if (lastTransmission == 0 || now - lastTransmission >= TRANSMISSION_INTERVAL) {
    lastTransmission = now;
    Serial.println("\n[TX] ===== NEW UPLINK TRANSMIT =====");

    JsonDocument doc;
    doc["uptime"] = now / 1000;
    doc["frameCounter"] = frameCounter;
    String payloadStr;
    serializeJson(doc, payloadStr);

    uint8_t payloadLen = strlen(payloadStr.c_str());

    Serial.printf("[TX] Payload (ASCII): %s\n", payloadStr.c_str());
    Serial.printf("[TX] Lunghezza: %d bytes\n", payloadLen);

    digitalWrite(LED_PIN, HIGH);

    unsigned long txStart = millis();
    int16_t sendReceiveState = node.sendReceive(
        (uint8_t *)payloadStr.c_str(), payloadLen, 1, downlinkPayload,
        &downlinkLen, false, &uplinkEvent, &downlinkEvent);

    unsigned long txDuration = millis() - txStart;
    Serial.printf("[TX] Tempo totale: %lu ms\n", txDuration);

    if (sendReceiveState > 0) {
      Serial.printf("[LoRaWAN] ✅ Uplink inviato + Downlink ricevuto su finestra RX%d!\n",
                    sendReceiveState);
      printDownlinkInfo();
    } else if (sendReceiveState == 0) {
      Serial.println("[LoRaWAN] ✅ Uplink inviato con successo, nessun downlink");
    } else if (sendReceiveState == RADIOLIB_ERR_TX_TIMEOUT) {
      Serial.println("[LoRaWAN] Errore: TX timeout - radio non risponde");
    } else if (sendReceiveState == RADIOLIB_ERR_RX_TIMEOUT) {
      Serial.println("[LoRaWAN] Errore: RX timeout - normale se no downlink");
    } else {
      Serial.printf("[LoRaWAN] ❌ ERRORE sendReceive(): %d\n", sendReceiveState);
    }

    frameCounter++;
    clearDownlinkBuffer();
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }

  int16_t state = node.getDownlinkClassC(downlinkPayload, &downlinkLen, &downlinkEvent);
  if (state > 0) {
    Serial.println("\n[RX] ===== Received a Class C downlink! =====");
    printDownlinkInfo();
    clearDownlinkBuffer();
  }

  delay(1);
}
