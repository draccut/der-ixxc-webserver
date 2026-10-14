# Der IXXC Webserver

Dies ist ein Lernprojekt, in dem ich herausfinden wollte, wie man einen sicheren C-Webserver für das Internet baut. Er läuft nun schon etwa seit dem 01.06.2026 produktiv im Internet, anfangs noch unter root und mit eigenem TLS 1.3 über OpenSSL. Seit er unter https://info.ixxc.de erreichbar ist, läuft er hinter einem Reverse Proxy und benötigt daher keinen privilegierten Port mehr. Er kann somit auch mit einem eingeschränkten User betrieben werden. Über Präprozessor-Einstellungen wäre er jedoch weiterhin in der Lage, alleine unter Port 443 zu laufen.

Es handelt sich um eine Art Tar-Archiv und Webserver in einem. Deshalb sind auch die vielen manuellen Build-Schritte erforderlich. `ixxc_endpoints.h` enthält eine Struct-Liste `IXXC_ENDPOINTS`. Diese besteht jeweils aus einem Pfad zu der Datei, die später exponiert wird, und dem eigentlichen Endpunkt. `generate_c_frontend_source.c` erstellt einen langen Vektor aller Dateien, der im finalen Build-Prozess des Servers im `.rodata`-Segment zusammen mit ihren Offsets abgelegt wird.

Beim Start des Servers werden zuerst alle benötigten Buffer berechnet und anschließend allokiert. Erst beim Eintreten des Interrupts, der zum Beenden und Freigeben allen Speichers führt, wird der Speicher zurückgegeben. Dies gilt für die gesamte Speicherverwaltung bis auf eine kleine Ausnahme. Die eigentliche Verarbeitung von Requests wird von 4 Workern durchgeführt, die unter dem User `ixxc` laufen. Der Main-Thread übergibt jeweils Requests an diese Worker, die gleichzeitig auf Frontend-Ressourcen zugreifen können und somit bei Volllast eine hohe Lokalität im L3-Cache erzeugen. Das Logging erfolgt über einen Ringbuffer, der von den `ixxc`-Usern beschrieben und von einem höher privilegierten Prozess in einem festen Zeitintervall abgearbeitet wird, falls der Server mit root gestartet wurde ansonst bleibt es der ixxc.

----

# How to use

### Server erstellen und verwenden

```
nix-shell
./build_and_test.sh
```

`test_endpoints.h` wird zum einen für zeitkonstante Vergleichstests verwendet und dient gleichzeitig als Vorlage für die eigene `endpoint_list.h`, die am Anfang von `ixxc_endpoints.h` gesetzt werden kann.

### Endpunkte und Archiv des Webservers testen

```
nix-shell -p gcc curl.dev
gcc -O2 -Wall -Wextra test_ixxc.c -o test_ixxc -lcurl -lpthread
```

Das Testprogramm liest jede in `endpoint_list.h` vorhandene Datei ein und speichert sie im Heap. Anschließend werden die Remote-Dateien über curl abgerufen. Dabei werden der OpenSSL-Layer, die Netzwerk-Response Header sowie die korrekte Pointer-Arithmetik innerhalb des Archivs überprüft. Wenn alle Tests mit [OK] abschließen, werden alle Endpunkte korrekt ausgeliefert.
