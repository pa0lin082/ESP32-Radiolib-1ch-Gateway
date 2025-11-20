/*
 * LoRaWAN Test Node - ABP con RadioLib LoRaWAN
 *
 * Questa versione usa RadioLib con supporto LoRaWAN VERO
 * (con AES-CMAC per MIC e AES-128 per cifratura)
 *
 * IMPORTANTE: Richiede RadioLib 6.6.0+
 */

#include <Arduino.h>
#include <ArduinoJson.h>
#include <RadioLib.h>

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
// CHIAVI ABP (DA CONFIGURARE IN CHIRPSTACK)
// ===========================
// Device Address (4 bytes) - in formato little-endian per LoRaWAN
uint32_t DevAddr = 0x260BDE80;

// LoRaWAN 1.1 - Chiavi di Rete Separate
// FNwkSIntKey - Forwarding Network session integrity key (16 bytes)
// attenzione se non si usa FNwkSIntKey, la funzione calculateMIC non funziona
// perche viene settato radiolib rev = 1
uint8_t *FNwkSIntKey = NULL;

// SNwkSIntKey - Serving Network session integrity key (16 bytes)
uint8_t SNwkSIntKey[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                         0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};

// NwkSEncKey - Network session encryption key (16 bytes)
uint8_t NwkSKey[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                     0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};

// Application Session Key (16 bytes)
uint8_t AppSKey[] = {0x2B, 0x7E, 0x15, 0x16, 0x28, 0xAE, 0xD2, 0xA6,
                     0xAB, 0xF7, 0x15, 0x88, 0x09, 0xCF, 0x4F, 0x3C};

