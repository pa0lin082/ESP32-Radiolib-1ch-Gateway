# Libreria Comune (lib_common)

Questa directory contiene codice condiviso tra i vari progetti e varianti.

## File condivisi

- `power.h` - Gestione batteria e power management

## Come usare

### Nel progetto principale (gateway)

Il file `platformio.ini` include già `-I lib_common` nei build_flags, quindi puoi includere i file così:

```cpp
#include "power.h"
```

### Nel progetto test_node

Il file `test_node/platformio.ini` include `-I ../lib_common`, quindi puoi includere i file così:

```cpp
#include "power.h"
```

## Aggiungere nuovi file condivisi

1. Copia il file in questa directory (`lib_common/`)
2. Assicurati che tutti i `platformio.ini` includano `-I lib_common` (o `-I ../lib_common` per progetti in sottodirectory)
3. Usa `#include "nomefile.h"` nei tuoi sorgenti

