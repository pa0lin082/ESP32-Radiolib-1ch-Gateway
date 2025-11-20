/*
 * LoRaWAN Single Channel Gateway for ChirpStack
 * Hardware: Heltec WiFi LoRa 32 V4 (ESP32-S3 + SX1262)
 * 
 * Features:
 * - RadioLib for SX1262 support
 * - Semtech UDP protocol for ChirpStack
 * - OLED display
 * - WiFi connectivity
 * - NTP time synchronization
 */

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <SPI.h>
#include <Wire.h>
#include <RadioLib.h>
#include <ArduinoJson.h>
#include <time.h>
#include <ArduinoOTA.h>
#include "USBCDC.h"
#include "config.h"
#include "esp32-hal.h"
#include "variant.h"
#include "common.h"
#include "TypeDef.h"

// ===========================
// OLED DISPLAY
// ===========================
#if DISPLAY_ENABLED
#include <U8g2lib.h>
// SSD1306/SSD1315 128x64 I2C display (Heltec V4 uses SSD1306)
// Hardware I2C: pins are set with Wire.begin(), only reset is needed
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, /* reset=*/ RESET_OLED);
#endif

// ===========================
// RADIO CONFIGURATION
// ===========================
SPIClass loraSPI(HSPI);
SX1262 radio = new Module(LORA_CS, LORA_DIO1, LORA_RESET, LORA_DIO2, loraSPI);

// ===========================
// NETWORK CONFIGURATION
// ===========================
WiFiUDP udpClient;
IPAddress serverIP;

const char* version = "1.0.0";

// ===========================
// GATEWAY STATE
// ===========================
uint64_t gatewayId = 0;
uint32_t packetsReceived = 0;
uint32_t packetsForwarded = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastNtpUpdate = 0;
unsigned long lastPullData = 0;
bool radioInitialized = false;


// Interrupt flag for packet reception
volatile bool packetReceived = false;

// Flag per ignorare la ricezione subito dopo downlink
bool justTransmitted = false;

// Debug counters
uint32_t totalInterrupts = 0;
uint32_t crcErrors = 0;
uint32_t timeouts = 0;
uint32_t otherErrors = 0;

// ===========================
// INTERRUPT SERVICE ROUTINE
// ===========================
void IRAM_ATTR setPacketReceivedFlag() {
    packetReceived = true;
}

// ===========================
// STATISTICS
// ===========================
struct Statistics {
    uint32_t rx_received;
    uint32_t rx_ok;
    uint32_t rx_fw;
    uint32_t rx_bad;
    uint32_t tx_received;
    uint32_t tx_emitted;
} stats = {0};

// ===========================
// FUNCTION DECLARATIONS
// ===========================
void initDisplay();
void updateDisplay();
void initWiFi();
void initOTA();
void initLoRa();
void initNTP();
void sendUdpPacket(const char* jsonData);
void handleLoRaPacket();
void sendStatPacket();
void sendPullData();
void handleUdpDownlink();
void sendDownlinkResponse(unsigned long rxTimestamp, PullRespPacket *pullRespPacket);
bool transmitDownlink(uint8_t* data, size_t length, unsigned long rxTimestamp);
void sendTxAck(uint16_t token);
void decodeLoRaWANPacket(uint8_t *data, size_t length);



DownlinkQueue dowQueue = DownlinkQueue();

// ===========================
// SETUP
// ===========================
void setup() {
    // Initialize USB Serial
    Serial.begin(115200);
    
    // Wait for USB CDC connection (max 3 seconds)
    for(int i=0; i<30 && !Serial; i++) {
        delay(100);
    }
    
    Serial.println("\n\n===================================");
    Serial.println("LoRaWAN Gateway for ChirpStack");
    Serial.println("Hardware: Heltec WiFi LoRa 32 V4");
    Serial.println("Version: " + String(version));
    Serial.println("===================================\n");

    // Initialize LED
    pinMode(LED_PIN, OUTPUT);
    digitalWrite(LED_PIN, LOW);  // LED on
    
    // Initialize OLED display
    #if DISPLAY_ENABLED
    initDisplay();
    #endif
    
    // Initialize WiFi
    initWiFi();
    
    // Initialize OTA
    initOTA();
    
    // Initialize NTP
    initNTP();
    
    // Generate Gateway ID from MAC
    generateGatewayId(&gatewayId);
    
    // Initialize LoRa radio
    initLoRa();
    
    digitalWrite(LED_PIN, HIGH);  // LED off
    
    Serial.println("\n===================================");
    Serial.println("Gateway ready!");
    Serial.println("===================================\n");
}

void processDownlinkQueue() {
  PullRespPacket *pullRespPacket = dowQueue.findFirstImmediate();
  if (pullRespPacket) {
    pullRespPacket->responseData.txpk.data;

    radio.invertIQ(true);
    int state = radio.transmit(pullRespPacket->responseData.txpk.data, pullRespPacket->responseData.txpk.size);
    radio.invertIQ(false);
    digitalWrite(LED_PIN, HIGH);
    justTransmitted = true;

    if (state == RADIOLIB_ERR_NONE) {
      Serial.println("[PULL] ✅ Messaggio Classe C trasmesso con successo!");
      sendTxAck(pullRespPacket->token);
      dowQueue.remove(pullRespPacket);
      stats.tx_emitted++;
    } else {
      Serial.println("[PULL] ❌ Errore trasmissione messaggio Classe C");
    }
  }
}
// ===========================
// MAIN LOOP
// ===========================
void loop() {
    // Handle OTA updates
    ArduinoOTA.handle();
    
    // Send PULL_DATA to ChirpStack periodically (every 5 seconds)
    if (millis() - lastPullData > 5000) {
        sendPullData();
        lastPullData = millis();
    }
    
    // Check for UDP packets from ChirpStack (downlink)
    handleUdpDownlink();
    processDownlinkQueue();
    
    // Handle incoming LoRa packets only when interrupt flag is set
    if (radioInitialized && packetReceived) {
        handleLoRaPacket();
    }
    
    // Update display periodically
    #if DISPLAY_ENABLED
    if (millis() - lastDisplayUpdate > DISPLAY_UPDATE_INTERVAL) {
        updateDisplay();
        lastDisplayUpdate = millis();
    }
    #endif
    
    // Update NTP periodically
    if (millis() - lastNtpUpdate > NTP_UPDATE_INTERVAL) {
        initNTP();
        lastNtpUpdate = millis();
    }
    
    // Send statistics every 300 seconds
    static unsigned long lastStatTime = 0;
    if (lastStatTime == 0 || millis() - lastStatTime > 300000) {
        sendStatPacket();
        lastStatTime = millis();
    }
    
    // Debug: stampa statistiche ogni 10 secondi
    static unsigned long lastDebugTime = 0;
    if (millis() - lastDebugTime > 120000) {
        Serial.println("\n[STATS] ===== GATEWAY STATUS =====");
        Serial.printf("[STATS] Uptime: %lu s\n", millis() / 1000);
        Serial.printf("[STATS] Interrupt totali: %lu\n", totalInterrupts);
        Serial.printf("[STATS] Pacchetti OK: %lu\n", stats.rx_ok);
        Serial.printf("[STATS] Errori CRC: %lu\n", crcErrors);
        Serial.printf("[STATS] Timeout: %lu\n", timeouts);
        Serial.printf("[STATS] Altri errori: %lu\n", otherErrors);
        Serial.printf("[STATS] Radio in ascolto: %s\n", radioInitialized ? "SI" : "NO");
        Serial.printf("[STATS] WiFi: %s\n", WiFi.isConnected() ? "OK" : "DISCONNESSO");
        Serial.println("[STATS] ===============================\n");
        lastDebugTime = millis();
    }
    
    // Small delay to prevent watchdog issues
    delay(1);
}

// ===========================
// DISPLAY FUNCTIONS
// ===========================
void initDisplay() {
    #if DISPLAY_ENABLED
    // Enable power to OLED display (active LOW)
    pinMode(VEXT_ENABLE, OUTPUT);
    digitalWrite(VEXT_ENABLE, LOW);
    delay(100); // Give the display time to power up
    
    // Initialize I2C for display
    Wire.begin(I2C_SDA, I2C_SCL);
    
    display.begin();
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tr);
    display.drawStr(0, 10, "LoRaWAN Gateway");
    display.drawStr(0, 25, "Initializing...");
    display.sendBuffer();
    
    Serial.println("[DISPLAY] Initialized");
    #endif
}

void updateDisplay() {
    #if DISPLAY_ENABLED
    display.clearBuffer();
    display.setFont(u8g2_font_6x10_tr);
    
    // Title
    display.drawStr(0, 10, "LoRaWAN Gateway");
    
    // WiFi status
    char line[32];
    snprintf(line, sizeof(line), "WiFi: %s", WiFi.isConnected() ? "OK" : "DISC");
    display.drawStr(0, 22, line);
    
    // Frequency and SF
    snprintf(line, sizeof(line), "%.1fMHz SF%d", LORA_FREQUENCY, LORA_SPREADING_FACTOR);
    display.drawStr(0, 34, line);
    
    // Statistics
    snprintf(line, sizeof(line), "RX: %lu FW: %lu", stats.rx_ok, stats.rx_fw);
    display.drawStr(0, 46, line);
    
    // Time
    time_t now = time(nullptr);
    struct tm* timeinfo = localtime(&now);
    snprintf(line, sizeof(line), "%02d:%02d:%02d", 
             timeinfo->tm_hour, timeinfo->tm_min, timeinfo->tm_sec);
    display.drawStr(0, 58, line);
    
    display.sendBuffer();
    #endif
}

