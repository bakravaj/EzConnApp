# Legacy API: исследование оригинального EasyConn

Дата: 2026-09-18. Этап: reverse-engineering checkpoint.
Уточнение пользователя: машина работает на RTOS-32, доступа для установки
нового агента нет. Упоминания QNX в исходном плане не описывают эту машину.
Для ускорения используем штатный протокол: 250 KiB при bit 5 ответа 38,
запрос compression/turbo через bits 0/4 команды 14 (0x31 вместе с bit 5).
Распаковка Z/H — на уровне пакета после LRC, T/X — после сборки файла.
Обновление реализации: добавлены C++ codec, чтение версий и отдельный C# LegacyHost.
Проверки с DLL выполняются пакетами через stdin/stdout дочернего процесса.
Результаты ниже описывают оригинал; рабочий адрес нового клиента — 192.168.1.66:2000.
Источники: локальные оригинальные сборки, ILSpy CLI 9.1.0.7988 и reflection
в Windows PowerShell/.NET Framework. Проверено офлайн; связь с Kaparossa не выполнялась.

## Идентификация источников

| Файл | SHA-256 |
| --- | --- |
| EasyConn.exe | 436FB6C56BD7772551267FD89136ABA466A6DA493FBB09B44F574EC543972A0B |
| Lib/TeleAssistenza.dll | DF945D2B4CAD3E06F98E7AACD8FC976CFDD377A463E1BCDC2649B0E5F2714E0A |
| Lib/Tool.dll | 3B807E19BFFC8FCFC067F186928CDABFDE053B2A069C9DE48C4B3C8A45A19126 |

TeleAssistenza имеет AssemblyVersion 0.2.0.0. Бинарники не изменены.
Полный инвентарь типов, конструкторов и объявленных public/nonpublic методов:
[legacy_api_inventory.json](legacy_api_inventory.json).

## Типы и распределение ответственности

`Schnell.TeleAssistenza.Protocollo` — **namespace, не класс**.
Пакет реализует `Schnell.TeleAssistenza.Protocollo.formatoComando`.
Все найденные типы TeleAssistenza публично видимы, отдельных internal-классов нет.

| Тип (сокращённое имя) | Конструкторы / назначение |
| --- | --- |
| Protocollo.formatoComando | Пустой и восьмиаргументный; кодирование, разбор и накопление пакетов |
| Protocollo.BytesInsufficientiException | Пустой |
| Protocollo.codifica | Пустой; static getCodifica(string) |
| Protocollo.RandomPassword | Пустой и static initializer; Generate(), Generate(int), Generate(int,int) |
| Protocollo.HTTP | Пустой; серверный HTTP-запрос |
| ArgomentiEventi.MessageEventArgs | Пустой; Buff, NumeroByte, Flag, Message |
| MAC.MACManager | Static-класс, без конструктора; серверная БД |
| User.Autenticazione | Пустой и static initializer; серверные пользователи/БД |
| Statistiche.Statistiche | Пустой и static initializer; серверная статистика |
| Statistiche.Statistiche+ClientAction / +TipoLogs | Вложенные enum серверной статистики, не ID машинных команд |

`DriverConnessione`, `DriverTrasferimenti`, `Comandi`, `Form1.Comandi_Comando_In`
находятся в **EasyConn.exe**, а не в TeleAssistenza.dll.
DriverConnessione целиком реализован в EXE: TCP/Serial, таймеры, ICMP ping,
опциональный HTML-префикс, чтение и отправка. Конструкторы: `()` для Serial и
`(string IP, int porta)` для TCP. DLL предоставляет пакет и аргументы событий;
Tool.dll предоставляет BytesManager и zlib-сжатие через SharpZipLib.

## API пакета

```csharp
formatoComando()
formatoComando(byte tipoPacchetto, byte LSB, byte MSB,
              byte mittente, byte destinatario, byte comando,
              byte tipoMessaggio, byte[] dati)
void SetPacchetto(byte[] bufferCom)
void SetPacchettoDaRete(MessageEventArgs info)
byte[] GetPacchettoInByte()
void SetDati(byte[] dati)
byte[] GetDati()
string GetDatiStr()
byte GetComando()
byte GetTipoPacchetto()
void SetTipoPacchetto(byte tipo)
bool isErrore()
string GetTestoErrore()
bool IsCompleto()
static void SetVersione(byte versione)
static byte GetVersione()
static bool IsCompressione { get; set; }
static bool IsTurbo { get; set; }
```

