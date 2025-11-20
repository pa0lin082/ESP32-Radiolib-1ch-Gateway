// ┌─────────────────────────────────────────────────────────────┐
// │                    PACCHETTO UDP SEMTECH                     │
// ├─────────────────────────────────────────────────────────────┤
// │ Offset │ Dimensione │ Campo          │ Descrizione          │
// ├────────┼────────────┼────────────────┼──────────────────────┤
// │   0    │    1 byte  │ version        │ Versione protocollo  │
// │        │            │                │ (sempre 0x02)        │
// ├────────┼────────────┼────────────────┼──────────────────────┤
// │   1    │    1 byte  │ token[15:8]    │ Token MSB (random)   │
// ├────────┼────────────┼────────────────┼──────────────────────┤
// │   2    │    1 byte  │ token[7:0]     │ Token LSB (random)   │
// ├────────┼────────────┼────────────────┼──────────────────────┤
// │   3    │    1 byte  │ identifier     │ Tipo messaggio:      │
// │        │            │                │ 0x00 = PUSH_DATA     │
// │        │            │                │ 0x01 = PUSH_ACK      │
// │        │            │                │ 0x02 = PULL_DATA     │
// │        │            │                │ 0x03 = PULL_RESP ⚡  │
// │        │            │                │ 0x04 = PULL_ACK      │
// │        │            │                │ 0x05 = TX_ACK        │
// ├────────┼────────────┼────────────────┼──────────────────────┤
// │   4    │    8 bytes │ gateway_id     │ SOLO per PUSH_DATA  │
// │        │            │ (opzionale)    │ e PULL_DATA          │
// │        │            │                │ (big-endian, 64-bit) │
// ├────────┼────────────┼────────────────┼──────────────────────┤
// │  12    │   variabile│ JSON payload   │ Stringa JSON UTF-8   │
// │        │            │                │ (null-terminated)    │
// └────────┴────────────┴────────────────┴──────────────────────┘
#include <ArduinoJson.h>
// ===========================
// ENUM PER TIPI MESSAGGIO SEMTECH UDP
// ===========================
enum class SemtechMessageType : uint8_t {
    PUSH_DATA  = 0x00,
    PUSH_ACK   = 0x01,
    PULL_DATA  = 0x02,
    PULL_RESP  = 0x03,
    PULL_ACK   = 0x04,
    TX_ACK     = 0x05,
    UNKNOWN    = 0xFF
};

// Funzione helper per convertire enum a stringa
inline const char* semtechMessageTypeToString(SemtechMessageType type) {
    switch (type) {
        case SemtechMessageType::PUSH_DATA:  return "PUSH_DATA";
        case SemtechMessageType::PUSH_ACK:   return "PUSH_ACK";
        case SemtechMessageType::PULL_DATA:  return "PULL_DATA";
        case SemtechMessageType::PULL_RESP:  return "PULL_RESP";
        case SemtechMessageType::PULL_ACK:   return "PULL_ACK";
        case SemtechMessageType::TX_ACK:     return "TX_ACK";
        default:                             return "UNKNOWN";
    }
}

// ===========================
// STRUCT PER HEADER UDP SEMTECH (solo per parsing)
// ===========================
#pragma pack(push, 1)  // Allineamento byte-per-byte (no padding)
struct SemtechUdpHeader {
    uint8_t version;      // Byte 0: Versione protocollo (0x02)
    uint8_t tokenH;       // Byte 1: Token MSB
    uint8_t tokenL;       // Byte 2: Token LSB
    uint8_t identifier;   // Byte 3: Tipo messaggio
    
    uint16_t getToken() const {
        return ((uint16_t)tokenH << 8) | tokenL;
    }
    
    SemtechMessageType getMessageType() const {
        if (identifier <= 0x05) {
            return static_cast<SemtechMessageType>(identifier);
        }
        return SemtechMessageType::UNKNOWN;
    }
    
    bool isValid() const {
        return version == 0x02 && identifier <= 0x05;
    }
};
#pragma pack(pop)

