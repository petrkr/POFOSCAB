# Portfolio ROM - File transfer Server: dispatch a TSR rozšíření

Zdrojový kód, na který se tento dokument odkazuje (`*.inc`/`.asm`
soubory zmíněné níže, např. `pofo-driver/hello.inc`), žije v tomto
repu (POFOSCAB) ve `src/pftd/` - historické zmínky `pofo-driver/`
prefixu odkazují na to, jak byly tyto soubory organizované před
přesunem do tohoto samostatného repa, ne na aktuální umístění.

## Cíl
Najít, kde Server mód (System Setup -> File transfer -> Server) rozhoduje
o funkčním kódu přijatého bloku (payload[0]: 2=receive file, 3=transmit
init, 5=transmit overwrite, 6=list), zjistit jestli tam je skrytý další
kód, a pokud ne, navrhnout bezpečný způsob, jak přidat nové příkazy
(mkdir, delete, free space, list-v2 se složkami/velikostmi) BEZ zásahu
do ROM (read-only) a BEZ duplikace stávající receive/send logiky.

## Výsledek 1: dispatch v ROM nemá skrytý příkaz

Dispatch je na fyzické adrese `0xD8190` (ROM B, C0000-DFFFF, segment
`D7D9`, offset `0x0400`):

```asm
mov ax,0x603D / call 0x7E5D        ; prompt/setup
push 0x5A (90) / lea ax,[bp-0x94] / call D7D9:0x6B2  ; přijmi blok (90 bajtů)
mov al,[bp-0x94]                    ; al = payload[0]
mov ah,0 / dec ax / dec ax          ; al = payload[0] - 2
cmp ax,4 / ja 0x8199                ; mimo [2,6] -> odmítnuto
mov bx,ax / shl bx,1
jmp word near [cs:bx+0x438]         ; jump table, 5 položek
```

Jump tabulka na `0xD81C8`:

| payload[0] | cíl | stav |
|---|---|---|
| 2 | `0xD81D2` | implementováno (receive file) |
| 3 | `0xD827A` | implementováno (transmit init) |
| **4** | `0xD8394` | **NO-OP** - jen epilog, nic nedělá |
| 5 | `0xD8199` | implementováno (transmit overwrite) |
| 6 | `0xD831E` | implementováno (receive/list) |

**Neznámý/neplatný kód (0,1,7+)** nezpůsobí chybu - `ja` skočí na
`0xD8199`, což je vstup do stejné funkce (čti další 90bajtový blok,
zkus dispatch znovu). ROM **čeká na další handshake `'Z'`/`0xA5`
DONEKONEČNA**, žádný timeout na její straně. Z pohledu klienta to bez
dalšího zásahu vypadá jako ticho/timeout, ne explicitní chyba.

**Závěr:** žádný skrytý mkdir/delete/free-space v ROM dispatchi
neexistuje. Jediný platný rozsah je `[2,6]`, hodnota 4 nevyužitá.

## Výsledek 2: funkční TSR rozšíření (otestováno na reálném HW)

Architektura, co **funguje** a byla ověřena end-to-end (`POST /sendRaw`
→ Atari → odpověď zpět, i s legacy `list` fungujícím beze změny vedle).
Tohle už není jen proof-of-concept - `pofo-driver/PFTD.asm` je produkční
pokračování přesně téhle architektury (HOOK3.COM byl prototyp, ze kterého
PFTD vychází), s reálně implementovanými příkazy HELLO a LIST extended -
viz sekce "PFTD command space (0x80+)" níže:

1. TSR se zavěsí na `int 0x61` vektor, **vždy** chainuje přes `jmp far
   [old61]` - nikdy `call far` (skutečný vstupní bod `int 0x61`, zjištěný
   přes živý vektor, je `E000:32E6` a končí `retf word 0x2`, ne `iret` -
   `call`+čekání na návrat by rozhodilo zásobník).
2. **Detekce (čtení payload[0]) přes "sleduj další volání" trik:** DOS je
   single-tasking, takže když `int 0x61 AH=0x30 AL=1` (receive) proběhne
   a *jakékoliv další* `int 0x61` přijde, předchozí receive už musí být
   dokončen. TSR si při AL=1 zapamatuje `DS:DX` (buffer) a `pending=1`;
   na příštím vstupu nejdřív zkontroluje `pending` a přečte payload[0] ze
   zapamatovaného bufferu.
3. **Odpověď na neznámý příkaz:** TSR po detekci `payload[0]>6` sám
   zavolá skutečnou `int 0x61` s `AX=0x3000` (AH=0x30 AL=0 transmit) a
   vlastním bufferem, PŘED tím, než udělá `jmp far [old61]` pro volání,
   které detekci spustilo. Funguje, protože:
   - Tohle vnořené volání znovu vstoupí do hooku, ale `pending` je už
     spotřebovaný (0) a `AX=0x3000≠0x3001`, takže vnořená instance jde
     rovnou do `jmp far [old61]` - originální transmit handler se zavolá
     přesně jako by ho zavolala ROM sama.
   - ROM čeká donekonečna (viz Výsledek 1) - zdržení návratu o dobu
     potřebnou k odeslání odpovědi nic nerozbije.
   - Žádná duplikace list/send/receive logiky - jen použití stejného
     zdokumentovaného `AH=0x30` transportního primitivu.