Private: восьмиаргументный SetPacchetto, calcolaDimensione(), calcolaLRC().
Нет публичных getter для адресов, типа сообщения и LRC: будущему host понадобятся
разбор wire bytes или reflection для диагностического доступа.
Статические version/compression/turbo общие для всех экземпляров: при нескольких
сессиях bridge нужно изолировать либо явно управлять этим состоянием.

## Бинарный формат

Пусть `w` — ширина длины, `N` — число wire-байтов payload, `L=N+4`.
Длина little-endian; общий размер пакета `N+w+6`.

| Смещение | Размер | Значение |
| --- | --- | --- |
| 0 | 1 | Тип пакета |
| 1 | w | L: адреса + команда + тип сообщения + payload, без LRC |
| 1+w | 1 | Получатель (destinatario) |
| 2+w | 1 | Отправитель (mittente) |
| 3+w | 1 | ID команды |
| 4+w | 1 | Тип сообщения |
| 5+w | N | Payload |
| 5+w+N | 1 | LRC |

| Тип | Hex | w | Использование в исходном клиенте |
| --- | --- | --- | --- |
| P | 50 | 1 | Короткие команды |
| C | 43 | 2 | Несжатые блоки |
| Z | 5A | 2 | Сжатые блоки |
| T | 54 | 2 | Turbo, фрагменты сжатого файла |
| B | 42 | 4 | Большие несжатые блоки |
| H | 48 | 4 | Большие сжатые блоки |
| X | 58 | 4 | Большие turbo-блоки |

Клиент отправляет из `0x41` в `0x42`, тип сообщения `0x57` (W).
`0x45` (E) обозначает ошибку; первый байт payload — код ошибки.
Полный набор типов ответов устройства ещё не подтверждён.

LRC — XOR четырёх внутренних байтов длины, получателя, отправителя, команды,
типа сообщения и каждого wire-байта payload. **Тип пакета в XOR не входит**.
Для корректных коротких пакетов старшие внутренние байты длины равны нулю.
Из формата следуют пределы payload: P — 251, двухбайтовые типы — 65531.
Четырёхбайтовую длину оригинал читает как signed Int32; новому парсеру нужен
собственный разумный предел выделения памяти.

Важные особенности оригинала:

- Конструктор сохраняет переданные LSB/MSB. calcolaDimensione вычисляет только
  размер буфера. Для корректной длины нужен SetDati, включая пустой payload.
- GetDati распаковывает Z/H; T/X не распаковываются на уровне пакета.
- Несовпадение LRC лишь печатается в stdout, пакет не отвергается.
- SetPacchetto не выставляет IsCompleto; накопитель выставляет его отдельно.
- SetPacchettoDaRete использует всю длину Buff, а не NumeroByte; избыток байтов
  сверх одного пакета вызывает исключение. Это нельзя переносить в TCP-парсер:
  один read может содержать части пакетов или несколько пакетов.
- SetPacchetto не валидирует вход перед индексированием; пустой error payload
  также небезопасен. Будущий C++ parser должен проверять размер и LRC до доступа.

Источник: TeleAssistenza.decompiled.cs, formatoComando, строки 68–514.

## Локальное подключение

Последовательность восстановлена статически из Form1 и Comandi в EXE:

1. TCP connect к 192.168.0.1:2000 по настройкам. HTML-префикс по умолчанию выключен.
2. Команда 1: ASCII challenge длиной 8; ожидается ASCII `f(challenge)`.
3. После успешного сравнения команда 2: ASCII `f(f(challenge))`.
4. После неошибочного ответа 2 — команда 14, payload `20` hex для локального режима.
5. После версии ПО — команда 38, пустой payload (опции).
6. После опций — команда 3, пустой payload (версия протокола).
7. Оригинальный UI затем открывает диск D; минимальный Probe должен завершить
   проверку на версии и не запускать дальнейшие UI-сценарии.

