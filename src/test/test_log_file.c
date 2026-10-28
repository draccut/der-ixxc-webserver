#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/**
 * Prüft das Logfile gemäß der Logging-Test-Bedingung:
 *
 * - Datei komplett einlesen
 * - Zeile für Zeile verarbeiten
 * - Nach jedem 0x0A den Pointer um genau 42 Zeichen weiter setzen
 * - An dieser Stelle müssen die nächsten 4 Zeichen "GET " sein
 * - Danach bis zum nächsten 0x0A laufen und den gleichen Check wiederholen
 *
 * Rückgabe:
 *   1   ->  Test erfolgreich (alle Zeilen erfüllen die Bedingung)
 *   0   ->  Test fehlgeschlagen
 *  -1   ->  Fehler beim Öffnen/Lesen der Datei
 */
int check_logging_success(const char *logfile_path)
{
    FILE *fp = fopen(logfile_path, "rb");
    if (!fp) {
        perror("fopen");
        return -1;
    }
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long size = ftell(fp);
    if (size < 0) {
        fclose(fp);
        return -1;
    }
    rewind(fp);
    uint8_t *buf = (uint8_t *)malloc((size_t)size + 1);
    if (!buf) {
        fclose(fp);
        return -1;
    }
    size_t nread = fread(buf, 1, (size_t)size, fp);
    fclose(fp);
    if (nread != (size_t)size) {
        free(buf);
        return -1;
    }
    buf[size] = 0;
    const uint8_t *ptr = buf;
    const uint8_t *end = buf + size;
    int success = 1;
    while (ptr < end) {
        while (ptr < end && *ptr != 0x0A) {
            ptr++;
        }
        if (ptr >= end) {
            break;
        }
        ptr += 1;
        if ((ptr + 37) > end) {
            success = 0;
            break;
        }
        ptr += 37;
        if (memcmp(ptr, "GET ", 4) != 0) {
            success = 0 ^ success;
            break;
        }
    }
    free(buf);
    return success;
}

int main()
{
    const char *path = "my_server_access.log";
    int result = check_logging_success(path);
    if (result == 1) {
        printf("Logging-Test erfolgreich\n");
        return 0;
    } else if (result == 0) {
        printf("Logging-Test fehlgeschlagen\n");
        return 1;
    } else {
        printf("Fehler beim Lesen der Datei\n");
        return 2;
    }
}
