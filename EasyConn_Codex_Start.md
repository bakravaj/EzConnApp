# EasyConn — стартовый контекст для Codex

Уточнение для нового EzConn: адрес подключения по умолчанию **192.168.1.66:2000**.
Указанный ниже 192.168.0.1 относится к настройкам оригинального EasyConn.

## Цель
Создать новый сервисный клиент для промышленных ПК Kaparossa, совместимый с функциями оригинального `EasyConn`, но с современной и расширяемой архитектурой.

Основное приложение должно быть **native C++20 + Qt 6 Widgets**, сборка через **CMake/MSVC**.

Оригинальный архив EasyConn использовать как эталон поведения и источник legacy-протокола. Не изменять оригинальные бинарники.

## Что известно об оригинале
Оригинал — WinForms/.NET 2.0 приложение.

Основные файлы:
- `EasyConn.exe` — UI и orchestration;
- `Lib/TeleAssistenza.dll` — legacy-протокол, пакетный формат, аутентификация/служебная логика;
- `Lib/Tool.dll` — вспомогательные функции;
- `Lib/MySql.Data.dll` — серверная часть, пока не нужна.

Для локального подключения по умолчанию используется:
- TCP: `192.168.0.1:2000`;
- также поддерживается COM/Serial.

Важные классы/точки оригинала, найденные в бинарниках:
- `Schnell.TeleAssistenza.Protocollo`
- `DriverConnessione`
- `DriverTrasferimenti`
- `Comandi_Comando_In`
- `UCParametri`
- `UCListaIO`
- `UCStreamVideo`

`Protocollo` содержит методы/поля вроде:
- `SetPacchetto`, `SetPacchettoDaRete`, `GetPacchettoInByte`
- `GetComando`, `GetDati`, `SetDati`
- `calcolaDimensione`, `calcolaLRC`
- packet type / size / sender / destination / command / message type / payload / LRC / version / compression / turbo.

Не угадывать бинарный формат и числовые ID команд — сначала восстановить их из DLL.

Известные команды включают:
`cmdVersioneProtocollo`, `cmdVersioneSoftware`, `cmdGetStatus`, `cmdInfoDisco`,
`cmdContenutoDirectory`, `cmdCambioDirectory`, `cmdCambioDirectory2`,
`cmdInitUpload`, `cmdRichiestaFile`, `cmdScaricaFile`, `cmdInvioDatiFile`,
`cmdLetturaParametri`, `cmdSettaParametro`,
`cmdLetturaListaIO`, `cmdRicezioneIO`, `cmdListaDrivesAzion`,
`cmdCatturaSchermata`, `cmdStreamVideo`,
`cmdStatisticheTask`, `cmdStopTask`, `cmdVisualizzaLog`,
`cmdTastieraVirtuale`, `cmdRebootMacchina`,
`cmdZipDir`, `cmdUnZipDir`, `cmdTestZip`.

## Функции, которые нужны
Оставляем в проекте:
1. локальное TCP-подключение;
2. COM/Serial;
3. параметры машины;
4. диагностика I/O;
5. диагностика приводов;
6. удалённый файловый менеджер;
7. backup / restore / update Kaparossa;
8. screenshot / screen stream;
9. удалённое управление (virtual keyboard, запуск, reboot, maintenance);
10. задачи/процессы, диски, системная информация;
11. логи и диагностика.

Пока **не реализовывать**:
- удалённое подключение через центральный сервер Schnell;
- fleet/machine management;
- chat;
- пользователи/роли/login для серверной инфраструктуры;
- центральный EasyConn server/MySQL;
- updater Windows-приложения EasyConn.

## Архитектура
GUI не должен зависеть от legacy DLL.

Предлагаемая структура:

```text
EasyConn (Qt Widgets)
   |
   +-- MachineSession
   |      |
   |      +-- ITransport
   |      |     +-- TcpTransport
   |      |     +-- SerialTransport
   |      |
   |      +-- IMachineProtocol
   |            +-- SchnellLegacyProtocol
   |            +-- EasyConnProtocolV2   (позже)
   |
   +-- services/
          FileService
          ParameterService
          IoService
          DriveService
          ScreenService
          SystemService
          BackupService
          LogService
```

Legacy .NET DLL изолировать от основного Qt-кода. Предпочтительный вариант для первого PoC:

```text
EasyConn.exe (C++/Qt)
       |
       | IPC / Named Pipe
       v
EasyConn.LegacyHost.exe
       |
       +-- TeleAssistenza.dll
       +-- Tool.dll
```

`LegacyHost` может быть C++/CLI (.NET Framework 4.8). Если подключение старых .NET 2.0 assembly через C++/CLI создаёт ненужные проблемы, допустим минимальный C#/.NET Framework host — но только как изолированный compatibility bridge; основной проект остаётся C++.

## Первый milestone
Не начинать с полного UI.

### 1. Reverse-engineering checkpoint
Сначала исследовать `TeleAssistenza.dll` и создать `docs/legacy_api.md`:
- public/internal классы и конструкторы;
- сигнатуры `Protocollo`;
- enum/числовые значения команд;
- точный layout бинарного пакета;
- алгоритм LRC;
- compression/turbo flags;
- handshake для локального TCP;
- какие части `DriverConnessione` находятся в `EasyConn.exe`, а какие в DLL.

Использовать reflection/decompiler. **Ничего не придумывать, если значение можно получить из оригинальной assembly.**

### 2. C++ skeleton
Создать CMake-проект:
- `EasyConn` — минимальный Qt Widgets GUI;
- `EasyConnCore` — library;
- `EasyConnProbe` — console utility;
- `tests`.

Реализовать:
- `ITransport`;
- `TcpTransport` через `QTcpSocket`;
- `SerialTransport` через `QSerialPort`;
- `MachineSession`;
- централизованный диагностический лог TX/RX в hex + timestamps.

### 3. Legacy compatibility PoC
Сделать минимальный `LegacyHost`, который:
- загружает оригинальный `TeleAssistenza.dll`;
- подтверждает доступ к `Schnell.TeleAssistenza.Protocollo`;
- умеет создать пакет через оригинальный код и вернуть полученные bytes;
- умеет разобрать переданный byte array и вернуть поля пакета.

Это нужно использовать как reference/oracle при переносе протокола на C++.

### 4. Первый реальный тест
`EasyConnProbe` должен уметь:

```text
EasyConnProbe --tcp 192.168.0.1:2000
```

и показать:
- connect/disconnect;
- raw TX/RX log;
- после восстановления handshake — protocol/software version или другую безопасную read-only команду.

На первом этапе **не отправлять reboot, write parameter, delete/update/restore и другие destructive commands**.

## Принципы
- сначала совместимость, потом красивый UI;
- protocol/transport/UI строго разделены;
- legacy DLL — временный reference backend, не фундамент всего приложения;
- все неизвестные детали протокола документировать;
- destructive operations должны иметь отдельную защиту/подтверждение;
- код проектировать так, чтобы позже добавить собственный `EasyConn Protocol v2` для нового QNX/Kaparossa agent.