// ===========================
// STRUCT PER HEADER LORAWAN (dopo decodifica base64)
// ===========================
#pragma pack(push, 1)
struct LoRaWANHeader {
    uint8_t mhdr;           // MAC Header
    uint32_t devAddr;       // Device Address (little-endian)
    uint8_t fctrl;          // Frame Control
    uint16_t fcnt;          // Frame Counter (little-endian)
    
    uint8_t getMType() const {
        return (mhdr >> 5) & 0x07;
    }
    
    uint8_t getFOptsLen() const {
        return fctrl & 0x0F;
    }
    
    bool getACK() const {
        return (fctrl >> 5) & 0x01;
    }
    
    bool getFPending() const {
        return (fctrl >> 4) & 0x01;
    }
};
#pragma pack(pop)

// ===========================
// STRUCT PER CAMPI TXPK (downlink)
// ===========================
struct TxPkData {
    // Campi obbligatori
    char data[512];           // Base64 payload (max ~384 bytes payload LoRaWAN)
    
    // Campi opzionali con valori di default
    bool imme = false;        // Immediate transmission (Classe C)
    uint32_t tmst = 0;       // Timestamp microsecondi
    float freq = 0.0;        // Frequenza MHz
    uint8_t rfch = 0;        // RF chain
    uint8_t powe = 14;       // Potenza dBm
    char modu[8] = "LORA";   // Modulazione
    char datr[16] = "";      // Data rate (es: "SF7BW125")
    char codr[8] = "4/5";    // Coding rate
    uint16_t fdev = 0;       // Frequency deviation
    bool ipol = true;        // Invert polarity
    uint16_t prea = 8;       // Preamble length
    uint16_t size = 0;       // Dimensione payload
    
    // Flag per indicare quali campi sono presenti
    bool has_imme = false;
    bool has_tmst = false;
    bool has_freq = false;
    bool has_powe = false;
    bool has_modu = false;
    bool has_datr = false;
    bool has_codr = false;
    bool has_size = false;
    
    // Metodo per parsare da JSON
    bool parseFromJson(JsonObject &txpk) {

        Serial.println("[TxPkData] parseFromJson:");
        serializeJsonPretty(txpk, Serial);
        Serial.println();
        
        // Campo obbligatorio
        if (!txpk.containsKey("data")) {
            return false;
        }
        strncpy(data, txpk["data"] | "", sizeof(data) - 1);
        data[sizeof(data) - 1] = '\0';
        
        // Campi opzionali
        if (txpk.containsKey("imme")) {
            imme = txpk["imme"] | false;
            has_imme = true;
        }
        
        if (txpk.containsKey("tmst")) {
            tmst = txpk["tmst"] | 0;
            has_tmst = true;
        }
        
        if (txpk.containsKey("freq")) {
            freq = txpk["freq"] | 0.0;
            has_freq = true;
        }
        
        if (txpk.containsKey("powe")) {
            powe = txpk["powe"] | 14;
            has_powe = true;
        }
        
        if (txpk.containsKey("modu")) {
            strncpy(modu, txpk["modu"] | "LORA", sizeof(modu) - 1);
            has_modu = true;
        }
        
        if (txpk.containsKey("datr")) {
            strncpy(datr, txpk["datr"] | "", sizeof(datr) - 1);
            has_datr = true;
        }
        
        if (txpk.containsKey("codr")) {
            strncpy(codr, txpk["codr"] | "4/5", sizeof(codr) - 1);
            has_codr = true;
        }
        
        if (txpk.containsKey("size")) {
            size = txpk["size"] | 0;
            has_size = true;
        }
        
        return true;
    }
};

// ===========================
// STRUCT PER DATI PULL RESPONSE (livello alto)
// ===========================
struct PullResponseData {
    TxPkData txpk;                    // Dati JSON txpk
    LoRaWANHeader lorawanHeader;     // Header LoRaWAN estratto
    uint8_t decodedPayload[256];     // Payload decodificato (base64 → binario)
    size_t decodedLength = 0;         // Lunghezza payload decodificato
    uint8_t fport = 0;                // FPort estratto
    bool isMacCommand = false;        // true se FPort == 0
    uint32_t devAddr = 0;             // DevAddr estratto (per comodità)
    