// ===========================
// CUSTOM SINGLE-CHANNEL BAND
// ===========================
// Configurazione personalizzata per single-channel gateway su 868.1 MHz
// Usa BAND_FIXED per impedire al network server di modificare i canali via MAC
// commands
const LoRaWANBand_t EU868_SINGLE_CHANNEL = {
    .bandNum = BandEU868,
    .bandType =
        RADIOLIB_LORAWAN_BAND_FIXED, // FIXED = canali NON modificabili dal NS
    .freqMin = 8630000,              // 863.0 MHz in passi di 100 Hz
    .freqMax = 8700000,              // 870.0 MHz in passi di 100 Hz
    .payloadLenMax = {51, 51, 51, 115, 242, 242, 242, 242, 0, 0, 0, 0, 0, 0, 0},
    .powerMax = 16,
    .powerNumSteps = 7,
    .dutyCycle = 36000,
    .dwellTimeUp = 0,
    .dwellTimeDn = 0,
    .txParamSupported = false,
    .txFreqs =
        {
            // Per BAND_FIXED, txFreqs[] non viene usato - usa txSpans[] invece
            RADIOLIB_LORAWAN_CHANNEL_NONE,
            RADIOLIB_LORAWAN_CHANNEL_NONE,
            RADIOLIB_LORAWAN_CHANNEL_NONE,
        },
    .numTxSpans = 1, // Un solo span
    .txSpans =
        {{
             // WORKAROUND: RadioLib richiede numChannels >= 16 per la divisione
             // per 16!
             // Con numChannels/16 deve essere >= 1 per eseguire il loop in
             // calculateChannelFlags()
             // Usiamo 16 canali ma con freqStep=0, quindi TUTTI puntano a 868.1
             // MHz!
             .numChannels =
                 16, // 16 canali (1 banco completo) - TUTTI su 868.1 MHz!
             .freqStart = 8681000, // 868.1 MHz in passi di 100 Hz
             .freqStep =
                 0, // ⚠️ CRITICO: 0 = tutti i canali sulla STESSA frequenza!
             .drMin = 5,        // DR5 (SF7/125kHz)
             .drMax = 5,        // DR5 (SF7/125kHz)
             .drJoinRequest = 5 // DR5 anche per JOIN
         },
         RADIOLIB_LORAWAN_CHANNEL_SPAN_NONE},
    .rx1Span =
        {                      // RX1 sullo stesso canale del TX
         .numChannels = 16,    // 16 canali come TX
         .freqStart = 8681000, // 868.1 MHz
         .freqStep = 0,        // Tutti su 868.1 MHz
         .drMin = 5,
         .drMax = 5,
         .drJoinRequest = RADIOLIB_LORAWAN_DATA_RATE_UNUSED},
    .rx1DrTable =
        {// Per single-channel con DR5 forzato, RX1 risponde sempre con DR5
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR0 TX -> DR5 RX1
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR1 TX -> DR5 RX1
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR2 TX -> DR5 RX1
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR3 TX -> DR5 RX1
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR4 TX -> DR5 RX1
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR5 TX -> DR5 RX1 (caso normale)
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR6 TX -> DR5 RX1
         {5, 5, 5, 5, 5, 5, 0x0F, 0x0F}, // DR7 TX -> DR5 RX1
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F},
         {0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F, 0x0F}},
    .rx2 = {.idx = 0,
            .freq = 8681000,
            .drMin = 5,
            .drMax = 5,
            .dr = 5}, // RX2 sempre su DR5 (SF7/125kHz)
    .txWoR = {{.idx = 0, .freq = 8651000, .drMin = 3, .drMax = 3, .dr = 3},
              {.idx = 1, .freq = 8655000, .drMin = 3, .drMax = 3, .dr = 3}},
    .txAck = {{.idx = 0, .freq = 8653000, .drMin = 3, .drMax = 3, .dr = 3},
              {.idx = 1, .freq = 8659000, .drMin = 3, .drMax = 3, .dr = 3}},
    .dataRates = {
        // DR0-DR7: Definizioni usando la nuova sintassi di RadioLib 7.4.0
        // Formato: { .modem, .dr = {.lora = {SF, BW(kHz), CR}}, .pc = {.lora =
        // {preamble, implicitHeader, crc, ldro}}}
        {.modem = RADIOLIB_MODEM_LORA,
         .dr = {.lora = {12, 125, 5}},
         .pc = {.lora = {8, false, true, true}}}, // DR0: SF12/125kHz
        {.modem = RADIOLIB_MODEM_LORA,
         .dr = {.lora = {11, 125, 5}},
         .pc = {.lora = {8, false, true, true}}}, // DR1: SF11/125kHz
        {.modem = RADIOLIB_MODEM_LORA,
         .dr = {.lora = {10, 125, 5}},
         .pc = {.lora = {8, false, true, false}}}, // DR2: SF10/125kHz
        {.modem = RADIOLIB_MODEM_LORA,
         .dr = {.lora = {9, 125, 5}},
         .pc = {.lora = {8, false, true, false}}}, // DR3: SF9/125kHz
        {.modem = RADIOLIB_MODEM_LORA,
         .dr = {.lora = {8, 125, 5}},
         .pc = {.lora = {8, false, true, false}}}, // DR4: SF8/125kHz
        {.modem = RADIOLIB_MODEM_LORA,
         .dr = {.lora = {7, 125, 5}},
         .pc = {.lora = {8, false, true, false}}}, // DR5: SF7/125kHz
        {.modem = RADIOLIB_MODEM_LORA,
         .dr = {.lora = {7, 250, 5}},
         .pc = {.lora = {8, false, true, false}}}, // DR6: SF7/250kHz
        {.modem = RADIOLIB_MODEM_FSK,
         .dr = {.fsk = {50, 25}},
         .pc = {.fsk = {40, 24, 2}}}, // DR7: FSK
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
// USA LA CUSTOM SINGLE-CHANNEL BAND!
// ⚠️ subBand=1 è necessario per inizializzare correttamente channelMasks con 1
// canale! Con subBand=0, RadioLib ha un bug: numBanks16 = numChannels/16 = 1/16
// = 0 (divisione intera) e il loop per inizializzare channelMasks[] non viene
// mai eseguito, lasciandolo a 0.
LoRaWANNode node(&radio, &EU868_SINGLE_CHANNEL, 1);

uint16_t frameCounter = 0;
unsigned long lastTransmission = 0;
const unsigned long TRANSMISSION_INTERVAL = 15000; // 15 secondi

uint8_t downlinkPayload[255];
size_t downlinkLen = 0;
LoRaWANEvent_t downlinkEvent;
LoRaWANEvent_t uplinkEvent;

// ===========================
// SETUP
// ===========================
void setup() {
  Serial.begin(115200);

  // Attendi connessione seriale
  for (int i = 0; i < 30 && !Serial; i++) {
    delay(100);
  }

  Serial.println("\n\n===================================");
  Serial.println("LoRaWAN Test Node - ABP (RadioLib)");
  Serial.println("===================================\n");

  // LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH); // LED ON

  // Inizializza SPI
  loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);

  // Inizializza radio
  Serial.println("[LORA] Inizializzazione SX1262...");
  int state = radio.begin(LORA_FREQUENCY, LORA_BANDWIDTH, LORA_SPREADING_FACTOR,
                          LORA_CODING_RATE, LORA_SYNC_WORD, LORA_OUTPUT_POWER,
                          LORA_PREAMBLE_LENGTH);

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
  Serial.println("[LORA] OK!");

  // // Configura DIO2 come RF switch
  radio.setDio2AsRfSwitch(true);

  state = radio.setCurrentLimit(140);
  Serial.printf("[RadioLib] Current limit set to %d mA\n", 140);
  Serial.printf("[RadioLib] Current limit set result %d\n", state);

  // // IMPORTANTE: Abilita CRC per compatibilità con il gateway
  // state = radio.setCRC(true);
  // if (state != RADIOLIB_ERR_NONE) {
  //     Serial.printf("[LORA] WARNING: setCRC failed, code: %d\n", state);
  // } else {
  //     Serial.println("[LORA] CRC abilitato");
  // }

  // ===========================
  // CONFIGURAZIONE LORAWAN ABP
  // ===========================
  Serial.println("[LoRaWAN] Configurazione sessione ABP...");
  Serial.println("[LoRaWAN] Chiavi da usare in ChirpStack:");
  Serial.println("===========================================");

  // DevAddr (in formato MSB per ChirpStack)
  Serial.printf("[LoRaWAN] DevAddr: %08X\n", DevAddr);

  // AppSKey (Application Session Key)
  Serial.print("[LoRaWAN] AppSKey: ");
  for (int i = 0; i < 16; i++) {
    Serial.printf("%02X", AppSKey[i]);
  }
  Serial.println();

  // NwkSKey (Network Session Encryption Key)
  if (NwkSKey != NULL) {
    Serial.print("[LoRaWAN] NwkSKey: ");
    for (int i = 0; i < 16; i++) {
      Serial.printf("%02X", NwkSKey[i]);
    }
    Serial.println();
  }

  // FNwkSIntKey (Forwarding Network Session Integrity Key)
  if (FNwkSIntKey != NULL) {
    Serial.print("[LoRaWAN] FNwkSIntKey: ");
    for (int i = 0; i < 16; i++) {
      Serial.printf("%02X", FNwkSIntKey[i]);
    }
    Serial.println();
  }

  // SNwkSIntKey (Serving Network Session Integrity Key)
  Serial.print("[LoRaWAN] SNwkSIntKey: ");
  for (int i = 0; i < 16; i++) {
    Serial.printf("%02X", SNwkSIntKey[i]);
  }
  Serial.println();
  Serial.println("===========================================");

  // IMPORTANTE: beginABP() ritorna un codice di errore!
  node.beginABP(DevAddr, FNwkSIntKey, SNwkSIntKey, NwkSKey, AppSKey);
  node.scanGuard = 50; // 10ms di guardia per il ricevitore


  int16_t activationState = node.activateABP();
  if (activationState != RADIOLIB_ERR_NONE) {
    if (activationState == RADIOLIB_LORAWAN_NEW_SESSION) {
      Serial.println("[LoRaWAN] ✅ Nuova sessione ABP creata!");
    } else if (activationState == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      Serial.println("[LoRaWAN] ✅ Sessione ABP ripristinata!");
    } else {
      Serial.printf("[LoRaWAN] ❌ ERRORE activateABP(): %d\n", activationState);
      Serial.println("[LoRaWAN] Possibili cause:");
      Serial.println("  - Chiavi sbagliate");
      Serial.println("  - Radio non inizializzata");
      Serial.println("  - Configurazione banda errata");
      while (1)
        delay(1000);
    }
  }
  Serial.println("[LoRaWAN] ✅ beginABP() completato");
  // Verifica che la sessione sia attiva
  if (!node.isActivated()) {
    Serial.println("[LoRaWAN] ❌ ERRORE: Sessione NON attiva!");
    while (1)
      delay(1000);
  }
  Serial.println("[LoRaWAN] ✅ Sessione ABP attiva!");
  Serial.printf("[LoRaWAN] DevAddr: 0x%08X\n", DevAddr);

  // ===========================
  // CONFIGURAZIONE SINGLE-CHANNEL GATEWAY
  // ===========================
  Serial.println("\n[LoRaWAN] 🎯 CONFIGURAZIONE SINGLE-CHANNEL GATEWAY");
  Serial.println("[LoRaWAN] ✅ Band personalizzata: EU868_SINGLE_CHANNEL");
  Serial.println("[LoRaWAN] ✅ SOLO canale 868.1 MHz abilitato!");
  Serial.println("[LoRaWAN] ✅ selectChannels() userà sempre 868.1 MHz");

  // CRITICO: Disabilita ADR per evitare che il server cambi i canali
  node.setADR(false);
  Serial.println("[LoRaWAN] ✅ ADR disabilitato");

  // Imposta datarate fisso DR5 (SF7/BW125)
  state = node.setDatarate(5);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println("[LoRaWAN] ✅ Datarate: DR5 (SF7/BW125)");
  }

  // Disabilita duty cycle per test (⚠️ viola regolamenti EU!)
  node.setDutyCycle(false);
  Serial.println("[LoRaWAN] ⚠️ Duty cycle disabilitato (solo test!)");

  // 🔒 BAND_FIXED: La soluzione CORRETTA per single-channel gateway!
  // RadioLib 7.4.0 (LoRaWAN.cpp:2459) controlla:
  //   if(this->band->bandType == RADIOLIB_LORAWAN_BAND_FIXED) { return(false);
  //   }
  // Questo significa che TUTTI i comandi MAC che modificano i canali
  // (NewChannelReq, DLChannelReq, etc.) vengono IGNORATI automaticamente! Il NS
  // non potrà mai aggiungere canali extra come 867.3 MHz, e selectChannels()
  // userà SEMPRE il canale calcolato da txSpans[]: 8681000 + 0*0 = 868.1 MHz @
  // DR5 (SF7)!
  Serial.println("[LoRaWAN] 🔒 BAND_FIXED attivo: canali FISSI, MAC commands "
                 "NewChannel IGNORATI");

  // Riepilogo configurazione radio
  Serial.println("\n[LORA] ===== CONFIGURAZIONE RADIO =====");
  Serial.printf("[LORA] Frequenza: %.3f MHz\n", LORA_FREQUENCY);
  Serial.printf("[LORA] Bandwidth: %.1f kHz\n", LORA_BANDWIDTH);
  Serial.printf("[LORA] Spreading Factor: %d\n", LORA_SPREADING_FACTOR);
  Serial.printf("[LORA] Coding Rate: 4/%d\n", LORA_CODING_RATE);
  Serial.printf("[LORA] Sync Word: 0x%02X\n", LORA_SYNC_WORD);
  Serial.printf("[LORA] Preamble Length: %d\n", LORA_PREAMBLE_LENGTH);
  Serial.printf("[LORA] Output Power: %d dBm\n", LORA_OUTPUT_POWER);
  Serial.printf("[LORA] CRC: ABILITATO\n");
  Serial.println("[LORA] ====================================\n");

  Serial.println("\n[NODE] ===== CONFIGURAZIONE LORAWAN ABP =====");
  Serial.printf("[NODE] DevAddr: 0x%08X\n", DevAddr);
  Serial.println("[NODE] Modalità: ABP (Activation By Personalization)");
  Serial.println("[NODE] Classe: A (uplink + RX1/RX2)");
  Serial.println("[NODE] =============================================\n");
  Serial.println("[NODE] Nodo pronto! Invio uplink ogni 15 secondi...\n");
  Serial.println("===================================\n");