// ===========================
// WIFI FUNCTIONS
// ===========================
void initWiFi() {
    Serial.print("[WIFI] Connecting to ");
    Serial.print(WIFI_SSID);
    Serial.print("...");
    
    #if DISPLAY_ENABLED
    display.clearBuffer();
    display.drawStr(0, 10, "WiFi connecting...");
    display.sendBuffer();
    #endif
    
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    unsigned long startTime = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - startTime > WIFI_CONNECT_TIMEOUT) {
            Serial.println("\n[WIFI] Connection timeout!");
            #if DISPLAY_ENABLED
            display.clearBuffer();
            display.drawStr(0, 10, "WiFi FAILED!");
            display.sendBuffer();
            #endif
            delay(5000);
            ESP.restart();
        }
        delay(500);
        Serial.print(".");
    }
    
    Serial.println(" Connected!");
    Serial.print("[WIFI] IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("[WIFI] MAC address: ");
    Serial.println(WiFi.macAddress());
    
    // Resolve server hostname
    if (WiFi.hostByName(SERVER_HOST, serverIP)) {
        Serial.print("[SERVER] Resolved to: ");
        Serial.println(serverIP);
    } else {
        Serial.println("[SERVER] ERROR: Could not resolve hostname");
    }
}

// ===========================
// OTA FUNCTIONS
// ===========================
void initOTA() {
    Serial.println("[OTA] Initializing OTA...");
    
    // Imposta hostname per mDNS
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    
    // Imposta password (opzionale ma consigliata)
    ArduinoOTA.setPassword(OTA_PASSWORD);
    
    // Callback quando inizia l'aggiornamento
    ArduinoOTA.onStart([]() {
        String type;
        if (ArduinoOTA.getCommand() == U_FLASH) {
            type = "sketch";
        } else {  // U_SPIFFS
            type = "filesystem";
        }
        
        Serial.println("[OTA] Start updating " + type);
        
        #if DISPLAY_ENABLED
        display.clearBuffer();
        display.drawStr(0, 10, "OTA Update...");
        display.drawStr(0, 25, type.c_str());
        display.sendBuffer();
        #endif
        
        // Ferma la radio durante l'aggiornamento
        if (radioInitialized) {
            radio.standby();
        }
    });
    
    // Callback durante l'aggiornamento (progresso)
    ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
        unsigned int percent = (progress * 100) / total;
        Serial.printf("[OTA] Progress: %u%%\r", percent);
        
        #if DISPLAY_ENABLED
        if (percent % 10 == 0) {  // Aggiorna display ogni 10%
            display.clearBuffer();
            display.drawStr(0, 10, "OTA Update...");
            char progressStr[32];
            snprintf(progressStr, sizeof(progressStr), "Progress: %u%%", percent);
            display.drawStr(0, 25, progressStr);
            display.sendBuffer();
        }
        #endif
    });
    
    // Callback quando l'aggiornamento è completato
    ArduinoOTA.onEnd([]() {
        Serial.println("\n[OTA] Update complete!");
        
        #if DISPLAY_ENABLED
        display.clearBuffer();
        display.drawStr(0, 10, "OTA Complete!");
        display.drawStr(0, 25, "Rebooting...");
        display.sendBuffer();
        #endif
        
        delay(1000);
    });
    
    // Callback in caso di errore
    ArduinoOTA.onError([](ota_error_t error) {
        Serial.printf("[OTA] Error[%u]: ", error);
        if (error == OTA_AUTH_ERROR) {
            Serial.println("Auth Failed");
        } else if (error == OTA_BEGIN_ERROR) {
            Serial.println("Begin Failed");
        } else if (error == OTA_CONNECT_ERROR) {
            Serial.println("Connect Failed");
        } else if (error == OTA_RECEIVE_ERROR) {
            Serial.println("Receive Failed");
        } else if (error == OTA_END_ERROR) {
            Serial.println("End Failed");
        }
        
        #if DISPLAY_ENABLED
        display.clearBuffer();
        display.drawStr(0, 10, "OTA ERROR!");
        display.sendBuffer();
        #endif
        
        // Riavvia la radio
        if (radioInitialized) {
            radio.startReceive();
        }
    });
    
    ArduinoOTA.begin();
    
    Serial.println("[OTA] Ready!");
    Serial.println("[OTA] Hostname: esp32-gateway.local");
    Serial.print("[OTA] IP address: ");
    Serial.println(WiFi.localIP());
}

// ===========================
// NTP FUNCTIONS
// ===========================
void initNTP() {
    Serial.print("[NTP] Synchronizing time...");
    
    configTime(0, 0, NTP_SERVER);
    
    time_t now = time(nullptr);
    int retry = 0;
    while (now < 8 * 3600 * 2 && retry < 15) {
        delay(500);
        Serial.print(".");
        now = time(nullptr);
        retry++;
    }
    
    if (now > 8 * 3600 * 2) {
        Serial.println(" OK");
        struct tm timeinfo;
        gmtime_r(&now, &timeinfo);
        Serial.print("[NTP] Current time: ");
        Serial.println(asctime(&timeinfo));
    } else {
        Serial.println(" FAILED");
    }
    
    lastNtpUpdate = millis();
}


// ===========================
// LORA INITIALIZATION
// ===========================
void initLoRa() {
    Serial.println("[LORA] Initializing SX1262...");
    
    #if DISPLAY_ENABLED
    display.clearBuffer();
    display.drawStr(0, 10, "LoRa init...");
    display.sendBuffer();
    #endif
    
    // Initialize SPI for LoRa
    loraSPI.begin(LORA_SCK, LORA_MISO, LORA_MOSI, LORA_CS);
    
    // Initialize radio
    int state = radio.begin(LORA_FREQUENCY, 
                           LORA_BANDWIDTH, 
                           LORA_SPREADING_FACTOR, 
                           LORA_CODING_RATE, 
                           LORA_SYNC_WORD, 
                           LORA_OUTPUT_POWER, 
                           LORA_PREAMBLE_LENGTH);
    
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[LORA] SX1262 initialized successfully!");
        Serial.printf("[LORA] Frequency: %.1f MHz\n", LORA_FREQUENCY);
        Serial.printf("[LORA] Bandwidth: %.1f kHz\n", LORA_BANDWIDTH);
        Serial.printf("[LORA] Spreading Factor: %d\n", LORA_SPREADING_FACTOR);
        Serial.printf("[LORA] Coding Rate: 4/%d\n", LORA_CODING_RATE);
        Serial.printf("[LORA] Output Power: %d dBm\n", LORA_OUTPUT_POWER);
        
        radioInitialized = true;
    } else {
        Serial.print("[LORA] ERROR: Initialization failed, code: ");
        Serial.println(state);
        
        #if DISPLAY_ENABLED
        display.clearBuffer();
        display.drawStr(0, 10, "LoRa FAILED!");
        char errorStr[32];
        snprintf(errorStr, sizeof(errorStr), "Error: %d", state);
        display.drawStr(0, 22, errorStr);
        display.sendBuffer();
        #endif
        
        return;
    }
    
    // Configure DIO2 as RF switch for Heltec V4
    state = radio.setDio2AsRfSwitch(true);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[LORA] WARNING: setDio2AsRfSwitch failed, code: ");
        Serial.println(state);
    }
    
    // Set CRC
    state = radio.setCRC(LORA_CRC);
    if (state != RADIOLIB_ERR_NONE) {
        Serial.print("[LORA] WARNING: setCRC failed, code: ");
        Serial.println(state);
    } else {
        Serial.printf("[LORA] CRC: %s\n", LORA_CRC ? "ABILITATO" : "DISABILITATO");
    }
    
    // Set interrupt action on DIO1
    radio.setDio1Action(setPacketReceivedFlag);
    Serial.println("[LORA] Interrupt configured on DIO1");
    
    state = radio.setCurrentLimit(140);
    Serial.printf("[RadioLib] Current limit set to %f\n", 140);
    Serial.printf("[RadioLib] Current limit set result %d\n", state);
    
    // Riepilogo configurazione
    Serial.println("\n[LORA] ===== CONFIGURAZIONE RADIO =====");
    Serial.printf("[LORA] Frequenza: %.3f MHz\n", LORA_FREQUENCY);
    Serial.printf("[LORA] Bandwidth: %.1f kHz\n", LORA_BANDWIDTH);
    Serial.printf("[LORA] Spreading Factor: %d\n", LORA_SPREADING_FACTOR);
    Serial.printf("[LORA] Coding Rate: 4/%d\n", LORA_CODING_RATE);
    Serial.printf("[LORA] Sync Word: 0x%02X\n", LORA_SYNC_WORD);
    Serial.printf("[LORA] Preamble Length: %d\n", LORA_PREAMBLE_LENGTH);
    Serial.printf("[LORA] Output Power: %d dBm\n", LORA_OUTPUT_POWER);
    Serial.printf("[LORA] CRC: %s\n", LORA_CRC ? "SI" : "NO");
    Serial.println("[LORA] ====================================\n");
    
    // Start receiving
    state = radio.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[LORA] ✅ Started receiving - In ascolto per pacchetti...\n");
    } else {
        Serial.print("[LORA] ❌ ERROR: startReceive failed, code: ");
        Serial.println(state);
        radioInitialized = false;
    }
}