    bool isValid() const {
        return decodedLength > 0 && strlen(txpk.data) > 0;
    }
    
    void printDebug() const {
        Serial.printf("[PullResponseData] Classe C: %s\n", txpk.imme ? "SI" : "NO");
        Serial.printf("[PullResponseData] DevAddr: 0x%08X\n", devAddr);
        Serial.printf("[PullResponseData] FPort: %d\n", fport);
        Serial.printf("[PullResponseData] MAC Command: %s\n", isMacCommand ? "SI" : "NO");
        Serial.printf("[PullResponseData] Payload size: %zu bytes\n", decodedLength);
    }
};

// ===========================
// CLASSE PRINCIPALE: SEMTECH UDP PACKAGE (livello basso)
// ===========================
class SemtechUdpPackage {
private:
    SemtechUdpHeader header;
    uint64_t gatewayId = 0;           // Gateway ID (solo per PUSH_DATA/PULL_DATA)
    bool hasGatewayId = false;        // Flag per indicare se gatewayId è presente
    const uint8_t* jsonPayload = nullptr;  // Puntatore al payload JSON
    size_t jsonPayloadLength = 0;     // Lunghezza payload JSON
    size_t bufferLength = 0;          // Lunghezza totale buffer UDP
    
    // Estrae gateway ID dal buffer (solo per PUSH_DATA e PULL_DATA)
    void extractGatewayId(const uint8_t* buffer) {
        if (bufferLength >= 12 && 
            (header.getMessageType() == SemtechMessageType::PUSH_DATA ||
             header.getMessageType() == SemtechMessageType::PULL_DATA)) {
            gatewayId = ((uint64_t)buffer[4] << 56) |
                       ((uint64_t)buffer[5] << 48) |
                       ((uint64_t)buffer[6] << 40) |
                       ((uint64_t)buffer[7] << 32) |
                       ((uint64_t)buffer[8] << 24) |
                       ((uint64_t)buffer[9] << 16) |
                       ((uint64_t)buffer[10] << 8) |
                       ((uint64_t)buffer[11]);
            hasGatewayId = true;
        }
    }
    
public:
    // Costruttore: inizializza dal buffer UDP
    // Ritorna true se inizializzazione riuscita, false altrimenti
    bool initFromBuffer(const uint8_t* buffer, size_t length) {
        // Reset stato
        gatewayId = 0;
        hasGatewayId = false;
        jsonPayload = nullptr;
        jsonPayloadLength = 0;
        bufferLength = 0;
        
        // Controllo dimensione minima (almeno header)
        if (length < 4) {
            return false;
        }
        
        // Copia header
        memcpy(&header, buffer, sizeof(SemtechUdpHeader));
        
        // Verifica validità header
        if (!header.isValid()) {
            return false;
        }

        bufferLength = length;
        // Serial.printf("[SemtechUdpPackage] Buffer length: %zu\n", bufferLength);
       
        
        // Estrae gateway ID se presente
        extractGatewayId(buffer);
        
        // Estrae payload JSON
        size_t jsonStartOffset = hasGatewayId ? 12 : 4;
        if (length > jsonStartOffset) {
            Serial.printf("[SemtechUdpPackage] JSON start at offset: %zu\n", jsonStartOffset);
            jsonPayload = &buffer[jsonStartOffset];
            jsonPayloadLength = bufferLength - jsonStartOffset;
        }
        
        return true;
    }
    
    // Ritorna il tipo di messaggio
    SemtechMessageType getMessageType() const {
        return header.getMessageType();
    }
    
    // Ritorna il nome del tipo di messaggio come stringa
    const char* getMessageTypeString() const {
        return semtechMessageTypeToString(getMessageType());
    }
    
    // Ritorna il token
    uint16_t getToken() const {
        return header.getToken();
    }
    
    // Ritorna il gateway ID (solo per PUSH_DATA/PULL_DATA)
    // Ritorna 0 se non presente
    uint64_t getGatewayId() const {
        return hasGatewayId ? gatewayId : 0;
    }
    
