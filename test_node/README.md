# Test LoRaWAN - Scenario 1: Nodo ABP Uplink-Only

Questa cartella contiene tutto il necessario per testare il gateway con un nodo ABP.

## 📁 File nella Cartella

| File | Descrizione |
|------|-------------|
| `test_node_abp.cpp` | Codice del nodo di test ABP |
| `platformio.ini` | Configurazione PlatformIO per il nodo |
| `quick_test.md` | **START HERE!** Guida rapida 5 minuti |
| `ABP_KEYS_EXPLAINED.md` | **IMPORTANTE!** Spiega come funzionano le chiavi ABP |
| `README_TEST.md` | Guida completa con test avanzati |
| `generate_keys.py` | Script per generare chiavi casuali |
| `monitor.sh` | Script per monitorare gateway e nodo insieme |
| `chirpstack_device_config.json` | Esempio configurazione ChirpStack |

## 🚀 Quick Start (5 minuti)

### Passo 1: Leggi prima questo! 🔑
**IMPORTANTE**: Leggi `ABP_KEYS_EXPLAINED.md` per capire come funzionano le chiavi ABP!

**TL;DR**: In ABP, le chiavi devono essere **identiche** nel nodo e in ChirpStack. Non vengono generate dal server!

### Passo 2: Setup rapido
Segui `quick_test.md` per:
1. Configurare ChirpStack (2 min)
2. Caricare il nodo (2 min)
3. Verificare che funzioni (1 min)

### Passo 3: Test avanzati
Leggi `README_TEST.md` per:
- Test di distanza
- Variazione SF
- Payload più grandi
- Troubleshooting

## 🔑 Gestione Chiavi - Punti Chiave

### ❌ Errore Comune
"Prendo le chiavi da ChirpStack e le metto nel nodo"
→ **SBAGLIATO!** ChirpStack NON genera le chiavi in ABP!

### ✅ Procedura Corretta

**Opzione A - Chiavi di Test (per iniziare):**
1. Le chiavi sono già in `test_node_abp.cpp`
2. Copia le **stesse** chiavi in ChirpStack quando crei il device
3. Testa!

**Opzione B - Chiavi Casuali (produzione):**
1. Genera chiavi: `python3 generate_keys.py`
2. Copia nel **nodo** (formato C++)
3. Copia in **ChirpStack** (formato esadecimale)
4. Verifica che siano identiche!

## 🎯 Cosa Fa Questo Test

Il nodo ABP:
- ✅ Invia pacchetti LoRaWAN ogni 30 secondi
- ✅ Usa **Unconfirmed Data Up** (nessun ACK richiesto)
- ✅ Incrementa il Frame Counter ad ogni invio
- ✅ Mostra statistiche su seriale

Il gateway:
- ✅ Riceve i pacchetti LoRa
- ✅ Mostra RSSI/SNR
- ✅ Inoltra a ChirpStack via UDP
- ✅ Aggiorna statistiche su OLED

ChirpStack:
- ✅ Riceve via protocollo UDP Semtech
- ✅ Decodifica frame LoRaWAN
- ✅ Verifica MIC (Message Integrity Code)
- ✅ Mostra payload in Web UI

## 🛠️ Comandi Utili

### Generare nuove chiavi
```bash
cd /Users/elfo/DEVELOPMENT/LORAWAN/ESP-1ch-Gateway-v2/test
python3 generate_keys.py
```

### Compilare e caricare il nodo
```bash
cd /Users/elfo/DEVELOPMENT/LORAWAN/ESP-1ch-Gateway-v2/test
pio run --target upload
pio device monitor
```

### Monitorare gateway e nodo insieme
```bash
cd /Users/elfo/DEVELOPMENT/LORAWAN/ESP-1ch-Gateway-v2/test
./monitor.sh
```

## 📊 Output Atteso

### Nodo (ogni 30s)
```
[TX] ===== INVIO PACCHETTO =====
[TX] Frame Counter: 5
[TX] Payload: Node 5, Up: 150 s
[TX] Pacchetto inviato con successo!
```

### Gateway
```
[RX] ===== PACKET RECEIVED =====
[RX] RSSI: -45.50 dBm
[RX] SNR: 9.75 dB
[UDP] Packet sent successfully
```

### ChirpStack
```
Uplink frames ricevuti con FCnt crescente (0, 1, 2, 3...)
Payload decodificato visibile
```

## 🐛 Troubleshooting Rapido

| Problema | Soluzione |
|----------|-----------|
| Nodo invia ma gateway non riceve | Verifica frequenza (868.1 MHz) e SF (7) identici |
| Gateway riceve ma ChirpStack no | Verifica DevAddr e chiavi identiche |
| ChirpStack riceve ma non decodifica | Verifica chiavi, abilita "Skip FCnt check" |
| RSSI molto basso (< -100 dBm) | Avvicina dispositivi, controlla antenne |

## 📚 Documentazione

- `quick_test.md` - Inizio rapido
- `ABP_KEYS_EXPLAINED.md` - **LEGGI QUESTO!** Spiega chiavi ABP
- `README_TEST.md` - Guida completa
- [ChirpStack Docs](https://www.chirpstack.io/docs/)
- [LoRaWAN Spec](https://lora-alliance.org/resource_hub/lorawan-specification-v1-0-3/)

## ⚠️ Note Importanti

1. **Chiavi di Test**: Le chiavi di default sono pubbliche. Per uso reale genera chiavi casuali!
2. **Single Channel**: Questo è un gateway single-channel. Non conforme LoRaWAN completo.
3. **ABP vs OTAA**: ABP è più semplice ma meno sicuro. In produzione usa OTAA.
4. **Frame Counter**: "Skip FCnt check" è comodo per test ma non sicuro in produzione.

## 🎓 Prossimi Passi

Dopo aver verificato questo scenario:
- [ ] Test con distanza maggiore
- [ ] Test con SF diversi (7-12)
- [ ] Implementare downlink
- [ ] Testare OTAA
- [ ] Multi-nodi

Buon test! 🚀