// ===========================
// LORA PACKET HANDLING
// ===========================
void handleLoRaPacket() {
    Serial.println("\n[RX] ===== HANDLING LORA PACKET =====");
    uint8_t rxBuffer[256];
    
    // Conta interrupt totali
    totalInterrupts++;
    
    // Reset interrupt flag
    packetReceived = false;
    
    // Se abbiamo appena trasmesso, ignora questo pacchetto (potrebbe essere il nostro eco)
    if (justTransmitted) {
        Serial.println("[DEBUG] Ignorato pacchetto subito dopo trasmissione (eco)");
        justTransmitted = false;
        radio.startReceive();
        return;
    }
    
    Serial.printf("[DEBUG] Interrupt #%lu - Lettura dati radio...\n", totalInterrupts);
    
    // Check if packet available
    int state = radio.readData(rxBuffer, sizeof(rxBuffer));
    
    
    if (state == RADIOLIB_ERR_NONE) {
        // Packet received successfully
        // Cattura il timestamp SUBITO per calcolo preciso finestre RX
        unsigned long rxTimestamp = millis();
        
        digitalWrite(LED_PIN, LOW);  // LED on
        
        size_t packetLength = radio.getPacketLength();
        float rssi = radio.getRSSI();
        float snr = radio.getSNR();
        
        stats.rx_received++;
        stats.rx_ok++;
        
        Serial.println("\n[RX] ---------------- LORA PACKET RECEIVED ----------------");
        Serial.printf("[RX] Length: %d bytes\n", packetLength);
        Serial.printf("[RX] RSSI: %.2f dBm\n", rssi);
        Serial.printf("[RX] SNR: %.2f dB\n", snr);
        Serial.print("[RX] Data (HEX): ");
        for (size_t i = 0; i < packetLength; i++) {
            Serial.printf("%02X ", rxBuffer[i]);
        }
        Serial.println();
        Serial.print("[RX] Data (ASCII): ");
        for (size_t i = 0; i < packetLength; i++) {
            if (rxBuffer[i] >= 32 && rxBuffer[i] <= 126) {
                Serial.printf("%c", rxBuffer[i]);
            } else {
                Serial.print(".");
            }
        }
        Serial.println();
        
        // Estrai DevAddr dal pacchetto LoRaWAN (se è un uplink valido)
        // Formato LoRaWAN uplink: MHDR(1) | DevAddr(4) | FCtrl(1) | FCnt(2) | ...
        // uint32_t devAddr = 0;
        // if (packetLength >= 7) {
        //     // DevAddr è in little-endian ai byte 1-4
        //     devAddr = ((uint32_t)rxBuffer[1]) | 
        //     ((uint32_t)rxBuffer[2] << 8) | 
        //     ((uint32_t)rxBuffer[3] << 16) | 
        //     ((uint32_t)rxBuffer[4] << 24);
        //     Serial.printf("[RX] DevAddr estratto: 0x%08X\n", devAddr);
        // }


        PullRespPacket *pullRespPacket = nullptr;
        LoRaWANHeader lorawanHeader;
        memcpy(&lorawanHeader, rxBuffer, sizeof(LoRaWANHeader));
        Serial.printf("[RX] MHDR: 0x%02X\n", lorawanHeader.mhdr);
        Serial.printf("[RX] DevAddr: 0x%08X\n", lorawanHeader.devAddr);
        Serial.printf("[RX] FCtrl: 0x%02X\n", lorawanHeader.fctrl);
        Serial.printf("[RX] FCnt: 0x%04X\n", lorawanHeader.fcnt);
        Serial.printf("[RX] FOptsLen: %d\n", lorawanHeader.getFOptsLen());
        Serial.printf("[RX] ACK: %s\n", lorawanHeader.getACK() ? "SI" : "NO");
        Serial.printf("[RX] FPending: %s\n", lorawanHeader.getFPending() ? "SI" : "NO");
        
        // Forward to ChirpStack
        if (WiFi.isConnected()) {
            // Create JSON packet
            StaticJsonDocument<512> doc;
            JsonArray rxpk_array = doc.createNestedArray("rxpk");
            JsonObject rxpk = rxpk_array.createNestedObject();
            
            // Get current time
            struct timeval tv;
            gettimeofday(&tv, NULL);
            uint32_t tmst = (uint32_t)(tv.tv_sec * 1000000 + tv.tv_usec);
            
            rxpk["tmst"] = tmst;
            rxpk["freq"] = LORA_FREQUENCY;
            rxpk["chan"] = 0;
            rxpk["rfch"] = 0;
            rxpk["stat"] = 1;
            rxpk["modu"] = "LORA";
            
            char datr[16];
            snprintf(datr, sizeof(datr), "SF%dBW%.0f", LORA_SPREADING_FACTOR, LORA_BANDWIDTH);
            rxpk["datr"] = datr;
            
            char codr[8];
            snprintf(codr, sizeof(codr), "4/%d", LORA_CODING_RATE);
            rxpk["codr"] = codr;
            
            rxpk["rssi"] = (int)rssi;
            rxpk["lsnr"] = snr;
            rxpk["size"] = packetLength;
            rxpk["data"] = encodeBase64(rxBuffer, packetLength);
            
            String jsonString;
            serializeJson(doc, jsonString);
            
            Serial.println("[GW] Forwarding LORA PACKET to ChirpStack:");
            Serial.println(jsonString);
            
            sendUdpPacket(jsonString.c_str());
            stats.rx_fw++;
            
            // IMPORTANTE: ChirpStack invia downlink SOLO come risposta a PULL_DATA!
            // Invia PULL_DATA subito dopo PUSH_DATA per richiedere downlink dalla coda
            sendPullData();
            
            // Aspetta attivamente il PULL_RESP (con timeout 500ms per dare tempo a ChirpStack)
            unsigned long waitStart = millis();
            unsigned long waitTime = 700;

            bool pullRespReceived = false;
            
            Serial.printf("[GW] ⚡ Attesa PULL_RESP for addr: 0x%08X downlink to queue after uplink (%lu ms), max wait time: %lu ms\n", lorawanHeader.devAddr, millis() - waitStart, waitTime);
            while (millis() - waitStart < waitTime) {  // Max 500ms di attesa (aumentato da 200ms)
                handleUdpDownlink(); // Controlla continuamente se arriva
                                   // PULL_RESP

                    
                pullRespPacket = dowQueue.findFirstByDevAddr(lorawanHeader.devAddr);
                if (pullRespPacket) {
                    pullRespReceived = true;
                    Serial.printf("[GW] ⚡ PULL_RESP ricevuto dopo uplink (%lu ms)\n", millis() - waitStart);
                    Serial.printf("[GW] %d messaggio/i in coda, sarà/anno trasmesso/i nelle finestre RX1/RX2\n", dowQueue.size());
                    break;
                }
            //   if (dowQueue.findFirstByDevAddr(uint32_t devAddr)) {
                
            //   }
                
                // Se abbiamo ricevuto un nuovo downlink, esci dal loop
                // if (downlinkQueueCount > 0 || pendingDownlink.pending) {
                //     pullRespReceived = true;
                //     Serial.printf("[PULL] ⚡ PULL_RESP ricevuto dopo uplink (%lu ms)\n", millis() - waitStart);
                //     Serial.printf("[PULL] %d messaggio/i in coda, sarà/anno trasmesso/i nelle finestre RX1/RX2\n", downlinkQueueCount);
                //     break;
                // }
                
                delay(5);  // Piccolo delay per non saturare la CPU
            }
            
            if (!pullRespReceived) {
                Serial.println("[PULL] Nessun PULL_RESP entro 500ms (normale se coda vuota)");
                Serial.println("[PULL] Nota: I downlink dalla coda possono arrivare anche dopo PULL_DATA periodici");
            }
            
        } else {
            Serial.println("[UDP] ERROR: WiFi disconnected, packet not forwarded");
        }

        
        
        Serial.println("[RX] =============================\n");
        
        digitalWrite(LED_PIN, HIGH);  // LED off
        
        // Invia risposta "OK" al nodo se abilitato
        #if AUTO_DOWNLINK_ENABLED
        if (lorawanHeader.devAddr != 0 && pullRespPacket) {
          sendDownlinkResponse(rxTimestamp, pullRespPacket);
        } else {
            Serial.println("[DOWNLINK] DevAddr non valido o PULL_RESP non trovato, skip downlink");
        }
        #endif
        radio.startReceive();
        
    } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
        // Timeout - nessun pacchetto ricevuto
        timeouts++;
        Serial.printf("[DEBUG] Timeout (totale: %lu) - Nessun pacchetto\n", timeouts);
        radio.startReceive();
    } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
        // CRC ERROR - MA I DATI SONO ARRIVATI!
        // Per LoRaWAN, accettiamo comunque (ha il suo MIC per verificare)
        crcErrors++;
        Serial.printf("[DEBUG] CRC ERROR (totale: %lu)\n", crcErrors);
        // stats.rx_received++;
        // stats.rx_ok++; // Contiamo come OK per LoRaWAN
        
        // digitalWrite(LED_PIN, LOW);  // LED on
        
        // size_t packetLength = radio.getPacketLength();
        // float rssi = radio.getRSSI();
        // float snr = radio.getSNR();
        
        // Serial.println("\n[RX] ===== PACKET RECEIVED (CRC ignored) =====");
        // Serial.printf("[RX] Length: %d bytes\n", packetLength);
        // Serial.printf("[RX] RSSI: %.2f dBm\n", rssi);
        // Serial.printf("[RX] SNR: %.2f dB\n", snr);
        // Serial.print("[RX] Data (HEX): ");
        // for (size_t i = 0; i < packetLength; i++) {
        //     Serial.printf("%02X ", rxBuffer[i]);
        // }
        // Serial.println();
        // Serial.print("[RX] Data (ASCII): ");
        // for (size_t i = 0; i < packetLength; i++) {
        //     if (rxBuffer[i] >= 32 && rxBuffer[i] <= 126) {
        //         Serial.printf("%c", rxBuffer[i]);
        //     } else {
        //         Serial.print(".");
        //     }
        // }
        // Serial.println();
        // Serial.printf("[RX] Note: CRC error (totale: %lu) ma dati validi\n", crcErrors);
        
        // // Forward to ChirpStack COMUNQUE
        // if (WiFi.isConnected()) {
        //     StaticJsonDocument<512> doc;
        //     JsonArray rxpk_array = doc.createNestedArray("rxpk");
        //     JsonObject rxpk = rxpk_array.createNestedObject();
            
        //     struct timeval tv;
        //     gettimeofday(&tv, NULL);
        //     uint32_t tmst = (uint32_t)(tv.tv_sec * 1000000 + tv.tv_usec);
            
        //     rxpk["tmst"] = tmst;
        //     rxpk["freq"] = LORA_FREQUENCY;
        //     rxpk["chan"] = 0;
        //     rxpk["rfch"] = 0;
        //     rxpk["stat"] = 1;
        //     rxpk["modu"] = "LORA";
            
        //     char datr[16];
        //     snprintf(datr, sizeof(datr), "SF%dBW%.0f", LORA_SPREADING_FACTOR, LORA_BANDWIDTH);
        //     rxpk["datr"] = datr;
            
        //     char codr[8];
        //     snprintf(codr, sizeof(codr), "4/%d", LORA_CODING_RATE);
        //     rxpk["codr"] = codr;
            
        //     rxpk["rssi"] = (int)rssi;
        //     rxpk["lsnr"] = snr;
        //     rxpk["size"] = packetLength;
        //     rxpk["data"] = encodeBase64(rxBuffer, packetLength);
            
        //     String jsonString;
        //     serializeJson(doc, jsonString);
            
        //     Serial.println("[UDP] Forwarding to ChirpStack:");
        //     Serial.println(jsonString);
            
        //     sendUdpPacket(jsonString.c_str());
        //     stats.rx_fw++;
        // }
        
        // Serial.println("[RX] =============================================\n");
        
        // delay(100);
        // digitalWrite(LED_PIN, HIGH);  // LED off
        
        radio.startReceive();
    } else {
        // Altri errori
        otherErrors++;
        Serial.printf("\n[RX] ===== ERROR %d =====\n", state);
        Serial.printf("[RX] Totale altri errori: %lu\n", otherErrors);
        Serial.println("[RX] ======================\n");
        radio.startReceive();
    }
}