4. Ověřeno: `data=F0` → Atari odpoví libovolným obsahem (testováno `0xAA`
   i text `"ahoj"`, obojí dorazilo zpět na klienta nezměněné přes debug
   endpoint na jeho straně). Legacy `list` (payload[0]=6) funguje beze
   změny se stejným TSR nainstalovaným - žádná regrese.

### Dvě chyby, na které je třeba pamatovat při psaní podobného hooku

- **`int 0x21` (DOS API) uvnitř `int 0x61` hooku způsobuje pád** (pozorováno
  jako "GGGGG" přes celou obrazovku, ale reagovalo na Ctrl+Alt+Del). DOS
  není obecně re-entrantní. Řešení: pro výstup na obrazovku použít
  `int 0x10 AH=0x0E` (BIOS teletype), ne `int 0x21 AH=0x09`.
- **`xlat`/`mov bx,tabulka` bez explicitního `DS=CS`** čte nesmysl, pokud
  byl `DS` mezitím přepnutý na cizí buffer segment. Řešení: `cs xlatb`
  (segment override), nebo nastavit `DS=CS` hned po přečtení cizí paměti.

### Nové 8086/DOS gotchas (nalezeno při psaní PFTD - HELLO/LIST extended)

- **AL (payload[0]) se ztrácí mezi čtením a voláním dispatcherů**, pokud
  se mezi tím udělá jakýkoliv `mov ax, ...` pro jiný účel (např. uložení
  `DS:DX` páru pro LIST extended do `list_src_ds`/`list_src_si`) - AL
  není nijak chráněné jen proto, že logicky "patří" k předchozímu kroku.
  Řešení: znovu načíst AL z paměti (`mov al, [cs:payload0]`) těsně před
  KAŽDÝM `call dispatch_*`, nespoléhat na to, že registr "přežije"
  nesouvisející operace mezi tím. Projevilo se v DOSBoxu jako STUB61
  logující jen 1 bajt místo skutečné HELLO/LIST odpovědi - obě
  `dispatch_*` rutiny viděly špatné AL a žádná nesouhlasila.
- **`call`/`iret` stack shape mismatch** - subroutina volaná přes `call`
  nesmí sama nikdy dělat `iret` (stack má jen 2 bajty return adresy, ne
  6 bajtů FLAGS/CS/IP, co `iret` čeká) - vždy `ret`, a `iret` dělá až
  volající, hned po `call`, dokud je stack ještě čistý (viz
  `residentcheck.inc`: `check_already_resident` vždy `ret`,
  `pftd_int61_handler` sám dělá `iret` hned po `call`, než cokoliv
  dalšího pushne). Projevilo se jako "invalid opcode" pád v DOSBoxu při
  druhém spuštění PFTD (already-resident probe).
- **`shr reg, imm8` (imm8>1) není na CPU 8086 dostupné** - stejné omezení
  jako už zdokumentované `rol reg,imm8`. Řešení: čtyři jednotlivé
  jednobitové posuny za sebou (`shr al,1` ×4), stejně jako
  `hexprint.inc`'s `print_hex16` řeší `rol`.
- **`int 0x21` (Find First/Next, AH=0x4E/0x4F) volané zevnitř `int 0x61`
  hooku je bezpečné JEN díky stejnému časování jako recursive
  `int 0x61 AH=0x30` trik z Výsledku 2, bodu 3** - ROM čeká donekonečna
  na další handshake, takže DOS nikdy nevstoupí do critical section
  skrz PŮVODNÍ volání, což teprve umožňuje `int 0x21` odsud vůbec volat.
  Není to samostatně bezpečná věc - je to rozšíření stejného
  předpokladu o jednu úroveň dál (od "další `int 0x61` je bezpečný" k
  "i `int 0x21` je bezpečný, dokud běžíme uvnitř toho samého okna").
- **DS/ES disciplína**: `int 0x21` nezaručuje zachování ES napříč
  voláními (jen DS je bezpečné použít jako trvalý datový segment po
  přepnutí zpět na CS) - `list.inc`'s `dispatch_list` proto používá ES
  jen na kopii ASCIIZ patternu z cizího bufferu, pak přepne na
  `DS=CS` natrvalo a už nikdy nespoléhá na ES.

## Jak se dispatch (`0xD8190`) našel - metodologie

1. Transport (`int 0x61 AH=0x30`, AL=0-4) je dokumentovaný (Atari
   Portfolio Technical Reference Guide, Fn 30H) a ověřený za běhu
   (`HOOK61.COM` na `int 0x61`).
2. Volání transportu jde z 5 wrapperů na `0xC87A4-0xC87EF` (ROM B),
   volaných z funkce `0xD7D96` (open/close portů, počítá velikost
   přenosu) - nalezeno CPU single-step trace (`TRACE1.COM`, aktivace při
   prvním `AH=0x30`).
3. Kdo volá `0xD7D96` nešlo najít staticky (žádný `far call` immediate)
   ani dalším trasováním (CPU trace flag se ztrácí uvnitř DOS "spusť
   proces" sekvence na `0xEB3E`/`0xFF9F0` kvůli `pushf/popf` critical
   section - fundamentální limit TF-based tracingu přes tuhle hranici).