    // Verifica se il gateway ID è presente
    bool hasGatewayIdField() const {
        return hasGatewayId;
    }
    
    // Verifica validità del pacchetto
    bool isValid() const {
        return header.isValid();
    }
    
    // Ritorna il payload JSON (se presente)
    const uint8_t* getJsonPayload() const {
        return jsonPayload;
    }
    
    // Ritorna la lunghezza del payload JSON
    size_t getJsonPayloadLength() const {
        return jsonPayloadLength;
    }
    
    // Estrae i dati PULL_RESP (solo se è un PULL_RESP)
    // Ritorna true se estrazione riuscita, false altrimenti
    bool getPullResponse(PullResponseData& result) const {
        // Verifica che sia un PULL_RESP
        if (getMessageType() != SemtechMessageType::PULL_RESP) {
            return false;
        }
        
        // Verifica che ci sia payload JSON
        if (jsonPayload == nullptr || jsonPayloadLength == 0) {
            return false;
        }
        
        // Parse JSON
        StaticJsonDocument<512> doc;
        DeserializationError error = deserializeJson(
            doc, 
            (const char*)jsonPayload, 
            jsonPayloadLength
        );

        if (error) {
            Serial.printf("[SemtechUdpPackage] ❌ Errore parsing JSON: %s\n", error.c_str());
            Serial.print("[SemtechUdpPackage] JSON raw: ");
            for (int i = 0; i < jsonPayloadLength && i < 100; i++) {
                Serial.printf("%c", jsonPayload[i]);
            }
            Serial.println();
            return false;
        }
        Serial.printf("[SemtechUdpPackage] jsonPayloadLength: %zu\n", jsonPayloadLength);
        
        // Verifica presenza campo txpk
        if (!doc.containsKey("txpk")) {
            Serial.printf("[SemtechUdpPackage] ❌ JSON: txpk non trovato\n");
            return false;
        }
        
        // Parse oggetto txpk
        JsonObject txpkObj = doc["txpk"];

        if (!result.txpk.parseFromJson(txpkObj)) {
            Serial.println("[SemtechUdpPackage] ❌ Errore parsing JSON: txpkObj");
            return false;
        }



        const char* base64Data = result.txpk.data;
        Serial.printf("[SemtechUdpPackage] Downlink data (base64): %s\n", base64Data);
        
        // Decodifica base64 payload
        result.decodedLength = decodeBase64(
            base64Data, 
            result.decodedPayload, 
            sizeof(result.decodedPayload)
        );
        Serial.printf("[SemtechUdpPackage] Decoded length: %zu\n",
                      result.decodedLength);
        Serial.printf("[SemtechUdpPackage] Decoded payload: ");
        for (size_t i = 0; i < result.decodedLength; i++) {
            Serial.printf("%02X ", result.decodedPayload[i]);
        }
        Serial.println();
        if (result.decodedLength == 0 || result.decodedLength < 8) {
            return false;
        }

        
        // Parse header LoRaWAN
        memcpy(&result.lorawanHeader, result.decodedPayload, sizeof(LoRaWANHeader));
        result.devAddr = result.lorawanHeader.devAddr;
        
        // Estrai FPort e determina se è MAC command
        uint8_t foptsLen = result.lorawanHeader.getFOptsLen();
        size_t fportPos = 8 + foptsLen;
        
        if (fportPos < result.decodedLength - 4) {  // -4 per MIC
            result.fport = result.decodedPayload[fportPos];
            result.isMacCommand = (result.fport == 0);
        }
        
        return true;
    }
    
    // Metodo per debug
    void printDebug() const {
        Serial.printf("[SemtechUdpPackage] Tipo: %s\n", getMessageTypeString());
        Serial.printf("[SemtechUdpPackage] Token: 0x%04X\n", getToken());
        if (hasGatewayId) {
            Serial.printf("[SemtechUdpPackage] Gateway ID: 0x%016llX\n", gatewayId);
        }
        Serial.printf("[SemtechUdpPackage] JSON payload: %zu bytes\n", jsonPayloadLength);
    }
};

// ===========================
// STRUCT COMPLETA PER PULL_RESP (compatibilità con codice esistente)
// ===========================
struct PullRespPacket {
    uint16_t token;
    PullResponseData responseData;