// ===========================
// UDP FUNCTIONS
// ===========================
void sendUdpPacket(const char* jsonData) {
    if (!WiFi.isConnected()) {
        Serial.println("[UDP] ERROR: WiFi not connected");
        return;
    }
    
    udpClient.beginPacket(serverIP, SERVER_PORT);
    
    // Protocol version (always 0x02)
    udpClient.write((uint8_t)0x02);
    
    // Random token
    uint16_t token = random(0xFFFF);
    udpClient.write((uint8_t)(token >> 8));
    udpClient.write((uint8_t)(token & 0xFF));
    
    // Identifier: PUSH_DATA = 0x00
    udpClient.write((uint8_t)0x00);

    // Gateway ID (8 bytes)
    for (int i = 7; i >= 0; i--) {
        udpClient.write((uint8_t)((gatewayId >> (i * 8)) & 0xFF));
    }
    
    // JSON data
    udpClient.print(jsonData);
    
    int result = udpClient.endPacket();
    
    if (result) {
        Serial.println("[SEND UDP PACKET] Packet sent successfully");
    } else {
        Serial.println("[SEND UDP PACKET] ERROR: Failed to send packet");
    }
}

void sendStatPacket() {
    if (!WiFi.isConnected()) return;
    
    StaticJsonDocument<256> doc;
    JsonObject stat = doc.createNestedObject("stat");
    
    // Get current time
    time_t now = time(nullptr);
    struct tm timeinfo;
    gmtime_r(&now, &timeinfo);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S GMT", &timeinfo);
    
    stat["time"] = timestamp;
    stat["rxnb"] = stats.rx_received;
    stat["rxok"] = stats.rx_ok;
    stat["rxfw"] = stats.rx_fw;
    stat["ackr"] = 100.0;
    stat["dwnb"] = stats.tx_received;
    stat["txnb"] = stats.tx_emitted;
    
    String jsonString;
    serializeJson(doc, jsonString);
    
    Serial.println("[STAT] Sending statistics:");
    Serial.println(jsonString);
    
    sendUdpPacket(jsonString.c_str());
}

// ===========================
// PULL_DATA - Chiede downlink a ChirpStack
// ===========================
void sendPullData() {
//   Serial.println("---- SEND PULL_DATA ----");
  unsigned long startMillis = millis();
    if (!WiFi.isConnected()) return;
    
    udpClient.beginPacket(serverIP, SERVER_PORT);
    
    // Protocol version (always 0x02)
    udpClient.write((uint8_t)0x02);
    
    // Random token
    uint16_t token = random(0xFFFF);
    udpClient.write((uint8_t)(token >> 8));
    udpClient.write((uint8_t)(token & 0xFF));
    
    // Identifier: PULL_DATA = 0x02
    udpClient.write((uint8_t)0x02);
    
    // Gateway ID (8 bytes)
    for (int i = 7; i >= 0; i--) {
        udpClient.write((uint8_t)((gatewayId >> (i * 8)) & 0xFF));
    }
    
    udpClient.endPacket();
    unsigned long endMillis = millis();
    Serial.printf("[PULL] Sent PULL_DATA to ChirpStack in: %lu ms\n", endMillis - startMillis);
}