//   node.setClass(RADIOLIB_LORAWAN_CLASS_C);
  node.setClass(RADIOLIB_LORAWAN_CLASS_A);

  digitalWrite(LED_PIN, LOW); // LED OFF
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

// helper function to check if a byte array contains valid JSON
// Returns true if JSON is valid, false otherwise
// Optionally prints the JSON in formatted form
bool checkAndPrintJSON(uint8_t *buffer, uint16_t len, bool printFormatted = true) {
  if (len == 0) {
    return false;
  }
  
  // Use static buffer (max 255 bytes for LoRaWAN payload)
  static char jsonStr[256];
  if (len >= sizeof(jsonStr)) {
    Serial.println("[JSON] Buffer troppo grande per essere JSON");
    return false;
  }
  
  // Convert byte array to null-terminated string
  memcpy(jsonStr, buffer, len);
  jsonStr[len] = '\0';
  
  // Check if all bytes are printable ASCII (basic check)
  bool isPrintable = true;
  for (uint16_t i = 0; i < len; i++) {
    if (buffer[i] < 32 && buffer[i] != 9 && buffer[i] != 10 && buffer[i] != 13) {
      // Not printable ASCII (except tab, newline, carriage return)
      isPrintable = false;
      break;
    }
  }
  
  if (!isPrintable) {
    Serial.println("[JSON] Buffer contiene caratteri non-ASCII, probabilmente non è JSON");
    return false;
  }
  
  // Try to parse JSON using ArduinoJson v7
  // ArduinoJson v7 uses JsonDocument (which is an alias for StaticJsonDocument)
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, jsonStr);
  
  if (error) {
    Serial.printf("[JSON] ❌ JSON non valido: %s\n", error.c_str());
    Serial.printf("[JSON] Stringa ricevuta: %s\n", jsonStr);
    return false;
  }
  
  // JSON is valid!
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
    Serial.println(
        "[RX] ⚠️ ATTENZIONE: Downlink ricevuto ma payload length è 0!");
  }
  Serial.printf("[RX] Downlink require ACK: %s\n", downlinkEvent.confirmed ? "Yes" : "No");
  Serial.printf("[RX] Downlink fPort: %d\n", downlinkEvent.fPort);
  Serial.printf("[RX] Downlink length: %zu bytes\n", downlinkLen);
  Serial.printf("[RX] Downlink datarate: %d\n", downlinkEvent.datarate);
  Serial.printf("[RX] Downlink Frame count:: %d\n", downlinkEvent.fCnt);
  Serial.printf("[RX] Downlink Frequency: %f MHz\n",
                downlinkEvent.freq );
  Serial.printf("[RX] Downlink Multicast: %s\n",
                downlinkEvent.multicast ? "Multi" : "Unicast");
}