`f`: parse uint32 hex → XOR 0x1234FEDC → обмен 16-битных половин → rotate-right 1
→ uppercase hex шириной 8. Источник: Protocollo.codifica.getCodifica.
Это описание совместимости оригинала; ответы и сроки на реальном устройстве
ещё не проверены. Аутентификация не связана с серверным User.Autenticazione.

COM пропускает команды 1/2 и начинает с 14. Serial: 8N1, DTR=true;
порт и baud rate берутся из настроек и в поставленном config пустые.
Не следует назначать выдуманную скорость по умолчанию.

Источник: EXE строки 5810–5865, 6073–6118, 6140–6220, 6586–6609,
Comandi.Autenticazione/VersioneSoftware/RichiestaOpzioni/VersioneProtocollo.

## Сжатие и turbo

VersioneSoftware отправляет битовую маску: bit 0 — compression, bit 4 — turbo,
bit 5 — поддержка больших пакетов (запрашивается всегда). Для локального TCP
и COM UI сбрасывает compression/turbo, поэтому запрос имеет payload 0x20.
Ответ команды 38 анализируется по тем же битам 0/4/5.
В режиме turbo сначала сжимается весь файл, затем делится на блоки T/X;
обычное сжатие обрабатывает отдельные блоки Z/H. Tool.BytesManager.Compress
использует zlib wrapper: Deflater(-1, noZlibHeaderOrFooter:false).
Golden vectors ниже пока проверяют только несжатые payload.

## Команды

### Экран (исследование оригинального EXE)

Form1.menuVideo_Click вызывает StreamVideo(1), затем StreamVideo(0) после
каждого завершённого кадра. Команда 67 имеет однобайтовый payload reset.
Ответ длиной 2 — little-endian размер передачи в KiB. Ответ иной длины
оригинал трактует как отсутствие обновления и снова запрашивает 67.

Данные забираются командой 26: `[blockIndex8, blockKiB8]`, для индекса >255
`[indexLow, indexHigh, blockKiB8]`. Число блоков — ceil(sizeKiB/blockKiB).
В новом клиенте blockKiB=62 (обычный локальный размер оригинала).
T/X означают сжатие целого файла; Z/H — отдельных пакетов. Пока выключено.
Stop в оригинале прекращает polling, отдельной команды остановки не обнаружено.

Изображения — PCX: header 128 bytes, 8 bits/plane, 3 RGB planes, RLE.
Позиция участка — xmin/ymin, размеры — xmax-xmin+1/ymax-ymin+1;
полный экран — HscreenSize/VscreenSize. BMP merger сохраняет предыдущий кадр;
Уточнение по реальным кадрам: HscreenSize/VscreenSize в дельта-кадре равны
размерам участка (например 1241x650 при xmin=1/ymin=248), а не всего экрана.
Как и оригинальный BMP merger, клиент сохраняет размеры первого кадра
и проверяет размещение следующих участков относительно него.
Однобайтовый ответ команды 67 после reset=1 — промежуточный ответ без файла;
для одиночного кадра также требуется продолжать polling до получения изображения.
RGB (181,183,248) в последующих кадрах обозначает неизменённый пиксель.
Первый кадр рисуется полностью, включая этот цвет.
Источник: UCStreamVideo.CaricaImmagine, PCX, BMP, Comandi.StreamVideo,
Comandi.InizioScaricaFile, DriverTrasferimenti.GetDettagliBlocco/BufferizzaFile.

Команда 70 возвращает имя файла снимка; далее оригинал использует 25 и 26.
В новой функции Single frame выбран подтверждённый потоковый запрос 67/reset=1,
поэтому отдельный файловый путь команды 70 пока не реализован.

### Чтение каталога и диска (добавлено 2026-09-18)