4. Řešení: emulace (Python + Unicorn) potvrdila strukturu hlavní menu
   smyčky (`0xC2D88`); hledání segmentu `D7D9` KDEKOLI mimo sebe sama
   našlo jediný zásah v horní ROM uvnitř menu dispatch tabulky
   (`0xED2EC`) - odtud vede cesta File transfer menu handler (`0xD7E77`,
   volá `0xD7D96` jako první instrukci) -> dispatch na `0xD8190`.
5. **Klíčové poučení:** volací kód v týhle ROM systematicky používá
   `push cs / call rel16` (near call v rámci segmentu), ne `far call`
   immediate - proto xref hledání na absolutní adresu dávalo nulové
   výsledky, dokud jsme nehledali podle SEGMENTU samotného.

## PFTD command space (0x80+)

`payload[0] >= 0x80` je vyhrazený prostor pro PFTD-only příkazy, daleko
mimo ROM rozsah `[2,6]` (viz Výsledek 1) - žádná kolize teď ani v žádné
budoucí ROM revizi. Aktuální seznam příkazů (HELLO 0x80, LIST extended
0x86, DRIVES 0x87), byte layout a capabilities bitmask jsou v
**`PROTOCOL.md`** - tohle zůstává jen jako investigativní záznam proč
a jak commanad space vznikl.

Proč DRIVES vrací jen počet, ne bitmask/media-typ per písmeno - viz níže "DIP DOS
critical error chování na jednotkách bez média", kde `AH=0x36`/`AH=0x32`
probing ukázal, že takový přístup na reálném Portfoliu vždy vyvolá
"Insert disk" hlášku a nekonzistentní chybové kódy. `AH=0x0E`/`AH=0x19`
jsou jediná kombinace, co se ověřila jako tichá (žádné I/O na médium).

### DIP DOS critical error chování na jednotkách bez média (nalezeno při vývoji DRIVES)

Na reálném Portfolio HW (NE v DOSBoxu - tam tohle vůbec nenastává,
proto to explorace/testování v DOSBoxu nikdy neodhalilo):

- `int 0x21 AH=0x36` (Get Disk Free Space) na jednotce bez vloženého
  média (např. prázdný card slot `A:`) vypíše `"Insert disk in drive
  A:"` přímo z Portfolio ROM/driver úrovně - PŘED jakýmkoliv DOS
  critical error mechanismem, ne jako jeho součást - a pak vyvolá
  `int 0x24` (critical error). Bez vlastního handleru to blokuje na
  `"Abort, Retry, Ignore?"` a čeká na klávesu.
- Fix: vlastní `int 0x24` handler s `AL=0` (Ignore) - jediná hodnota
  garantovaná na DOS 2.x/2.11 (`AL=3` "Fail" je DOS 3.0+ only,
  nedefinované chování na 2.x). Potlačí blokující prompt, ale NE tu
  "Insert disk" zprávu samotnou (ta se vypíše dřív, než `int 0x24`
  vůbec nastane).
- I s Ignore handlerem: `AX` po volání NENÍ konzistentní sentinel -
  pozorováno `AX=0x0003` na jedné jednotce bez média, `AX=0xFFFF` na
  jiné. Nelze na tom postavit spolehlivý "existuje/neexistuje" test.
- `int 0x21 AH=0x32` (Get Drive Parameter Block) byl vyzkoušen jako
  údajně I/O-free alternativa (dle Ralf Brown's Interrupt List by měl
  jen vrátit pointer na DPB v paměti, žádné čtení média) - na DIP DOS
  PŘESTO vyvolal identický critical error/prompt. DIP DOS se tedy v
  téhle funkci neřídí PC MS-DOS dokumentovaným kontraktem.
- `int 0x21 AH=0x0E` (Select Default Drive) kombinované s `AH=0x19`
  (Get Current Default Drive, aby se `AH=0x0E` zavolalo s tím samým
  diskem a nic reálně nezměnilo) se ukázalo jako skutečně tiché -
  žádná zpráva, žádný critical error, ověřeno na reálném HW.
- **Kontiguita ověřena na reálném HW:** `AH=0x36` probe A-Z (s Ignore
  handlerem, i s "Insert disk" hláškami jako akceptovaným side-effectem
  jednorázového manuálního testu) na testovaném Portfoliu ukázal `A:`
  a `B:` jako "Insert disk" (existují, prázdné card sloty, `AL≠0xFF`
  po Ignore), `C:` bez hlášky vůbec (existuje, RAM disk), `D:` a dál
  konstantně `AL=0xFF` (neexistuje) - přesně odpovídá `AH=0x0E`'s
  count=3. Na tomhle HW je tedy `1..count` skutečně souvislý rozsah od
  `A:` beze mezer - `AH=0x36`'s vysoký byte (`AH` po volání) NENÍ
  0/0xFF sentinel jak by se čekalo, jen `AL` samo rozlišuje platnost
  (`AL=0xFF` neplatná, cokoliv jiného platná) - `AX==0xFFFF` test by
  fungoval jen náhodou.