// ===========================
// LOOP
// ===========================
void loop() {
  unsigned long now = millis();

  // Invia pacchetto ogni TRANSMISSION_INTERVAL
  if (lastTransmission == 0 ||
      now - lastTransmission >= TRANSMISSION_INTERVAL) {
    lastTransmission = now;
    Serial.println("\n[TX] ===== NEW UPLINK TRANSMIT =====");
    Serial.println("\n[TX] ----- CREATE PAYLOAD JSON -----");
    JsonDocument doc;
    doc["uptime"] = now / 1000;
    doc["frameCounter"] = frameCounter;
    String payloadStr;
    serializeJson(doc, payloadStr);
    // Crea payload JSON con uptime e frameCounter
    // char payloadStr[80];
    // unsigned long uptime = now / 1000; // uptime in secondi
    // snprintf(payloadStr, sizeof(payloadStr),
    //          "{\"uptime\":%lu,\"frameCounter\":%d}", uptime, frameCounter);

    uint8_t payloadLen = strlen(payloadStr.c_str());

    
    Serial.printf("[TX] Payload (ASCII): %s\n", payloadStr.c_str());
    Serial.printf("[TX] Lunghezza: %d bytes\n", payloadLen);
    Serial.print("[TX] Payload (HEX): ");
    for (int i = 0; i < payloadLen; i++) {
      Serial.printf("%02X ", (uint8_t)payloadStr.c_str()[i]);
    }
    Serial.println();

    // LED on
    digitalWrite(LED_PIN, HIGH);

    // ===========================
    // INVIO UPLINK LORAWAN
    // ===========================
    unsigned long txStart = millis();
    Serial.println("\n[TX] ----- SEND LORAWAN FRAME -----");
    Serial.printf("[TX] Starting transmission of payload at: %lu\n", txStart);
    Serial.printf("[TX] fPort: 1 (porta applicativa)\n");

    // sendReceive() BLOCCA per ~2-3 secondi (RX1 + RX2)
    // Ritorna: >0 = finestra RX, 0 = nessun downlink, <0 = errore
    int16_t sendReceiveState = node.sendReceive(
        (uint8_t *)payloadStr.c_str(), // dati uplink
        payloadLen,            // lunghezza uplink
        1,                     // fPort
        downlinkPayload,       // buffer per downlink
        &downlinkLen,          // lunghezza downlink ricevuto
        false,                 // isConfirmed (false = unconfirmed uplink)
        &uplinkEvent,          // eventUp (NULL = non necessario)
        &downlinkEvent         // eventDown (per ottenere fPort e altre info)
    );

    unsigned long txDuration = millis() - txStart;
    Serial.printf("[TX] Tempo totale: %lu ms\n", txDuration);

    if (sendReceiveState > 0) {
      // Downlink ricevuto!
      Serial.printf(
          "[LoRaWAN] ✅ Uplink inviato + Downlink ricevuto su finestra RX%d!\n",
          sendReceiveState);
      printDownlinkInfo();

    } else if (sendReceiveState == 0) {
      // Nessun downlink - NORMALE per uplink-only
      Serial.println(
          "[LoRaWAN] ✅ Uplink inviato con successo, nessun downlink");
    } else {
     

      // Decodifica errori comuni
      if (sendReceiveState == RADIOLIB_ERR_TX_TIMEOUT) {
        Serial.println("[LoRaWAN] Errore: TX timeout - radio non risponde");
      } else if (sendReceiveState == RADIOLIB_ERR_RX_TIMEOUT) {
        Serial.println("[LoRaWAN] Errore: RX timeout - normale se no downlink");
      } else if (sendReceiveState == RADIOLIB_ERR_CRC_MISMATCH) {
        Serial.println("[LoRaWAN] Errore: CRC mismatch");
      } else if (sendReceiveState == RADIOLIB_ERR_MIC_MISMATCH) {
        Serial.println("[LoRaWAN] Errore: MIC mismatch");
      } else if (sendReceiveState == RADIOLIB_ERR_INVALID_FREQUENCY) {
        Serial.println("[LoRaWAN] Errore: Frequenza non valida");
      } else {
        // Errore nella trasmissione
        Serial.printf("[LoRaWAN] ❌ ERRORE sendReceive(): %d\n",
                      sendReceiveState);

        Serial.println(
            "[LoRaWAN] Il pacchetto potrebbe NON essere stato trasmesso!");
      }
    }
    clearDownlinkBuffer();
    // LED off
    delay(100);
    digitalWrite(LED_PIN, LOW);
  }



  // check if a Class C downlink is ready for processing
  // tip: internally, this just checks a boolean;
  //      it does not poll the radio over SPI.
  // tip: you are not required to continuously call
  //      this function; you can do other stuff in between.
  //      however, a downlink may be overwritten if you
  //      don't call this function in time for the previous one.
  int16_t state =
      node.getDownlinkClassC(downlinkPayload, &downlinkLen, &downlinkEvent);
  if (state > 0) {
    Serial.println("\n[RX] ===== Received a Class C downlink! =====");
    printDownlinkInfo();
    clearDownlinkBuffer();
  }

  delay(1);
}