    bool isValid() const {
        return responseData.isValid();
    }

};

// ===========================
// CLASSE PER GESTIONE CODA DOWNLINK
// ===========================
#ifndef MAX_DOWNLINK_QUEUE_SIZE
#define MAX_DOWNLINK_QUEUE_SIZE 10  // Massimo 10 messaggi totali in coda
#endif

#ifndef MAX_DOWNLINK_PER_DEVADDR
#define MAX_DOWNLINK_PER_DEVADDR 2  // Massimo 2 messaggi per DevAddr (RX1 + RX2)
#endif

class DownlinkQueue {
private:
    PullRespPacket queue[MAX_DOWNLINK_QUEUE_SIZE];
    uint8_t count = 0;  // Numero elementi attualmente nella coda
    
    // Trova il primo slot vuoto (ritorna MAX_DOWNLINK_QUEUE_SIZE se pieno)
    uint8_t findEmptySlot() const {
        for (uint8_t i = 0; i < MAX_DOWNLINK_QUEUE_SIZE; i++) {
            if (!queue[i].isValid()) {
                return i;
            }
        }
        return MAX_DOWNLINK_QUEUE_SIZE;  // Coda piena
    }
    
public:
    // Costruttore: inizializza la coda
    DownlinkQueue() {
        clear();
    }
    
    // Aggiunge un elemento alla coda (copia)
    // Ritorna true se aggiunto con successo, false se:
    //   - Coda piena (MAX_DOWNLINK_QUEUE_SIZE raggiunto)
    //   - Limite per DevAddr raggiunto (MAX_DOWNLINK_PER_DEVADDR)
    bool add(const PullRespPacket& packet) {
        // Controllo 1: Coda totale piena?
        if (count >= MAX_DOWNLINK_QUEUE_SIZE) {
            return false;  // Coda piena
        }
        
        // Controllo 2: Limite per DevAddr raggiunto?
        uint8_t countForDevAddr = countByDevAddr(packet.responseData.devAddr);
        if (countForDevAddr >= MAX_DOWNLINK_PER_DEVADDR) {
            return false;  // Limite per DevAddr raggiunto
        }
        
        // Trova slot vuoto
        uint8_t slot = findEmptySlot();
        if (slot >= MAX_DOWNLINK_QUEUE_SIZE) {
            return false;  // Nessuno slot vuoto trovato (non dovrebbe mai succedere qui)
        }
        
        // Copia il pacchetto nello slot
        queue[slot] = packet;
        count++;
        return true;
    }
    
    // Ritorna il numero di elementi nella coda
    uint8_t size() const {
        return count;
    }
    
    // Ritorna true se la coda è vuota
    bool isEmpty() const {
        return count == 0;
    }
    
    // Ritorna true se la coda è piena
    bool isFull() const {
        return count >= MAX_DOWNLINK_QUEUE_SIZE;
    }
    
    // Verifica se si può aggiungere un elemento per un DevAddr specifico
    bool canAddForDevAddr(uint32_t devAddr) const {
        if (count >= MAX_DOWNLINK_QUEUE_SIZE) {
            return false;
        }
        uint8_t countForDev = countByDevAddr(devAddr);
        return countForDev < MAX_DOWNLINK_PER_DEVADDR;
    }
    
    // Ritorna il numero di slot disponibili per un DevAddr specifico
    uint8_t availableSlotsForDevAddr(uint32_t devAddr) const {
        uint8_t countForDev = countByDevAddr(devAddr);
        if (countForDev >= MAX_DOWNLINK_PER_DEVADDR) {
            return 0;
        }
        return MAX_DOWNLINK_PER_DEVADDR - countForDev;
    }
    
    // Trova il primo elemento con un specifico DevAddr
    PullRespPacket* findFirstByDevAddr(uint32_t devAddr) {
        for (uint8_t i = 0; i < MAX_DOWNLINK_QUEUE_SIZE; i++) {
            if (queue[i].isValid() && queue[i].responseData.devAddr == devAddr) {
                return &queue[i];
            }
        }
        return nullptr;
    }
    