- **DOSBox je NEPOUŽITELNÝ pro testování `AH=0x0E`/kontiguity:**
  DOSBox-X (a obecně DOS 3.0+) implementuje `LASTDRIVE=` konfigurační
  koncept, který DOS 2.11/DIP DOS nemá - `AH=0x0E`'s count na DOSBoxu
  reflektuje `lastdrive` config direktivu (nastavitelnou v
  `dosbox-x*.conf`), NE skutečný počet použitých jednotek. Testováno:
  DOSBox s mountnutými `C:`/`D:`/`Z:` (nesouvislé, mezera D..Y) vrátilo
  `AH=0x0E` count=36 (vyšší než abeceda má písmen) - naprosto
  nesouvisí se skutečnou topologií. Na DOS 2.x/DIP DOS toto
  `LASTDRIVE`-style chování NEPLATÍ (RBIL: DOS 2.x count = "highest
  drive actually present", žádná CONFIG.SYS direktiva existuje) - proto
  reálný HW test byl nutný a DOSBox zde jen mate.
- **Poučení pro budoucí PFTD příkazy:** kterákoliv DOS funkce
  dokumentovaná jako "I/O-free" nebo s určitým návratovým kontraktem v
  Ralf Brown's Interrupt List (psáno primárně pro PC MS-DOS 3.0+) se NA
  DIP DOS musí ověřit empiricky na reálném HW, NIKDY jen v DOSBoxu -
  DOSBox se u minimálně dvou funkcí (`AH=0x32`, `AH=0x0E`) chová jinak
  než DOS 2.11/DIP DOS, oběma směry (jednou přísněji - critical error
  kde by nemělo být -, jednou volněji - LASTDRIVE cap, co DOS 2.x
  nemá).

### MKDIR/DELETE (0x88/0x89) - real-HW test výsledky a CF-ordering bug

Při prvním real-HW testu (PFTD build 0xFFFF0011) přes klientovy
mkdir/delete operace:

- MKDIR `C:\TESTDIR` (nový adresář) - `200 OK`, happy path funguje.
- MKDIR `C:\TESTDIR` podruhé (adresář už existuje) - očekáváno `409`
  (status=0x10), reálně vráceno `200 OK "Directory created"`.
- DELETE `C:\NOTEXIST.TXT` (neexistující soubor) - očekáváno `409`,
  reálně vráceno `200 OK "Deleted"`.

Ověřeno přes syrové bajty na drátě (bez klient-side interpretace):
DELETE na `C:\NOTEXIST.TXT` vrátil `20 00` (status=0x20 ok, errcode=0) i
když soubor evidentně neexistuje.

**Příčina: bug v dispatch_mkdir/dispatch_delete, ne DIP DOS chování.**
Obě rutiny testovaly `critical_error_flag` přes `cmp byte [cs:
critical_error_flag], 0` DŘÍV než `jc .dos_error` - ale `cmp` sama
přepisuje `CF` podle výsledku VLASTNÍHO porovnání. Protože
`critical_error_flag` je vždy 0 v úspěšném i neúspěšném případě `int 0x21`
(žádné podtečení při `cmp 0,0`), `CF` byl po tomto `cmp` vždy 0 bez ohledu
na to, co `int 0x21 AH=0x39`/`AH=0x41` skutečně vrátily - `jc .dos_error`
proto nikdy neskočil a kód vždy spadl do "success" větve.

Fix: `CF` se musí otestovat (`jc`/`jnc` nebo ekvivalent) OKAMŽITĚ po
`int 0x21`, dřív než cokoliv jiného (včetně `cmp` na `critical_error_flag`)
flag přepíše. Opraveno v `mkdir.inc`/`delete.inc` (viz jejich komentáře u
`dispatch_mkdir`/`dispatch_delete`), `BUILD_ID` bumpnut na `0xFFFF0012`.

**Poučení pro budoucí PFTD příkazy:** `CF` po `int 0x21` je jednorázová
hodnota platná jen do první další instrukce, co flags mění (`cmp`, `add`,
`sub`, ...) - i "neškodný" diagnostický `cmp` proti vlastnímu flagu ho
zničí. Pokud je potřeba `CF` i jiný stav zkombinovat, otestovat/uložit `CF`
jako první věc po `int 0x21`, než se sáhne na cokoliv dalšího.

Retest po opravě (stejný scénář, MKDIR/DELETE na existující/neexistující
cíl) je nutný před uzavřením test matrix z `PROTOCOL.md`'s MKDIR/DELETE
sekce - zejména zjistit skutečný DOS 2.x errcode pro "already exists" na
`AH=0x39` (určuje, jestli je errcode `2` vůbec dosažitelný, nebo jestli DOS
2.x vrací stejný kód jako "access denied").

**Retest po opravě (PFTD build 0xFFFF0012), real HW, drive C::**

| Scénář | Výsledek |
|---|---|
| MKDIR nový adresář (`C:\TESTDIR2`) | `200 OK`, `20 00` - happy path OK |
| MKDIR na existující adresář (`C:\TESTDIR`) | `409`, `errcode=4` - DOS 2.x `AH=0x39` vrací stejný kód (5, access denied) pro "already exists" jako pro obecné odepření, žádný distinct kód. **errcode `2` ("already exists") je tedy na tomto DOS/HW nedosažitelný** - mkdir.inc's `.dos_error` mapování (jen 3/5/8 rozlišeno) je potvrzeno správně, errcode 2 zůstává definován ve sdíleném enum jen pro budoucí HW s jemnějším rozlišením. |
| DELETE existujícího souboru (`C:\HELLO.TXT`) | `200 OK`, `20 00` - happy path OK, ověřeno zmizením z listingu a nárůstem `freeBytes` |
| DELETE neexistujícího souboru (`C:\NOTEXIST.TXT`) | `409`, `errcode=1` (not found) - `AH=0x41` vrací DOS kód 2, mapuje se správně |
| DELETE na adresář (`C:\TESTDIR2`, mimo plánovanou test matrix) | Přes PFTD: `409`, `errcode=0xFF`. Přes izolovaný test `TUNLKDIR.COM` (viz níže): `CF=1, AX=3` - **žádný critical error, žádný hang.** Původní domněnka ("AH=0x41 na adresář vyvolá int 0x24") byla nesprávná - viz vysvětlení níže. |

**Oprava/upřesnění: `errcode=0xFF` u "DELETE na adresář" nebyl skutečný
critical error.** Domněnka výše byla nepotvrzená spekulace. Ověřeno
izolovaným standalone testem `pofo-driver/tests/TUNLKDIR.asm/.COM`
(volá `int 0x21 AH=0x41` přímo na `C:\TESTDIR2`, bez PFTD, bez
`critical_error.inc` handleru, bez čehokoliv jiného) na reálném HW:
**žádný "Abort, Retry, Ignore?" prompt, žádné zaseknutí** - `int 0x21`
se vrátilo normálně s `CF=1, AX=3` (path not found), přesně stejný kód
jako "path not found" jinde. `AH=0x41` na adresáři je tedy na tomto
DOS/HW jen běžná DOS chyba, ne critical-error case.

`errcode=0xFF` pozorovaný přes PFTD musel být způsoben něčím jiným než
samotným `AH=0x41` voláním - kandidáti: zbytkový stav
`critical_error_flag` z předchozího MKDIR volání ve stejné PFTD relaci
(flag se nuluje na začátku každého dispatche, takže by neměl přetrvávat,
ale nebylo to ověřeno izolovaně), nebo jiná interakce specifická pro
PFTD dispatch cestu. **Nevysvětleno, vyžaduje další zkoumání pokud bude
relevantní** - prakticky nedůležité, protože DELETE je scoped na
soubory (`AH=0x41`) a volání na adresář je stejně mimo podporovaný
použití; zaznamenáno jen aby se nešířila nepravdivá informace o tom, že
`critical_error.inc` handler je nutný kvůli tomuto konkrétnímu případu.

**MKDIR bez vloženého média (`A:\TESTDIR`, karta vyndaná z A:), izolovaný
test `TMKDIRA.COM` (stejný přístup jako TUNLKDIR - `int 0x21 AH=0x39`
přímo, bez PFTD, bez `critical_error.inc` handleru):**

Na rozdíl od DELETE-na-adresáři (viz výše) se tady critical error
**skutečně objevil** - potvrzeno přímým pozorováním na reálném HW:
"Abort, Retry, Ignore?" prompt se objevil a bez handleru čekal na
klávesu. Pozorováno pro všechny tři volby:
- **Abort**: nic se nestane (program se neukončí viditelně/nevypíše nic
  dalšího - přesné chování nebylo dál zkoumáno, mimo scope tohoto testu).
- **Retry**: (nezaznamenáno zvlášť, patrně zopakuje pokus a narazí na
  stejný prompt znovu, protože médium pořád chybí).
- **Ignore**: `int 0x21` se vrátí, `AX=3` (path not found) - konzistentní
  hodnota, na rozdíl od DRIVES' `AH=0x36` nálezu, kde `AX` po Ignore bylo
  nekonzistentní mezi jednotkami (`0x0003` vs `0xFFFF`). Jen jeden
  pozorovaný vzorek zatím - nestačí na to změnit `critical_error.inc`'s
  konzervativní `errcode=0xFF` (nedůvěřovat `AX` po Ignore) mapování,
  ale je to datový bod pro budoucí zpřesnění.

**Závěr: `critical_error.inc` handler je i nadále nutný** - potvrzuje
původní důvod jeho existence (analogie s `AH=0x36` u DRIVES). Bez něj by
MKDIR/DELETE na jednotce bez média zablokovalo PFTD na Portfoliu, dokud
by někdo fyzicky nezmáčkl klávesu.

**Poznámka k ROM/File Transfer Server chování:** oficiální ROM file
transfer server (LIST, payload[0]=6) při listování neexistujícího média
také vypíše "Insert disk", ale (dle pozorování) nezobrazuje
Abort/Retry/Ignore, pokud médium "je" (např. prázdný, ale přítomný
card). To je jiný scénář, než "médium úplně chybí" - nebylo dál
zkoumáno, jestli ROM má vlastní `int 0x24` handler, nebo jestli jde o
jinou DOS/driver úroveň volání, co critical error vůbec nevyvolá.

Nezbývá dotestovat z plánované matrix (vyžaduje fyzickou manipulaci s
médiem): DELETE bez média (analogicky k MKDIR výše, neotestováno
odděleně, ale očekává se stejné chování), DELETE na write-protected
médiu.

### RMDIR (0x8A) - real-HW test výsledky

Přidáno jako doplněk k MKDIR/DELETE (`int 0x21 AH=0x3A`, sdílí
`critical_error.inc` a stejný CF-first fix jako mkdir.inc/delete.inc,
tentokrát napsáno rovnou správně od začátku). PFTD build `0xFFFF0013`,
retest přes klientovo RMDIR volání:

| Scénář | Výsledek |
|---|---|
| RMDIR prázdný adresář (`C:\RMTEST`, čerstvě vytvořený přes MKDIR) | `200 OK`, `20 00` - happy path OK, ověřeno zmizením z listingu |
| RMDIR neprázdný adresář (`C:\TESTDIR2`, obsahoval `A.TXT` - pozůstatek staršího ručního testu) | `409`, `errcode=4` (access denied) - potvrzuje předpoklad z `PROTOCOL.md`: DOS 2.x nemá distinct kód pro "not empty", stejné chování jako MKDIR "already exists" |
| RMDIR neexistující cesta (`C:\NOTEXIST`) | `409`, `errcode=1` (not found) |

Žádné netestováno-bez-média/write-protected scénáře pro RMDIR zvlášť -
očekává se stejné chování jako MKDIR (critical error handler chrání),
po analogii, ne odděleně ověřeno.

### Write-protected médium (D:) - real-HW test výsledky

`D:` na testovaném HW je read-only/write-protected jednotka (obsahuje
`TEST.TXT`, 40 bajtů). Test přes klientovo MKDIR/DELETE volání:

| Scénář | Výsledek |
|---|---|
| MKDIR `D:\WPTEST` | `409`, `errcode=1` (not found) - **ne** `errcode=4` (access denied), jak by se čekalo. DOS 2.x `AH=0x39` na téhle write-protected jednotce vrací kód mapující se na "not found", ne na "access denied" - buď specifický pro tenhle typ jednotky (D: může být ROM/RAM disk, ne běžná disketová/kartová mechanika), nebo obecně platí, že write-protect a "cesta neexistuje" nejsou na `AH=0x39` odlišitelné na DOS 2.x. Nedovyšetřeno, který z důvodů. |
| DELETE `D:\TEST.TXT` (existující soubor) | `409`, `errcode=0xFF` (**critical error fired**) - na rozdíl od MKDIR výše, `AH=0x41` na write-protected médiu SKUTEČNĚ vyvolá `int 0x24`. `critical_error.inc` handler zafungoval správně: PFTD neblokovalo, odpovědělo normálně s `errcode=0xFF`, `TEST.TXT` zůstal netknutý (ověřeno listingem po pokusu). |

**Závěr:** stejná operační třída (MKDIR vs. DELETE) se na stejné
write-protected jednotce chová různě - MKDIR dostane běžnou DOS chybu,
DELETE vyvolá critical error. To je konzistentní s dřívějším pozorováním
(DELETE na adresář via PFTD hlásilo `0xFF` i když izolovaný test mimo
PFTD to samé volání ukázal jako obyčejnou chybu) - `AH=0x41`/Unlink se
na tomhle HW zdá být náchylnější k critical error cestě než `AH=0x39`.
Potvrzuje definitivně, že `critical_error.inc` handler je nutný pro
DELETE, nejen pro MKDIR - bez něj by tenhle test zablokoval PFTD na
Abort/Retry/Ignore.

### Bez média (A:, prázdný card slot) - DELETE/RMDIR real-HW test výsledky

Doplňuje dřívější izolovaný `TMKDIRA.COM` test (MKDIR bez média,
potvrdil critical error). Test DELETE/RMDIR na `A:` (žádná karta
vložena) přes klientovo DELETE/RMDIR volání, PFTD build `0xFFFF0013`:

| Scénář | Výsledek |
|---|---|
| DELETE `A:\NOTEXIST.TXT` | `409`, `errcode=0xFF` (critical error fired) - konzistentní s `AH=0x41` na write-protected `D:` (viz výše), `AH=0x41` vyvolává critical error i na jednotce zcela bez média. |
| RMDIR `A:\NOTEXIST` | `409`, `errcode=1` (not found) - **žádný** critical error. `AH=0x3A` na jednotce bez média se na tomto HW chová jako běžná DOS chyba, ne critical error - na rozdíl od `AH=0x39` (MKDIR, potvrzeno critical error přes `TMKDIRA.COM`) a `AH=0x41` (DELETE, critical error i tady i na `D:`). |

Systém zůstal plně responzivní po obou voláních (potvrzeno klientovým
status dotazem, žádné zaseknutí) - `critical_error.inc` handler funguje spolehlivě i
když se critical error skutečně spustí, a nezpůsobuje problém ani u
volání, co critical error nevyvolají (RMDIR zde).

**Souhrn napříč AH=0x39/0x41/0x3A, kdy critical error skutečně nastává
(potvrzeno na reálném HW, ne domněnka):**

| Volání | Bez média (A:) | Write-protected (D:) | Adresář místo souboru |
|---|---|---|---|
| `AH=0x39` MKDIR | critical error (TMKDIRA.COM) | běžná DOS chyba (`errcode=1`, ne `4` - nevysvětleno) | n/a |
| `AH=0x41` DELETE (Unlink) | critical error | critical error | běžná DOS chyba (TUNLKDIR.COM, mimo PFTD) |
| `AH=0x3A` RMDIR | běžná DOS chyba | netestováno | n/a |

Žádná jednotná pravidla - critical error vs. běžná DOS chyba záleží na
kombinaci konkrétní DOS funkce a konkrétní příčiny, ne jen na "je něco
špatně s médiem". Potvrzuje `ROM_RESEARCH_NOTES.md`'s obecné poučení
(DIP DOS se nechová predikovatelně dle RBIL kontraktu) - `critical_error.
inc` handler musí zůstat nainstalovaný pro všechny tři příkazy
(MKDIR/DELETE/RMDIR), protože není spolehlivý způsob, jak předem
zaručit, že konkrétní volání critical error nevyvolá.

### RENAME (0x8B) - transportní bug (90B ROM buffer overflow) a real-HW test výsledky

**Bug nalezený při prvním real-HW testu, ne DOS/critical-error problém
tentokrát - čistě transportní, a výhradně na klientské straně.** Klient
stavěl RENAME request do lokálního bufferu dost velkého na dvě plné
8.3 cesty (163 bajtů), ale posílal vždy celý tenhle buffer přes drát,
i když skutečný obsah (krátká cesta) byl jen ~34 bajtů.

ROM's File Transfer Server hlavní smyčka volá `int 0x21 AH=0x30 AL=1`
(receive block) do fixního stack-frame bufferu `[bp-0x94]`, velikost
90 bajtů (`push 0x5A` v disassembly, viz sekce výše "Výsledek 1"). Tenhle
ROM buffer nemá žádnou bounds-check ochranu na přijímací straně. Poslání
163 bajtů do 90bajtového bufferu přepsalo přilehlou stack frame ROM
smyčky - **potvrzeno na reálném HW jako "communication error" a pád
spojení s Portfoliem** (uživatelské hlášení, ne jen teoretické riziko).

Wire protokol (`sendBlock`/`receiveBlock`) je sender-declares-length
(2B LE prefix + přesně `len` bajtů dat) - poslání MÉNĚ bajtů, než je
velikost lokálního bufferu, je protokolem naprosto v pořádku a ostatní
příkazy (HELLO/LIST/DRIVES) to tak dělaj oboustranně (odpovědi jsou
1-12 bajtů, ne padded na 90). Chyba byla výhradně v tom, že RENAME na
klientské straně posílalo velikost celého lokálního bufferu místo
skutečné potřebné délky.

**Fix (na klientské straně):** RENAME teď počítá skutečnou potřebnou
délku (`3 + oldLen + 1 + newLen + 1`), kontroluje, že se vejde do 90B
ROM limitu, a posílá jen tolik bajtů, kolik je skutečně potřeba. Pro
dvě 8.3 cesty by k přetečení 90B limitu došlo až při ~4 a více úrovních
vnořených adresářů na obou stranách současně (spočítáno, ne testováno).

**Retest po opravě (PFTD build 0xFFFF0014 nezměněn - bug byl jen na
klientské straně), real HW, drive C:**

| Scénář | Výsledek |
|---|---|
| RENAME v rámci stejného adresáře (`C:\TUNLKDIR.COM` -> `C:\RENAMED.COM`) | `200 OK`, ověřeno zmizením starého jména a existencí nového (stejná velikost 385B) |
| RENAME neexistujícího zdroje | `409`, `errcode=1` (not found) |
| MOVE mezi adresáři (`C:\RENAMED.COM` -> `C:\TESTDIR\MOVED.COM`) | `200 OK`, ověřeno - soubor skutečně skončil uvnitř `TESTDIR`, stejná velikost. **Potvrzuje, že `AH=0x56` na tomto DOS/HW funguje i jako move napříč adresáři v rámci stejného disku, ne jen jako rename ve stejné složce.** |

Netestováno: cross-drive rename (`C:` -> `D:`, očekává se DOS chyba,
přesný kód neznámý), rename na existující cíl (errcode 4 očekáván po
analogii s MKDIR/RMDIR, neověřeno), rename bez média/na write-protected
médiu (očekává se critical error po analogii s MKDIR/DELETE/RMDIR,
neověřeno).

### Kontiguita 1..count je architektonicky garantovaná, ne náhodná (uzavřeno)

Ověřeno druhým testem s uměle přidanou 4. jednotkou (vlastní minimalistický
FAT12 block device driver, `DEVICE=` v CONFIG.SYS): `AH=0x0E` count
naskočilo z 3 na 4 a nová jednotka se objevila přesně na `D:` (hned za
`C:`), ne na libovolném jiném písmenu.

**Proč je tohle spolehlivý invariant, ne jen shoda náhod:** DOS 2.x/DIP
DOS nemá ŽÁDNÝ mechanismus, který by dovolil vytvořit mezeru v
přiřazení písmen:
- `ASSIGN` (dostupné od DOS 2.0) pouze PŘESMĚRUJE existující jednotku
  na jinou - nevytváří novou jednotku ani mezeru.
- `SUBST`/`JOIN` (mapování cesty na jednotku) jsou DOS 3.1+ - na
  DOS 2.11/DIP DOS nedostupné vůbec.
- `LASTDRIVE=` (rezervace/cap písmen v CONFIG.SYS) je DOS 3.0+ - DOS
  2.x nemá způsob, jak "přeskočit" písmeno bez driveru, který by ho
  fyzicky obsadil.
- Device drivery (`DEVICE=` v CONFIG.SYS) dostávají DALŠÍ VOLNÉ písmeno
  v pořadí, v jakém je DOS při bootu zpracuje (potvrzeno testem výše) -
  driver si nemůže vybrat konkrétní písmeno mimo pořadí přes
  dokumentované DOS API.

**Závěr:** na Atari Portfoliu (2 card sloty + interní RAM disk pevně
zadrátované jako `A:`/`B:`/`C:`, cokoliv navíc - Memory Expander,
periferie jako ZIP drive, virtuální disk - se připojuje sekvenčně za
ně) bude `AH=0x0E`'s count VŽDY minimálně 3 a VŽDY znamená přesně
"`A:` až <count>-té písmeno, beze mezer". `pofo-driver/drives.inc`
nepotřebuje žádný záchranný/ověřovací mechanismus navíc - jednoduchý
count je definitivně dostačující a spolehlivý.

## DOSBox testovací nástroje (pofo-driver/tests/)

DOSBox nemá nativní `int 0x61` handler (NULL vektor by default), takže
PFTD tam samo o sobě nejde vyzkoušet end-to-end bez pomocných nástrojů:

- `STUB61.COM` - minimální `int 0x61` handler, co dá PFTD's `.chain`
  (`jmp far [old61]`) kam přistát, místo NULL vektoru. Na `AX=0x3000`
  (transmit) loguje odesílané bajty jako hex přes `int 0x10 AH=0x0E`
  (BIOS teletype, bezpečné volat re-entrantně na rozdíl od `int 0x21`).
- `THELLO.COM` / `TLISTEXT.COM` - simulují ROM's receive-block handshake
  (`int 0x61 AX=0x3001` s payload[0]=0x80/0x86 v bufferu, pak libovolné
  další `int 0x61`), aby vyvolaly PFTD detekci a `dispatch_hello`/
  `dispatch_list` bez reálného Portfolia nebo klienta na drátě.
- Co tohle OVĚŘÍ: že PFTD správně detekuje payload[0] a zavolá správný
  dispatcher bez zaseknutí/pádu, a (díky STUB61 logu) i přesné bajty
  odpovědi. Co NEOVĚŘÍ: chování na reálném Portfolio hardwaru (HW
  detekce je v DOSBoxu vypnutá přes `CHECK_POFO=0` build) - to
  potřebuje skutečný Portfolio a reálného klienta na kabelu.
- Použití v DOSBoxu: `pofo-driver/loadtest.bat` nahodí `STUB61` a
  `PFTDN` (CHECK_POFO=0 build) jedním příkazem; samotný test nástroj
  (`TESTS\THELLO.COM` / `TESTS\TLISTEXT.COM`) se pak spouští ručně podle
  toho, co se zrovna testuje.

## Nástroje (historické - objevovací fáze před PFTD)

Tyhle nástroje posloužily k nalezení ROM dispatche a transportu (viz
Výsledek 1/2 a metodologie výše); jejich poznatky jsou už plně zachycené
v téhle dokumentaci, takže samotné soubory byly z repa odstraněny.
- `HOOK3.asm/.COM` - první funkční TSR proof-of-concept (Výsledek 2) -
  `pofo-driver/PFTD.asm` je jeho produkční pokračování.
- `HOOK61.asm/.COM` - jednodušší TSR, jen logování `int 0x61` provozu.
- `VECDUMP.asm/.COM` - vypsal živý `int 0x61` vektor (SEG:OFF) -
  potvrdil `E000:32E6` použitý ve Výsledku 2.
- `TRACE1.asm/.COM` - CPU trace po prvním `AH=0x30`, log CALL/RET - vedl
  k nalezení `0xD7D96` (viz metodologie, krok 2).

## Soubory
- `ROM_A.bin` - 128KB dump E0000-FFFFF (systémová ROM A)
- `ROM_B.bin` - 128KB dump C0000-DFFFF (ROM B, dumpnuto s prázdným
  credit-card slotem - jde o vnitřní ROM, ne obsah karty)

## Další krok
Aktuální stav implementace, checklist pro přidání nového PFTD příkazu
a seznam plánovaných nápadů (mkdir, delete, SETTIME) jsou teď v
`PROTOCOL.md` - důvody/zdůvodnění jednotlivých DOS volání (`AH=0x39`,
`AH=0x41`/`0x3A`, `AH=0x2D`/`0x2B`) a jejich rizika na DIP DOS zůstávají
zdokumentované jen tady, viz "DIP DOS critical error" sekce výše.