// ===========================
// Gestisce pacchetti UDP da ChirpStack
// ===========================
void handleUdpDownlink() {
    int packetSize = udpClient.parsePacket();
    if (packetSize == 0) return;  // Nessun pacchetto
    
    Serial.printf("[handleUdpDownlink] Received packet size: %d\n", packetSize);
    
    uint8_t udpBuffer[512];
    int len = udpClient.read(udpBuffer, sizeof(udpBuffer));

    if (len < 4)
      return; // Pacchetto troppo corto
    if (len > sizeof(udpBuffer)) {
      Serial.println("[handleUdpDownlink] ❌ Pacchetto troppo grande");
      return;
    }

    SemtechUdpPackage packet;
    if (!packet.initFromBuffer(udpBuffer, len)) {
    //   Serial.println("[SemtechUdpPackage] ✅ Packet parsed successfully!");
    //   packet.printDebug();
    //   Serial.println("DevAddr: " + String(packet.devAddr));
    //   Serial.println("Token: " + String(packet.header.getToken()));
    //   Serial.printf("getMessageType: 0x%02X\n", static_cast<uint8_t>(packet.header.getMessageType()));
    //   Serial.println("getMessageTypeString: " + String(packet.header.getMessageTypeString()));
    //   Serial.println("FPort: " + String(packet.fport));
    //   Serial.println("MAC Command: " + String(packet.isMacCommand));
    //   Serial.println("Payload size: " + String(packet.decodedLength));

    // } else {
        Serial.println("[handleUdpDownlink] ❌ Errore parsing SemtechUdpPackage");
        return;
    }

    if (packet.getMessageType() == SemtechMessageType::PULL_ACK) {
      return;
    }else  if (packet.getMessageType() == SemtechMessageType::PULL_RESP) {
      PullResponseData responseData;
      if (packet.getPullResponse(responseData)) {
        Serial.println("[handleUdpDownlink] ✅ PULL_RESP ricevuto - downlink disponibile!");
        responseData.printDebug();

        PullRespPacket pullRespPacket;
        pullRespPacket.token = packet.getToken();
        pullRespPacket.responseData = responseData;
        if (dowQueue.add(pullRespPacket)) {
            Serial.println("[handleUdpDownlink] ✅ PULL_RESP aggiunto alla coda");
        } else {
            dowQueue.printDebug();
            Serial.println("[handleUdpDownlink] ❌ PULL_RESP scartato: coda piena");
        }
      }else{
        Serial.println("[handleUdpDownlink] ❌ PULL_RESP scartato: errore parsing PullResponseData");
        return;
      }
    } else {
        Serial.printf("[handleUdpDownlink] ? not implemented getMessageType: 0x%02X\n", static_cast<uint8_t>(packet.getMessageType()));
        Serial.println("[handleUdpDownlink] getMessageTypeString: " + String(packet.getMessageTypeString()));
        packet.printDebug();
    }
    
    
    //   Serial.println("[PULL] ✅ PULL_RESP ricevuto - downlink disponibile!");
    //   Serial.println("[PULL] DevAddr: " + String(packet.devAddr));
    //   packet.printDebug();
    //   dowQueue.add(packet);
    //   dowQueue.printDebug();
    // }else{
    //   Serial.println("[PULL] NO_HOPE");
    //   return;
    // }
    
    // uint8_t version = udpBuffer[0];
    // uint16_t token = (udpBuffer[1] << 8) | udpBuffer[2];
    // uint8_t identifier = udpBuffer[3];
    
    // Serial.printf("[UDP] Ricevuto pacchetto: ver=%d, token=%04X, id=%d, len=%d\n", 
    //               version, token, identifier, len);
    
    // if (identifier == 0x03) {  // PULL_RESP = downlink disponibile!
    //     Serial.println("[PULL] ✅ PULL_RESP ricevuto - downlink disponibile!");
        
    //     // Il payload JSON inizia dal byte 4
    //     if (len > 4) {
    //         // Parse JSON per estrarre il downlink
    //         StaticJsonDocument<512> doc;
    //         DeserializationError error = deserializeJson(doc, (char*)&udpBuffer[4], len - 4);
            
    //         if (error) {
    //             Serial.printf("[PULL] ❌ Errore parsing JSON: %s\n", error.c_str());
    //             Serial.print("[PULL] JSON raw: ");
    //             for (int i = 4; i < len && i < 100; i++) {
    //                 Serial.printf("%c", udpBuffer[i]);
    //             }
    //             Serial.println();
    //         } else if (doc.containsKey("txpk")) {
    //             JsonObject txpk = doc["txpk"];
                
    //             // DEBUG: Stampa tutti i campi del JSON per capire cosa invia ChirpStack
    //             Serial.println("[PULL] 📋 Campi disponibili nel JSON txpk:");
    //             for (JsonPair kv : txpk) {
    //                 const char* key = kv.key().c_str();
    //                 JsonVariant value = kv.value();
    //                 if (value.is<bool>()) {
    //                     Serial.printf("[PULL]   - %s: %s\n", key, value.as<bool>() ? "true" : "false");
    //                 } else if (value.is<int>()) {
    //                     Serial.printf("[PULL]   - %s: %d\n", key, value.as<int>());
    //                 } else if (value.is<unsigned int>()) {
    //                     Serial.printf("[PULL]   - %s: %u\n", key, value.as<unsigned int>());
    //                 } else if (value.is<const char*>()) {
    //                     Serial.printf("[PULL]   - %s: %s\n", key, value.as<const char*>());
    //                 } else {
    //                     Serial.printf("[PULL]   - %s: (altro tipo)\n", key);
    //                 }
    //             }
                
    //             // Estrai i dati del downlink
    //             if (txpk.containsKey("data")) {
    //                 const char* base64Data = txpk["data"];
    //                 uint32_t tmst = txpk["tmst"] | 0;  // Timestamp (opzionale)
    //                 bool imme = txpk["imme"] | false;  // Immediate transmission (Classe C)
                    
    //                 Serial.printf("[PULL] Downlink data (base64): %s\n", base64Data);
    //                 Serial.printf("[PULL] Downlink tmst: %lu\n", tmst);
    //                 Serial.printf("[PULL] ⚠️ Immediate (Classe C): %s\n", imme ? "SI ✅" : "NO ❌");
                    
    //                 if (!imme) {
    //                     Serial.println("[PULL] ⚠️ ATTENZIONE: ChirpStack sta inviando questo messaggio come Classe A!");
    //                     Serial.println("[PULL] Per abilitare Classe C, configura il device in ChirpStack:");
    //                     Serial.println("[PULL]   1. Vai su Device → [Il tuo device]");
    //                     Serial.println("[PULL]   2. Tab 'Configuration' o 'Device Profile'");
    //                     Serial.println("[PULL]   3. Imposta 'Device Class' a 'C' (Class C)");
    //                     Serial.println("[PULL]   4. Salva e riprova");
    //                 }
                    
    //                 // Decodifica base64 in buffer temporaneo
    //                 uint8_t tempBuffer[256];
    //                 size_t decodedLen = decodeBase64(base64Data, tempBuffer, sizeof(tempBuffer));
                    
    //                 if (decodedLen > 0) {
    //                     // Estrai DevAddr dal pacchetto LoRaWAN
    //                     uint32_t devAddr = 0;
    //                     bool isMacCommand = false;
    //                     if (decodedLen >= 9) {
    //                         // DevAddr è ai byte 1-4 (little-endian)
    //                         devAddr = ((uint32_t)tempBuffer[1]) | 
    //                                  ((uint32_t)tempBuffer[2] << 8) | 
    //                                  ((uint32_t)tempBuffer[3] << 16) | 
    //                                  ((uint32_t)tempBuffer[4] << 24);
                            
    //                         // Determina se è un comando MAC
    //                         uint8_t fctrl = tempBuffer[5];
    //                         uint8_t foptsLen = fctrl & 0x0F;
    //                         size_t fportPos = 8 + foptsLen;
    //                         if (fportPos < decodedLen - 4) {
    //                             uint8_t fport = tempBuffer[fportPos];
    //                             isMacCommand = (fport == 0);
    //                         }
    //                     }
                        
    //                     Serial.printf("[PULL] DevAddr destinatario: 0x%08X\n", devAddr);
                        
    //                     // Conta quanti messaggi ci sono già per questo DevAddr
    //                     uint8_t countForDevAddr = 0;
    //                     for (int i = 0; i < downlinkQueueCount; i++) {
    //                         if (downlinkQueue[i].devAddr == devAddr && downlinkQueue[i].pending) {
    //                             countForDevAddr++;
    //                         }
    //                     }
                        
    //                     // Se è Classe C (immediate), trasmette subito senza aggiungere alla coda
    //                     if (imme) {
    //                         Serial.println("[PULL] ⚡ CLASSE C: Trasmissione immediata!");
                            
    //                         // Trasmetti immediatamente
    //                         if (radioInitialized) {
    //                             digitalWrite(LED_PIN, LOW);
    //                             radio.invertIQ(true);
    //                             int state = radio.transmit(tempBuffer, decodedLen);
    //                             radio.invertIQ(false);
    //                             digitalWrite(LED_PIN, HIGH);
                                
    //                             if (state == RADIOLIB_ERR_NONE) {
    //                                 Serial.println("[PULL] ✅ Classe C trasmesso con successo!");
    //                                 sendTxAck(token);
    //                                 stats.tx_emitted++;
    //                                 justTransmitted = true;
                                    
    //                                 // Riavvia ricezione
    //                                 radio.startReceive();
    //                             } else {
    //                                 Serial.printf("[PULL] ❌ Errore trasmissione Classe C: %d\n", state);
    //                             }
    //                         } else {
    //                             Serial.println("[PULL] ❌ Radio non inizializzata, skip Classe C");
    //                         }
    //                     } else {
    //                         // Classe A: aggiungi alla coda per trasmissione dopo uplink
    //                         if (downlinkQueueCount < MAX_DOWNLINK_QUEUE_SIZE && 
    //                             countForDevAddr < MAX_DOWNLINK_PER_DEVADDR) {
    //                             PendingDownlink* dl = &downlinkQueue[downlinkQueueCount];
    //                             dl->length = decodedLen;
    //                             memcpy(dl->data, tempBuffer, decodedLen);
    //                             dl->tmst = tmst;
    //                             dl->token = token;
    //                             dl->devAddr = devAddr;
    //                             dl->pending = true;
    //                             dl->isMacCommand = isMacCommand;
    //                             dl->isClassC = false;
                                
    //                             downlinkQueueCount++;
    //                             Serial.printf("[PULL] Messaggio Classe A aggiunto alla coda per DevAddr 0x%08X (%d/%d per questo nodo, %d/%d totale)\n", 
    //                                          devAddr, countForDevAddr + 1, MAX_DOWNLINK_PER_DEVADDR, 
    //                                          downlinkQueueCount, MAX_DOWNLINK_QUEUE_SIZE);
    //                         } else {
    //                             if (countForDevAddr >= MAX_DOWNLINK_PER_DEVADDR) {
    //                                 Serial.printf("[PULL] ⚠️ Coda piena per DevAddr 0x%08X! (max %d messaggi per nodo)\n", 
    //                                              devAddr, MAX_DOWNLINK_PER_DEVADDR);
    //                             } else {
    //                                 Serial.println("[PULL] ⚠️ Coda globale piena! Messaggio scartato.");
    //                             }
    //                         }
    //                     }
                        
    //                     // Mantieni compatibilità con codice esistente
    //                     pendingDownlink.length = decodedLen;
    //                     memcpy(pendingDownlink.data, tempBuffer, decodedLen);
    //                     pendingDownlink.tmst = tmst;
    //                     pendingDownlink.token = token;
    //                     pendingDownlink.devAddr = devAddr;
    //                     pendingDownlink.pending = true;
                        
    //                     Serial.printf("[PULL] ✅ Downlink decodificato: %d bytes\n", pendingDownlink.length);
    //                     Serial.printf("[PULL] Token salvato: 0x%04X (per TX_ACK)\n", token);
    //                     Serial.print("[PULL] Downlink (HEX): ");
    //                     for (size_t i = 0; i < pendingDownlink.length; i++) {
    //                         Serial.printf("%02X ", pendingDownlink.data[i]);
    //                     }
    //                     Serial.println();
                        
    //                     // Decodifica pacchetto LoRaWAN per mostrare informazioni
    //                     decodeLoRaWANPacket(pendingDownlink.data, pendingDownlink.length);
                        
    //                     // Verifica se è un comando MAC o payload applicativo
    //                     // Il FPort si trova dopo: MHDR(1) + DevAddr(4) + FCtrl(1) + FCnt(2) + FOpts(variabile)
    //                     if (pendingDownlink.length >= 9) {
    //                         uint8_t fctrl = pendingDownlink.data[5];
    //                         uint8_t foptsLen = fctrl & 0x0F;  // FOptsLen è nei primi 4 bit di FCtrl
    //                         size_t fportPos = 8 + foptsLen;  // Posizione FPort dopo header + FOpts
                            
    //                         if (fportPos < pendingDownlink.length - 4) {  // -4 per MIC
    //                             uint8_t fport = pendingDownlink.data[fportPos];
    //                             if (fport == 0) {
    //                                 Serial.println("[PULL] ⚠️ ATTENZIONE: Questo è un COMANDO MAC (FPort=0), non un payload applicativo!");
    //                                 Serial.println("[PULL] IMPORTANTE: I comandi MAC DEVONO essere trasmessi per mantenere la sincronizzazione!");
    //                                 Serial.println("[PULL] Se li ignori, il frame counter si desincronizza e i successivi messaggi avranno MIC errato.");
    //                                 Serial.println("[PULL] ChirpStack sta dando priorità ai comandi MAC rispetto ai downlink dalla coda.");
    //                                 Serial.println("[PULL] SOLUZIONE: In ChirpStack, vai su Device → MAC Settings e disabilita:");
    //                                 Serial.println("[PULL]   - LinkCheckReq (se non necessario)");
    //                                 Serial.println("[PULL]   - LinkADRReq (se non necessario)");
    //                                 Serial.println("[PULL]   - DutyCycleReq (se non necessario)");
    //                                 Serial.println("[PULL] Oppure aspetta che tutti i comandi MAC siano stati inviati.");
    //                                 Serial.println("[PULL] ⚠️ Il comando MAC verrà comunque trasmesso per evitare desincronizzazione!");
    //                             } else {
    //                                 Serial.printf("[PULL] ✅ Questo è un PAYLOAD APPLICATIVO (FPort=%d) dalla coda!\n", fport);
    //                             }
    //                         }
    //                     }
                        
    //                     Serial.println("[PULL] ⚠️ IMPORTANTE: Il downlink sarà trasmesso al prossimo uplink!");
    //                 } else {
    //                     Serial.println("[PULL] ❌ Errore decodifica base64");
    //                 }
    //             } else {
    //                 Serial.println("[PULL] ⚠️ JSON txpk non contiene campo 'data'");
    //             }
    //         } else {
    //             Serial.println("[PULL] ⚠️ JSON non contiene campo 'txpk'");
    //             Serial.print("[PULL] JSON ricevuto: ");
    //             serializeJson(doc, Serial);
    //             Serial.println();
    //         }
    //     } else {
    //         Serial.println("[PULL] ⚠️ PULL_RESP troppo corto (manca payload JSON)");
    //     }
    // }
    // else if (identifier == 0x01) {  // PUSH_ACK
    //     Serial.println("[UDP] PUSH_ACK ricevuto");
    // }
    // else if (identifier == 0x04) {  // PULL_ACK
    //     Serial.println("[UDP] PULL_ACK ricevuto");
    // }
}