    // Trova il primo elemento con immediate=true (Classe C)
    PullRespPacket* findFirstImmediate() {
        for (uint8_t i = 0; i < MAX_DOWNLINK_QUEUE_SIZE; i++) {
            if (queue[i].isValid() && queue[i].responseData.txpk.imme) {
                return &queue[i];
            }
        }
        return nullptr;
    }
    
    // Conta quanti elementi ci sono per un specifico DevAddr
    uint8_t countByDevAddr(uint32_t devAddr) const {
        uint8_t cnt = 0;
        for (uint8_t i = 0; i < MAX_DOWNLINK_QUEUE_SIZE; i++) {
            if (queue[i].isValid() && queue[i].responseData.devAddr == devAddr) {
                cnt++;
            }
        }
        return cnt;
    }

        // Elimina un elemento dalla coda dato il suo indice
    // Ritorna true se eliminato con successo
    bool removeAt(uint8_t index) {
        // Verifica che l'indice sia valido
        if (index >= MAX_DOWNLINK_QUEUE_SIZE) {
            return false;
        }
        
        // Verifica che l'elemento sia valido (non vuoto)
        if (!queue[index].isValid()) {
            return false;
        }
        
        // Invalida l'elemento (resetta a zero)
        memset(&queue[index], 0, sizeof(PullRespPacket));
        
        // Decrementa il contatore
        count--;
        
        return true;
    }

    // Elimina un elemento dalla coda dato il puntatore
    bool remove(PullRespPacket* packet) {
        if (packet == nullptr) return false;
        uint8_t index = (packet - queue);
        if (index >= MAX_DOWNLINK_QUEUE_SIZE) {
            return false;
        }
        return removeAt(index);
    }


  

    // Accede a un elemento per indice (senza validazione)
    PullRespPacket& operator[](uint8_t index) {
        return queue[index];
    }
    
    const PullRespPacket& operator[](uint8_t index) const {
        return queue[index];
    }
    
    
    // Svuota completamente la coda
    void clear() {
        memset(queue, 0, sizeof(queue));
        count = 0;
    }
    

    
    // Stampa informazioni di debug sulla coda
    void printDebug() const {
        Serial.printf("[QUEUE] Elementi nella coda: %d/%d\n", count, MAX_DOWNLINK_QUEUE_SIZE);
        Serial.printf("[QUEUE] Limite per DevAddr: %d messaggi\n", MAX_DOWNLINK_PER_DEVADDR);
        
        // Raggruppa per DevAddr
        uint32_t seenDevAddrs[10] = {0};
        uint8_t seenCount = 0;
        
        for (uint8_t i = 0; i < MAX_DOWNLINK_QUEUE_SIZE; i++) {
            if (queue[i].isValid()) {
                bool alreadySeen = false;
                for (uint8_t j = 0; j < seenCount; j++) {
                    if (seenDevAddrs[j] == queue[i].responseData.devAddr) {
                        alreadySeen = true;
                        break;
                    }
                }
                
                if (!alreadySeen) {
                    seenDevAddrs[seenCount++] = queue[i].responseData.devAddr;
                }
            }
        }
        
        // Stampa per ogni DevAddr
        for (uint8_t i = 0; i < seenCount; i++) {
            uint32_t devAddr = seenDevAddrs[i];
            uint8_t countForDev = countByDevAddr(devAddr);
            Serial.printf("[QUEUE] DevAddr 0x%08X: %d/%d messaggi\n", 
                         devAddr, countForDev, MAX_DOWNLINK_PER_DEVADDR);
            
            // Stampa dettagli slot
            for (uint8_t j = 0; j < MAX_DOWNLINK_QUEUE_SIZE; j++) {
                if (queue[j].isValid() && queue[j].responseData.devAddr == devAddr) {
                    Serial.printf("[QUEUE]   Slot %d: ClasseC=%s, FPort=%d, Token=0x%04X\n",
                                 j, queue[j].responseData.txpk.imme ? "SI" : "NO", 
                                 queue[j].responseData.fport, queue[j].token);
                }
            }
        }
    }
};
