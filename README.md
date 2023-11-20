Jméno: Sviatoslav Shishnev
Login: xshish02
Datum: 20.11.2023
## Popis Programu

Tento program implementuje Trivial File Transfer Protocol (TFTP), což je jednoduchý protokol pro přenos souborů v počítačových sítích. Klient a server jsou navrženy tak, aby podporovaly komunikaci pomocí TFTP. Klient je schopen pracovat v octet módu a umožňuje provádět operace čtení a zápisu souborů.

### Omezení

- Tento klient momentálně podporuje pouze octet mód.
- Server nepodporuje Netascii mód a možnost Transfer Size, jak je definováno v RFC 2347.

## Příklad Spuštění

### Klient

```bash
./tftp-client -h [hostname] [-p port] [-f filepath] -t dest_filepath
```
Parametry
-h: IP adresa/doménový název vzdáleného serveru.
-p: Port vzdáleného serveru. (Volitelný, pokud není specifikován, použije se výchozí dle specifikace.)
-f: Cesta ke stahovanému souboru na serveru (download). (Volitelný, pokud není specifikován, použije se obsah stdin pro upload.)
-t: Cesta, pod kterou bude soubor na vzdáleném serveru/lokálně uložen.

### Server

```bash
./tftp-server [-p port] root_dirpath
```
Parametry
-p: Místní port, na kterém bude server očekávat příchozí spojení.
root_dirpath: Cesta k adresáři, pod kterým se budou ukládat příchozí soubory.

### Odevzdané Soubory:
server.c: zdrojový soubor popisující server
client.c: zdrojový soubor popisující klienta 
manual.pdf: Dokumentace ve formátu PDF obsahující úvod do problematiky, návrh aplikace, popis implementace, návod na použití a další informace.
README.md: Tento soubor obsahující informace o autorovi, popis programu, příklad spuštění a seznam odevzdaných souborů.