unsigned long getElapsedTime(unsigned long referenceMillis) {
  return millis() - referenceMillis;
}
// ===========================
// TRASMISSIONE DOWNLINK
// Ritorna true se la trasmissione è riuscita, false altrimenti
// ===========================
bool transmitDownlink(uint8_t* data, size_t length, unsigned long rxTimestamp) {
    if (!radioInitialized) {
        Serial.println("[TX_DL] Radio non inizializzata");
        radio.startReceive();
        return false;
    }
    
    Serial.println("\n[TX_DL] ===== TRASMISSIONE DOWNLINK =====");
    Serial.printf("[TX_DL] Lunghezza: %d bytes\n", length);
    Serial.print("[TX_DL] Frame (HEX): ");
    for (size_t i = 0; i < length; i++) {
        Serial.printf("%02X ", data[i]);
    }
    Serial.println();
    
    // Calcola tempo trascorso dalla ricezione
    unsigned long elapsed = getElapsedTime(rxTimestamp);
    Serial.printf("[TX_DL] Tempo trascorso dalla RX: %lu ms\n", elapsed);
    
    bool transmitted = false;
    int rxWindow = 0;
    
    // ===== TENTATIVO RX1 =====
    if (elapsed < RX1_DELAY) {
        unsigned long waitTime = RX1_DELAY - elapsed;
        Serial.printf("[TX_DL] Attendo %lu ms per finestra RX1...\n", waitTime);
        delay(waitTime);
        
        unsigned long txStart = millis();
        unsigned long actualDelay = txStart - rxTimestamp;
        Serial.printf("[TX_DL] >>> FINESTRA RX1 (delay reale: %lu ms) <<<\n", actualDelay);
        
        digitalWrite(LED_PIN, LOW);
        
        // LoRaWAN usa IQ invertito per downlink!
        radio.invertIQ(true);
        
        int state = radio.transmit(data, length);
        
        // Ripristina IQ normale
        radio.invertIQ(false);
        
        unsigned long txEnd = millis();
        unsigned long txDuration = txEnd - txStart;
        
        if (state == RADIOLIB_ERR_NONE) {
            Serial.printf("[TX_DL] ✅ Trasmesso in RX1! (TX: %lu ms)\n", txDuration);
            stats.tx_emitted++;
            justTransmitted = true;
            transmitted = true;
            rxWindow = 1;
        } else {
            Serial.printf("[TX_DL] ❌ Errore RX1: %d\n", state);
        }
        
        digitalWrite(LED_PIN, HIGH);
    } else {
        Serial.printf("[TX_DL] ⚠️ RX1 persa (elapsed: %lu ms)\n", elapsed);
    }
    
    // ===== TENTATIVO RX2 (solo per debug, normalmente skip se RX1 OK) =====
    if (!transmitted) {
        elapsed = getElapsedTime(rxTimestamp);
        
        if (elapsed < RX2_DELAY) {
            unsigned long waitTime = RX2_DELAY - elapsed;
            Serial.printf("[TX_DL] Attendo %lu ms per finestra RX2...\n", waitTime);
            delay(waitTime);
            
            unsigned long txStart = millis();
            unsigned long actualDelay = txStart - rxTimestamp;
            Serial.printf("[TX_DL] >>> FINESTRA RX2 (delay reale: %lu ms) <<<\n", actualDelay);
            
            digitalWrite(LED_PIN, LOW);
            
            radio.invertIQ(true);
            int state = radio.transmit(data, length);
            radio.invertIQ(false);
            
            unsigned long txEnd = millis();
            unsigned long txDuration = txEnd - txStart;
            
            if (state == RADIOLIB_ERR_NONE) {
                Serial.printf("[TX_DL] ✅ Trasmesso in RX2! (TX: %lu ms)\n", txDuration);
                stats.tx_emitted++;
                justTransmitted = true;
                transmitted = true;
                rxWindow = 2;
            } else {
                Serial.printf("[TX_DL] ❌ Errore RX2: %d\n", state);
            }
            
            digitalWrite(LED_PIN, HIGH);
        } else {
            Serial.printf("[TX_DL] ❌ RX2 persa (elapsed: %lu ms)\n", elapsed);
        }
    }
    
    // ===== RIEPILOGO =====
    if (transmitted) {
        Serial.printf("[TX_DL] ✅ Successo! Finestra: RX%d\n", rxWindow);
    } else {
        Serial.println("[TX_DL] ❌ FALLITO: Nessuna finestra disponibile");
    }
    
    Serial.println("[TX_DL] ==============================\n");
    
    // Riavvia la ricezione
    int state = radio.startReceive();
    if (state == RADIOLIB_ERR_NONE) {
        Serial.println("[LORA] Radio tornata in ascolto");
    } else {
        Serial.printf("[LORA] ❌ Errore riavvio ricezione: %d\n", state);
        radioInitialized = false;
    }
    
    // Ritorna true se la trasmissione è riuscita
    return transmitted;
}