- Команда 24: ASCII-путь без NUL, например `D:\`; после успешного ответа
  команда 23 с пустым payload. Меняет текущий каталог сессии, файлы не изменяет.
- Ответ 23: ASCII, строки разделены LF. Суффикс `\` обозначает каталог,
  `.\` пропускается, `..\` означает родительский каталог. Размеров/дат файлов
  в этом формате не найдено. Регистр сохраняем, хотя оригинальный UI понижает его.
- Команда 45: ASCII-корень диска. В EXE есть вызов `C:\`, а для статистики D
  встречается `D\` без двоеточия. В новом GUI используется стандартный `D:\`;
  его принятие устройством ещё требует проверки, автоматических повторов нет.
- Ответ 45: пять little-endian 32-битных полей на смещениях 0/4/8/12/16:
  serial, bytesPerCluster, totalClusters, freeClusters, badClusters. Оригинал
  читает Int32; новый клиент сохраняет serial как signed, счётчики как unsigned
  и использует 64-битные произведения для размеров. Отрицательные значения-сентинелы
  и расширенные ответы оригинальным кодом не описаны; валидация требует 20 байт.

Источники: Comandi.CambioDirectory/ContenutoDirectory/InfoDisco,
UCRisorse.caricaListView, ElementoLista.SetElemento, UCStatistiche.SetInfoDisco;
Tool.BytesManager.StringToByte/GetStringFromByte используют Encoding.ASCII.
Не-ASCII имена пока вызывают явную ошибку вместо отправки искажённого пути.

Реальная проверка пользователя: MultiAssembler 4.3.16, protocol 3, options 0x22;
двухэтапная аутентификация и чтение версий успешно завершены. Бит 1 options
не расшифрован; bit 5 установлен, bit 0 и bit 4 сброшены. Ответы использовали W.

ID находятся не в enum DLL, а в static-полях Comandi в EXE.
Полная извлечённая таблица: [legacy_commands.csv](legacy_commands.csv).
Колонки включают тип пакета, адреса, команду и тип сообщения из конструкторов;
тип файлового пакета впоследствии может переключаться во время передачи.
Наличие команды в таблице не означает её реализацию или разрешение на отправку.

## Виртуальная клавиатура: проверка 2026-09-21

В `UCStreamVideo_KeyUp` оригинального EasyConn функциональные клавиши, Home и стрелки
формируются как `00 00 00 00 VK 00`. `Comandi.TastieraVirtuale` передаёт их командой 66,
пакетом P, сообщением W, от 65 к 66. Для F1 полный пакет:
`50 0A 42 41 42 57 00 00 00 00 70 00 6C`.
Тест `ProtocolTest::oracle` сравнивает все поддержанные клавиши с оригинальной DLL.

Переключатель `utilizzoRemoto` действует локально: разрешает добавление/извлечение
элементов очереди. Отдельного сетевого включения в этом обработчике нет.
Оригинал отправляет клавишу после завершённого кадра или ответа 67 без изображения,
после ответа 66 снова запрашивает 67. EzConn использует ту же последовательность.

На реальной Kaparossa получены ответы 66 с сообщением `0x57` и payload `00`, но
пользователь не наблюдает действия клавиш. Оригинал считает ошибкой сообщение E
(`0x45`); смысла `00` как подтверждения выполнения нажатия в клиентском коде нет.
Причина отсутствия действия пока не установлена; наличие ответа не доказывает
доставку события в интерфейс MultiAssembler. Серверного кода в проекте нет.

## Проверка и дальнейшая работа

[Скрипт проверки](../scripts/Inspect-Legacy.ps1) загружает DLL в .NET Framework,
сохраняет inventory, создаёт 5 пакетов и проверяет точные bytes и обратный разбор.
Результат: PASS для protocol-version, software-version-local, options,
medium-frame, large-frame; SHA-256 оригиналов до/после совпали.
[Контрольные пакеты](legacy_oracle_vectors.json) получены от оригинальной DLL.

Следующий этап: CMake/C++20 каркас Qt Widgets/Core/Probe, TCP/Serial и лог TX/RX;
отдельный LegacyHost с IPC и строгим контрактом. Текущий PowerShell-скрипт —
исследовательский инструмент, не реализация LegacyHost.
Qt, CMake и MSVC не обнаружены в PATH/проверенных стандартных каталогах;
доступен .NET SDK 8.0.419. Полная проверка расположения C++ toolchain ещё предстоит.
Нужны дополнительные проверки split/coalesced frames, повреждённых пакетов,
границ длины, сжатия и handshake на устройстве. Рабочая совместимость с машиной
пока не заявляется.
