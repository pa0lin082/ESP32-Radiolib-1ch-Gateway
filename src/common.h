#include <Arduino.h>
#include <ArduinoJson.h>
#include <time.h>
#include <WiFi.h>

// ===========================
// BASE64 ENCODING/DECODING
// ===========================
String encodeBase64(uint8_t* data, size_t length) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    String result;
    
    for (size_t i = 0; i < length; i += 3) {
        uint32_t b = (data[i] << 16) | ((i + 1 < length ? data[i + 1] : 0) << 8) | (i + 2 < length ? data[i + 2] : 0);
        
        result += base64_chars[(b >> 18) & 0x3F];
        result += base64_chars[(b >> 12) & 0x3F];
        result += (i + 1 < length) ? base64_chars[(b >> 6) & 0x3F] : '=';
        result += (i + 2 < length) ? base64_chars[b & 0x3F] : '=';
    }
    
    return result;
}

size_t decodeBase64(const char* base64Str, uint8_t* output, size_t maxLen) {
    const char base64_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t inLen = strlen(base64Str);
    size_t outLen = 0;
    
    uint32_t val = 0;
    int valb = -8;
    
    for (size_t i = 0; i < inLen; i++) {
        char c = base64Str[i];
        if (c == '=') break;
        
        const char* pos = strchr(base64_chars, c);
        if (pos == nullptr) continue;
        
        val = (val << 6) | (pos - base64_chars);
        valb += 6;
        
        if (valb >= 0) {
            if (outLen >= maxLen) break;
            output[outLen++] = (val >> valb) & 0xFF;
            valb -= 8;
        }
    }
    
    return outLen;
}

// ===========================
// GATEWAY ID GENERATION
// ===========================
void generateGatewayId(uint64_t *gatewayId) {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    
    // Gateway ID format: MAC with 0xFFFF inserted in middle
    // Example: AA:BB:CC:DD:EE:FF -> AABBCCFFFFDDEEFF
    *gatewayId = ((uint64_t)mac[0] << 56) |
                ((uint64_t)mac[1] << 48) |
                ((uint64_t)mac[2] << 40) |
                ((uint64_t)0xFF << 32) |
                ((uint64_t)0xFF << 24) |
                ((uint64_t)mac[3] << 16) |
                ((uint64_t)mac[4] << 8) |
                ((uint64_t)mac[5]);
    
    Serial.print("[GATEWAY] ID: ");
    Serial.printf("%016llX\n", *gatewayId);
}