// ===========================
// DECODIFICA PACCHETTO LORAWAN
// ===========================
void decodeLoRaWANPacket(uint8_t* data, size_t length) {
    if (length < 12) {
        Serial.println("[DECODE] ⚠️ Pacchetto troppo corto per essere LoRaWAN valido (min 12 bytes)");
        return;
    }
    
    Serial.println("\n[DECODE] ===== DECODIFICA PACCHETTO LORAWAN =====");
    
    // MHDR (1 byte)
    uint8_t mhdr = data[0];
    uint8_t mtype = (mhdr >> 5) & 0x07;  // Bits 7-5
    uint8_t major = mhdr & 0x03;  // Bits 1-0
    
    const char* mtypeNames[] = {
        "Join Request", "Join Accept", "Unconfirmed Data Up", 
        "Unconfirmed Data Down", "Confirmed Data Up", "Confirmed Data Down",
        "RFU", "Proprietary"
    };
    
    Serial.printf("[DECODE] MHDR: 0x%02X\n", mhdr);
    Serial.printf("[DECODE] MType: %d (%s)\n", mtype, mtype < 8 ? mtypeNames[mtype] : "Unknown");
    Serial.printf("[DECODE] Major: %d (LoRaWAN R%d)\n", major, major + 1);
    
    // DevAddr (4 bytes, little-endian)
    uint32_t devAddr = ((uint32_t)data[1]) | 
                       ((uint32_t)data[2] << 8) | 
                       ((uint32_t)data[3] << 16) | 
                       ((uint32_t)data[4] << 24);
    
    Serial.printf("[DECODE] DevAddr: 0x%08X\n", devAddr);
    Serial.printf("[DECODE] DevAddr (little-endian bytes): %02X %02X %02X %02X\n", 
                  data[1], data[2], data[3], data[4]);
    
    // FCtrl (1 byte)
    uint8_t fctrl = data[5];
    bool adr = (fctrl & 0x80) != 0;
    bool adrAckReq = (fctrl & 0x40) != 0;
    bool ack = (fctrl & 0x20) != 0;
    bool classB = (fctrl & 0x10) != 0;
    uint8_t foptsLen = fctrl & 0x0F;
    
    Serial.printf("[DECODE] FCtrl: 0x%02X\n", fctrl);
    Serial.printf("[DECODE]   ADR: %s\n", adr ? "SI" : "NO");
    Serial.printf("[DECODE]   ADRACKReq: %s\n", adrAckReq ? "SI" : "NO");
    Serial.printf("[DECODE]   ACK: %s\n", ack ? "SI" : "NO");
    Serial.printf("[DECODE]   ClassB: %s\n", classB ? "SI" : "NO");
    Serial.printf("[DECODE]   FOptsLen: %d\n", foptsLen);
    
    // FCnt (2 bytes, little-endian)
    uint16_t fcnt = ((uint16_t)data[6]) | ((uint16_t)data[7] << 8);
    Serial.printf("[DECODE] FCnt: %d (0x%04X)\n", fcnt, fcnt);
    
    size_t pos = 8;
    
    // FOpts (se presente, lunghezza in FCtrl)
    if (foptsLen > 0) {
        if (pos + foptsLen <= length - 4) {  // -4 per MIC
            Serial.printf("[DECODE] FOpts (%d bytes): ", foptsLen);
            for (uint8_t i = 0; i < foptsLen; i++) {
                Serial.printf("%02X ", data[pos + i]);
            }
            Serial.println();
            pos += foptsLen;
        } else {
            Serial.println("[DECODE] ⚠️ FOptsLen maggiore dello spazio disponibile");
        }
    }
    
    // FPort (1 byte, presente solo se c'è payload)
    uint8_t fport = 0;
    bool hasFPort = false;
    if (pos < length - 4) {  // C'è spazio per FPort + payload + MIC
        fport = data[pos];
        hasFPort = true;
        pos++;
        
        Serial.printf("[DECODE] FPort: %d\n", fport);
        
        // FRMPayload (se presente)
        size_t payloadLen = length - pos - 4;  // -4 per MIC
        if (payloadLen > 0) {
            Serial.printf("[DECODE] FRMPayload (%d bytes): ", payloadLen);
            for (size_t i = 0; i < payloadLen && i < 32; i++) {  // Limita a 32 bytes per display
                Serial.printf("%02X ", data[pos + i]);
            }
            if (payloadLen > 32) {
                Serial.print("...");
            }
            Serial.println();
            
            // Prova a decodificare come ASCII se sembra testo
            Serial.print("[DECODE] FRMPayload (ASCII): ");
            for (size_t i = 0; i < payloadLen && i < 32; i++) {
                uint8_t b = data[pos + i];
                if (b >= 32 && b <= 126) {
                    Serial.printf("%c", b);
                } else {
                    Serial.print(".");
                }
            }
            if (payloadLen > 32) {
                Serial.print("...");
            }
            Serial.println();
        }
    }
    
    // MIC (4 bytes, ultimi 4 bytes)
    Serial.printf("[DECODE] MIC: %02X %02X %02X %02X\n", 
                  data[length-4], data[length-3], data[length-2], data[length-1]);
    
    Serial.println("[DECODE] ===========================================\n");
}

// ===========================
// TX_ACK - Conferma trasmissione downlink a ChirpStack
// ===========================
void sendTxAck(uint16_t token) {
    if (!WiFi.isConnected()) {
        Serial.println("[TX_ACK] WiFi non connesso, skip TX_ACK");
        return;
    }
    
    Serial.printf("[TX_ACK] Invio TX_ACK con token 0x%04X\n", token);
    
    udpClient.beginPacket(serverIP, SERVER_PORT);
    
    // Protocol version (always 0x02)
    udpClient.write((uint8_t)0x02);
    
    // Token (stesso del PULL_RESP)
    udpClient.write((uint8_t)(token >> 8));
    udpClient.write((uint8_t)(token & 0xFF));
    
    // Identifier: TX_ACK = 0x05
    udpClient.write((uint8_t)0x05);
    
    // Gateway ID (8 bytes)
    for (int i = 7; i >= 0; i--) {
        udpClient.write((uint8_t)((gatewayId >> (i * 8)) & 0xFF));
    }
    
    int result = udpClient.endPacket();
    
    if (result) {
        Serial.println("[TX_ACK] ✅ TX_ACK inviato con successo");
    } else {
        Serial.println("[TX_ACK] ❌ Errore invio TX_ACK");
    }
}

// ===========================
// DOWNLINK RESPONSE
// ===========================
void sendDownlinkResponse(unsigned long rxTimestamp, PullRespPacket *pullRespPacket) {
    if (!radioInitialized) {
        Serial.println("[DOWNLINK] Radio non inizializzata");
        radio.startReceive();
        return;
    }

    // if (!pullRespPacket) {
    //     Serial.println("[DOWNLINK] PULL_RESP non trovato");
    //     return;
    // }
    
    // ===== PRIORITÀ 1: Usa downlink da ChirpStack se disponibile =====
    // Strategia: Cerca messaggi per questo DevAddr specifico
    // Trasmetti fino a 2 messaggi per questo nodo - uno in RX1 e uno in RX2
    // int messagesForDevAddr = 0;
    // int indices[MAX_DOWNLINK_PER_DEVADDR];
    
    // // Cerca messaggi per questo DevAddr
    // for (int i = 0; i < downlinkQueueCount && messagesForDevAddr < MAX_DOWNLINK_PER_DEVADDR; i++) {
    //     if (downlinkQueue[i].pending && downlinkQueue[i].devAddr == devAddr) {
    //         indices[messagesForDevAddr] = i;
    //         messagesForDevAddr++;
    //     }
    // }
    
    if (pullRespPacket) {
        // Serial.printf("[DOWNLINK] ✅ %d messaggio/i in coda per DevAddr 0x%08X!\n", messagesForDevAddr, devAddr);
        
        // Trasmetti il primo messaggio in RX1
        // PendingDownlink* dl1 = &downlinkQueue[indices[0]];
        Serial.println("[DOWNLINK] 📤 Trasmissione messaggio 1 in RX1...");
        bool tx1Success = transmitDownlink(
            pullRespPacket->responseData.decodedPayload,  // Dati binari decodificati (non base64!)
            pullRespPacket->responseData.decodedLength,    // Lunghezza corretta
            rxTimestamp
        );
        
        if (tx1Success) {
            Serial.println("[DOWNLINK] ✅ Messaggio 1 trasmesso con successo, invio TX_ACK");
            sendTxAck(pullRespPacket->token);

            dowQueue.remove(pullRespPacket);
            // // Rimuovi dalla coda (sposta gli elementi successivi)
            // for (int i = indices[0]; i < downlinkQueueCount - 1; i++) {
            //     downlinkQueue[i] = downlinkQueue[i + 1];
            // }
            // downlinkQueueCount--;
            
            // Aggiorna gli indici dopo la rimozione
            // if (messagesForDevAddr > 1 && indices[1] > indices[0]) {
            //     indices[1]--;
            // }
            
            // Se c'è un secondo messaggio per questo DevAddr, trasmettilo in RX2
            // if (messagesForDevAddr > 1) {
            //     PendingDownlink* dl2 = &downlinkQueue[indices[1]];
            //     Serial.println("[DOWNLINK] 📤 Trasmissione messaggio 2 in RX2...");
                
            //     // Calcola tempo per RX2
            //     unsigned long elapsed = getElapsedTime(rxTimestamp);
            //     if (elapsed < RX2_DELAY) {
            //         unsigned long waitTime = RX2_DELAY - elapsed;
            //         delay(waitTime);
                    
            //         digitalWrite(LED_PIN, LOW);
            //         radio.invertIQ(true);
            //         int state = radio.transmit(dl2->data, dl2->length);
            //         radio.invertIQ(false);
            //         digitalWrite(LED_PIN, HIGH);
                    
            //         if (state == RADIOLIB_ERR_NONE) {
            //             Serial.println("[DOWNLINK] ✅ Messaggio 2 trasmesso in RX2, invio TX_ACK");
            //             sendTxAck(dl2->token);
            //             stats.tx_emitted++;
            //             justTransmitted = true;
            //         } else {
            //             Serial.printf("[DOWNLINK] ❌ Errore trasmissione messaggio 2: %d\n", state);
            //         }
            //     } else {
            //         Serial.println("[DOWNLINK] ⚠️ RX2 già passata, messaggio 2 non trasmesso");
            //     }
                
            //     // Rimuovi dalla coda
            //     for (int i = indices[1]; i < downlinkQueueCount - 1; i++) {
            //         downlinkQueue[i] = downlinkQueue[i + 1];
            //     }
            //     downlinkQueueCount--;
            // }
        } else {
            Serial.println("[DOWNLINK] ❌ Trasmissione messaggio 1 fallita, NON invio TX_ACK");
            // Rimuovi comunque dalla coda per evitare loop infiniti
            // for (int i = indices[0]; i < downlinkQueueCount - 1; i++) {
            //     downlinkQueue[i] = downlinkQueue[i + 1];
            // }
            // downlinkQueueCount--;
        }
        
        // Reset compatibilità
        // pendingDownlink.pending = false;
        return;
    }
    
    // Compatibilità con codice esistente (legacy)
    // Verifica che il DevAddr corrisponda prima di trasmettere
    // if (pendingDownlink.pending) {
    //     if (pendingDownlink.devAddr == devAddr) {
    //         Serial.println("[DOWNLINK] ✅ Downlink da ChirpStack disponibile (legacy)!");
            
    //         uint16_t savedToken = pendingDownlink.token;
    //         bool txSuccess = transmitDownlink(pendingDownlink.data, pendingDownlink.length, rxTimestamp);
            
    //         if (txSuccess) {
    //             Serial.println("[DOWNLINK] ✅ Trasmissione riuscita, invio TX_ACK a ChirpStack");
    //             sendTxAck(savedToken);
    //         } else {
    //             Serial.println("[DOWNLINK] ❌ Trasmissione fallita, NON invio TX_ACK");
    //         }
            
    //         pendingDownlink.pending = false;
    //         return;
    //     } else {
    //         Serial.printf("[DOWNLINK] ⚠️ Downlink legacy per DevAddr 0x%08X, ma uplink da 0x%08X - ignorato\n", 
    //                      pendingDownlink.devAddr, devAddr);
    //         pendingDownlink.pending = false;
    //         return;
    //     }
    // }
    
    // ===== PRIORITÀ 2: Genera downlink locale (fallback) =====
    // Serial.println("[DOWNLINK] Nessun downlink da ChirpStack, genero ACK locale");
    
    // // Verifica se il DevAddr è supportato
    // #if LORAWAN_DEVADDR != 0
    // if (devAddr != LORAWAN_DEVADDR) {
    //     Serial.printf("[DOWNLINK] DevAddr 0x%08X non supportato, skip downlink\n", devAddr);
    //     radio.startReceive();
    //     return;
    // }
    // #endif
    
    // Serial.printf("[DOWNLINK] DevAddr 0x%08X riconosciuto\n", devAddr);
    
    // // Chiavi LoRaWAN dal config.h
    // uint16_t FCntDown = 0;  // Frame counter downlink (TODO: incrementare per ogni TX)
    
    // // Costruisce pacchetto LoRaWAN downlink senza payload
    // uint8_t lorawan_frame[32];
    // uint8_t pos = 0;
    
    // // MHDR (1 byte): Downlink unconfirmed
    // lorawan_frame[pos++] = 0x60;  // MType=001 (Unconfirmed Data Down)
    
    // // DevAddr (4 bytes) - little-endian
    // lorawan_frame[pos++] = (devAddr) & 0xFF;
    // lorawan_frame[pos++] = (devAddr >> 8) & 0xFF;
    // lorawan_frame[pos++] = (devAddr >> 16) & 0xFF;
    // lorawan_frame[pos++] = (devAddr >> 24) & 0xFF;
    
    // // FCtrl (1 byte): tutti i flag a 0
    // lorawan_frame[pos++] = 0x00;
    
    // // FCnt (2 bytes): Frame counter - little-endian
    // lorawan_frame[pos++] = (FCntDown) & 0xFF;
    // lorawan_frame[pos++] = (FCntDown >> 8) & 0xFF;
    
    // // Ora calcoliamo il MIC su tutto il frame (senza MIC stesso)
    // size_t frame_length_without_mic = pos;
    
    // // Calcola MIC con AES-CMAC (LoRaWAN 1.1 downlink)
    // // Block B0 per MIC calculation (rinominato per evitare conflitto con macro Arduino)
    // uint8_t blockB0[16];
    // memset(blockB0, 0, 16);
    // blockB0[0] = 0x49;  // 0x49 per downlink
    // blockB0[1] = 0x00;  // ConfFCnt (4 bytes) - 0 per unconfirmed
    // blockB0[2] = 0x00;
    // blockB0[3] = 0x00;
    // blockB0[4] = 0x00;
    // blockB0[5] = 0x01;  // Direction: 1 = downlink
    // blockB0[6] = (devAddr) & 0xFF;
    // blockB0[7] = (devAddr >> 8) & 0xFF;
    // blockB0[8] = (devAddr >> 16) & 0xFF;
    // blockB0[9] = (devAddr >> 24) & 0xFF;
    // blockB0[10] = (FCntDown) & 0xFF;
    // blockB0[11] = (FCntDown >> 8) & 0xFF;
    // blockB0[12] = 0x00;  // FCnt upper bytes
    // blockB0[13] = 0x00;
    // blockB0[14] = 0x00;
    // blockB0[15] = frame_length_without_mic;
    
    // // Prepara buffer per CMAC: blockB0 + frame
    // uint8_t cmac_buffer[16 + 32];
    // memcpy(cmac_buffer, blockB0, 16);
    // memcpy(cmac_buffer + 16, lorawan_frame, frame_length_without_mic);
    
    // // Calcola CMAC usando la chiave da config.h
    // RadioLibAES128 aes;
    // aes.init((uint8_t*)LORAWAN_SNWKSINTKEY);
    // uint8_t fullMic[16];
    // aes.generateCMAC(cmac_buffer, 16 + frame_length_without_mic, fullMic);
    
    // // LoRaWAN usa solo i primi 4 bytes del MIC
    // lorawan_frame[pos++] = fullMic[0];
    // lorawan_frame[pos++] = fullMic[1];
    // lorawan_frame[pos++] = fullMic[2];
    // lorawan_frame[pos++] = fullMic[3];
    
    // size_t length = pos;
    
    // Serial.println("\n[DOWNLINK] ===== PREPARAZIONE ACK LOCALE =====");
    // Serial.printf("[DOWNLINK] LoRaWAN Frame: %d bytes\n", length);
    // Serial.printf("[DOWNLINK] MIC calcolato: %02X%02X%02X%02X\n", 
    //               fullMic[0], fullMic[1], fullMic[2], fullMic[3]);
    // Serial.print("[DOWNLINK] Frame (HEX): ");
    // for (size_t i = 0; i < length; i++) {
    //     Serial.printf("%02X ", lorawan_frame[i]);
    // }
    // Serial.println();
    
    // // Usa transmitDownlink per inviare con timing corretto
    // transmitDownlink(lorawan_frame, length, rxTimestamp);
}


