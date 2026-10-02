# NET-DESIGN — сеть BFME1 Mobile (черновик v0.1, 02.10.2026)

Владелец: «Сеть и сервер». Статус: проектирование (до Э5 код не пишем, расходов нет).
Пометки: **[не проверено]** — не подтверждено кодом/данными/устройством; **[оценка]** — расчёт с допущениями.
Пути: TSH = `src/GeneralsGameCode` (HEAD 28170001), MZ = `src/GeneralsZH-Android-Port` (HEAD 8ef2349a, depth 1).
Номера строк сняты 02.10.2026; ключевые (Connection.cpp, ConnectionManager.cpp, Network.cpp, NetworkDefs.h, Transport.h, NetworkMesh.cpp, NextGenTransport.cpp) перепроверены 02.10.2026 в 17:05 МСК. При изменении HEAD перепроверять.

## 0. Кратко
- Lockstep SAGE (ConnectionManager/FrameData/DisconnectManager) сохраняем; меняем транспорт, run-ahead, повторы, реконнект.
- Транспорт: GNS 1.6 (нативный ICE) по образцу MZ NextGenTransport, но сигналинг, STUN/TURN и релей — наши.
- Два пути с первого дня: P2P (GNS ICE) + релей (фаза 1 — TURN внутри ICE; фаза 2 — свой релей UDP/WebSocket с параллельной отправкой).
- Адаптивный run-ahead по p95 RTT/джиттеру/потерям в мс, гистерезис, ранняя отправка, избыточность K, адаптивный RTO.
- CRC ≈ раз в секунду, автоматический отчёт о рассинхроне на сервер и в корпус QA.
- Сессия по токену, не по IP; реконнект при Wi-Fi↔LTE и после фона Android.

## 1. Как устроено сейчас (ZH, TSH)

### 1.1 Lockstep, кадры, частота
- Логика 30 кадров/с: `LOGICFRAMES_PER_SECOND = WWSyncPerSecond = 30` — `Core/GameEngine/Include/Common/GameCommon.h:65–73`, `Core/Libraries/Source/WWVegas/WWLib/WWCommon.h:37`.
- Начальные runAhead `min(max(30, MIN_RUNAHEAD), MAX_FRAMES_AHEAD/2)` и frameRate 30 — `Core/GameEngine/Source/GameNetwork/Network.cpp:330–331`.
- Константы `Core/GameEngine/Include/GameNetwork/NetworkDefs.h`:
  - `MIN_LOGIC_FRAMES=5` (`:36`), `MAX_FRAMES_AHEAD=128` (`:37`), `MIN_RUNAHEAD=4` (`:38`; твик TSH 26.08.2025, «до 133 мс латентности»), `FRAME_DATA_LENGTH=(MAX_FRAMES_AHEAD+1)*2` (`:42`);
  - `MAX_SLOTS=MAX_PLAYER+1=8` (`:61`, `MAX_PLAYER=7` на `:48`), `MAX_UDP_PAYLOAD_SIZE=1100` (`:66`; учтён MTU мобильных сетей, в retail было 476), `MAX_MESSAGES=256` (`:83`), `NETWORK_BASE_PORT_NUMBER=8088` (`:228`);
  - `struct TransportMessageHeader{UInt crc; UShort magic}` (`:52`) = 6 байт, `GENERALS_MAGIC_NUMBER=0xF00D` (`:197`);
  - флаги MSG_ACK/NEEDACK/SEQUENCED/SUPERCEDING; типы NetCommand: ACK*, FRAMEINFO, GAMECOMMAND, RUNAHEADMETRICS, RUNAHEAD, KEEPALIVE, FRAMERESENDREQUEST, DISCONNECT*, PACKETROUTERQUERY и др.
- Каждый игрок на каждый кадр шлёт FRAMEINFO (число команд кадра). Кадр исполняется, когда от всех пришли FRAMEINFO и все команды. Если число команд не сошлось — FRAMERESENDREQUEST: `ConnectionManager.cpp:636` (processFrameResendRequest), `:2561` (requestFrameDataResend).
- Команды идут через packet router, то есть фактически топология «звезда»: `sendLocalCommand`, `ConnectionManager.cpp:1608–1652` (комментарий `:1606` «Commands sent in this way go through the packet router»; рассылка роутером `:1625`, через роутер `:1635–1636`). Прямая отправка — `sendLocalCommandDirect` (`:1658`); ACK идут напрямую (`:1200`).

### 1.2 Run-ahead (задержка ввода)
- `ConnectionManager::updateRunAhead`, `ConnectionManager.cpp:1350–1489`. Считает только packet router (`m_packetRouterSlot = 0` с @todo, `:297`; ветка роутера `:1356`, остальные шлют метрики роутеру `:1465`).
  - `newRunAhead = ceilf(getMaximumLatency() * (1 + slack/100) * minFps)` (`:1387–1388`), clamp `[MIN_RUNAHEAD, MAX_FRAMES_AHEAD/2 = 64]` (`:1392`).
  - minFps зажимается в `[MIN_LOGIC_FRAMES=5, FramesPerSecondLimit]` (`:1381`), гистерезис 10% по FPS (`:1375–1378`), самому медленному +10% FPS (минимум +1), но не выше 30 (`:1448–1456`).
- `getMaximumLatency` — среднее двух наибольших латентностей (`:1491–1511`).
- Латентность (FrameMetrics.cpp) — время от отправки кадра до ответа (по сути RTT), скользящее среднее 200 записей. FPS берётся из `TheDisplay->getAverageFPS()` раз в секунду, стартовая латентность 0,2 с.
- Новый RA рассылается командой RUNAHEAD; `Network::processRunAheadCommand` задаёт frameGrouping = (1000·RA/FPS)/2, в пределах 1–500 мс (`Network.cpp:644–657`).
- Cushion-метрики — `ConnectionManager.cpp:1274–1283`. В `Network::timeForNewFrame` (`Network.cpp:772–814`): если cushion < RA·slack%, кадр замедляется на 10%.
- Параметры INI: `GeneralsMD/Code/GameEngine/Source/Common/GlobalData.cpp:499–507` (парсинг), `:978–987` (умолчания: например LatencyHistory 200 на `:979`, RunAheadMetricsTime 500 на `:980`, DisconnectTime 5000 на `:984`), поля `Include/Common/GlobalData.h:493–501`. Умолчания TSH: FPSHistory 30, LatencyHistory 200, RunAheadMetricsTime 500, Cushion 10, Slack 10, KeepAlive 20 с, DisconnectTime 5000, PlayerTimeout 60000, DisconnectScreenNotify 15000.
- **BFME1** (`data\ini\gamedata.ini` из ini.big, только анализ): `UseFPSLimit=Yes`, `FramesPerSecondLimit=30` (строки 10/12); сетевой блок 538–549: те же ключи, но **RunAheadMetricsTime 5000** и **NetworkDisconnectTime 15000**.

### 1.3 Повторы и ACK
- `Core/GameEngine/Source/GameNetwork/Connection.cpp`: фиксированный `m_retryTime = 2000` мс (`:41`, в MZ тоже `:41`). Адаптивный вариант закомментирован (TSH `:362` `frameGrouping*4`, `:374` `m_averageLatency*1.5`; в MZ те же строки `:386`, `:398`). doSend повторяет неподтверждённое по истечении retryTime (`:263`), пакет не уходит раньше frameGrouping после прошлого (`:243`). Латентность считается по ACK (`:345–349`). `resendPendingCommands` — `ConnectionManager.cpp:1975`.
- Следствие: одна потерянная команда без ACK ждёт до 2 с. Это главный источник фризов на LTE с потерями (ср. RESEARCH 5: в Open-BFME-1 при RTT 150 мс и 3% потерь худший фриз упал с 3,7 до 0,8 с, задержка локальной команды с ~92 до ~0 мс; цифры только из цитаты RESEARCH).

### 1.4 Транспорт (оригинал)
- `Core/GameEngine/Source/GameNetwork/Transport.cpp`: «шифрование» — XOR с маской `0x0000Fade` и шагом +0x321 (функции `:34–60`, маска на `:41` и `:56`), CRC пакета + magic (`:408–417`, проверка magic `:442`), есть симуляция латентности. Криптографической защиты нет.

### 1.5 CRC и рассинхрон
- `GeneralsMD/Code/GameEngine/Source/GameLogic/System/GameLogic.cpp:3748–3776`: CRC `getCRC(CRC_RECALC)` каждые `TheGameInfo->getCRCInterval()` кадров в MP (`:3752`) или REPLAY_CRC_INTERVAL в реплее/соло (`:3755–3757`); результат идёт в поток как MSG_LOGIC_CRC. XferCRC/XferDeepCRC (дамп состояния) — с `:4134`.
- `Network.cpp:56–60`: `NET_CRC_INTERVAL = 100` (`:59`; `= 1` при DEBUG_CRC, `:57`), то есть 3,3 с при 30 Гц. `GameInfo.cpp:305`: m_crcInterval = NET_CRC_INTERVAL. `CommandLine.cpp:320/333` переопределяет интервалы.
- `Network::setSawCRCMismatch` (`Network.cpp:365–391`) показывает CRCMismatch.wnd и вызывает `TheRecorder->logCRCMismatch`.
- `GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp`: `REPLAY_CRC_INTERVAL=100` (`:57`; при воспроизведении берётся из реплея, `:1210`), CRCInfo (`:106–146`; первый CRC в MP по сети не приходит, `:122`), logCRCMismatch (`:209`), handleCRCMessage (`:988–1048`), кадр рассинхрона (`:1020`).

### 1.6 Дисконнект
- `Core/GameEngine/Source/GameNetwork/DisconnectManager.cpp`: экран после зависания дольше `m_networkDisconnectTime` (`:106`), таймаут игрока 60 с, при 1/3 оставшегося таймаута синхронизация кадров (`:212`), голосование за отключение (`processDisconnectVote` `:359`, до `:470`). `MaxQuitFlushTime` 30000 (Connection.cpp).

### 1.7 Реплей
- Реплей — журнал команд (Recorder). Размеры реальных ZH-реплеев в публичном TheSuperHackers/GeneralsReplays: 25 КБ – 1,29 МБ, типично 50–400 КБ (длительность матчей неизвестна).

## 2. Как MYSOREZ встроил GNS
- Сборка: `GeneralsMD/Code/GameEngine/CMakeLists.txt:1122–1210, 1261–1330`. Портирован GeneralsOnline (GeneralsOnlineDevelopmentTeam/SuperHackers_GO, ветка go_int, 32ae5135). P2P-транспорт NetworkMesh/NextGenTransport включается опцией `GENERALS_ONLINE_ENABLE_P2P_TRANSPORT` (по умолчанию `${ANDROID}`, `:1200–1206`; define на `:1329–1330`), плюс `add_compile_definitions(GENERALS_ONLINE)` (`:1305`). GNS 1.6.0 из vcpkg, нативный ICE без WebRTC. Клиент 30 или 60 Гц: `CMakeLists.txt:101/104` (GENERALS_ONLINE_HIGH_FPS_LIMIT), `NextGenMP_defines.h:70–83`.
- Порт vcpkg `cmake/vcpkg-ports/gamenetworkingsockets/`: vcpkg.json (1.6.0, feature ice, зависимости openssl и protobuf), portfile.cmake и три патча:
  - `android-os-check.patch` — для Android GNS собирается с `LINUX POSIX ANDROID` (bionic совместим с epoll/eventfd);
  - `android-getlocaladdresses.patch` — на Android не было GetLocalAddresses, поэтому не было ICE-кандидатов;
  - `android-ice-stun-spin.patch` — дубли STUN/TURN зацикливали thinker, зависание до 24,8 с.
- `Core/GameEngine/Include/GameNetwork/Transport.h:40–62` — Transport сделан виртуальным (комментарий `:46–52`, virtual dtor/init/reset/update/doRecv/doSend на `:54–62`). `ConnectionManager.cpp:1606–1629` (initTransport): при `TheLAN == nullptr` (`:1615`) создаётся NextGenTransport (`:1617`), иначе старый UDP.
- `GeneralsMD/Code/GameEngine/Source/GameNetwork/GeneralsOnline/NextGenTransport.cpp`: TransportMessage (6 байт + payload) заворачивается в сообщение GNS с 1 байтом канала (NETWORK_CHANNEL_GAME; античит идёт тем же соединением, `:111–135`). CRC и magic сохранены: формирование `:442–462`, проверка на приёме `:222–239`. Адрес = слот (`TheNGMPGame->getSlot(addr)`, `:345`), отправка через `NetworkMesh::SendGamePacket` (`:365`).
- `GeneralsMD/Code/GameEngine/Source/GameNetwork/GeneralsOnline/NetworkMesh.cpp`:
  - identity задаётся `SetGenericString(userID)` (`:639` локальная, `:911` удалённая) без сертификатов, соединения Unauthenticated (флаги логируются `:298–321`);
  - STUN по умолчанию `stun.playgenerals.online:53/:3478`, `stun.l.google.com:19302` (`:670`); TURN `turn.playgenerals.online:53/:3478` (`:696`) с логином и токеном из лобби; переопределяется подписанным GXRemoteConfig (`:665–714`); TURN только UDP;
  - `relay_all_traffic`/g_bForceRelay → ICE_Enable_Relay (`:721–723`), иначе All (`:727`);
  - Listen/Connect в SymmetricConnect (`:780`, `:938`), `ConnectP2PCustomSignaling` (`:955`);
  - CSignalingClient (`:339–460`): сигналы идут через WebSocket лобби GO (`WebSocket::SendData_Signalling`, `OnlineServices_Init.cpp:1052`; диапазон `:1042–1060`; NETWORK_SIGNAL=12, REQUEST_SIGNALLING=19, `OnlineServices_Init.h:50–80`);
  - обработчик состояний: при ClosedByPeer/ProblemDetectedLocally (`:72–73`) — `disconnectPlayer` (`:92`); повторный сигналинг до 3 попыток (`numSignallingAttempts = 3`, `:98–99`, только если retry_signalling), инициирует игрок с большим userID (`:124`). Другие вызовы `disconnectPlayer`: `:220`, `:252`, `:884` (ошибка отправки), `:990`, `:1035`. **Реконнекта посреди матча со сменой IP нет** (по коду; на устройстве [не проверено]);
  - `NetworkMesh::SendGamePacket` (`:859`, при ошибке отправки — disconnectPlayer `:884`) → `PlayerConnection::SendGamePacket` (`:1106–1135`): по умолчанию `Reliable|AutoRestartBrokenSession` (`:1108`), через `network_send_flags` можно Unreliable/UnreliableNoNagle/UnreliableNoDelay/Reliable/ReliableNoNagle (`:1117–1133`); пинг из GetConnectionRealTimeStatus (`:1300`, `:1318`).
- ServiceConfig с сервера (`OnlineServices_Init.h:264–300`): retry_signalling (по умолчанию false, `:266`), min_run_ahead_frames=4 (`:268`), ra_update_frequency_frames=10, relay_all_traffic (`:270`), ra_slack_percent=20, frame_grouping_frames=2, enable_host_migration, network_do_immediate_flush_per_frame, network_send_flags=-1 (`:276`), do_replay_upload=true (`:283`), ibra_* (slack по диапазонам RTT >200/>300 мс). Применение — `NGMPGame.cpp:411–435`. `NetworkDefs.h:37` (`extern Int MIN_RUNAHEAD`) и `NetworkUtil.cpp:34` (`Int MIN_RUNAHEAD = 4`): MIN_RUNAHEAD стал переменной. `Network.cpp:722–730` фиксирует m_frameRate на тик логики. SeedLatencyData объявлена (FrameMetrics.cpp:156), вызовов не нашёл [не проверено].
- Эндпоинты чужие: `api.playgenerals.online`, `api-ru.playgenerals.online` (`OnlineServices_Init.cpp:268–278`), WSS `api-ru.playgenerals.online/ws` (`:834`). **Их не используем.**
- Детерминизм (MZ `docs/DEV_BLOG/2026-09-DIARY.md`):
  - 20.09: 12 трансцендентных функций бит-в-бит на x86_64/aarch64 при `-ffp-contract=off`; рассинхроны были из-за расхождений кода с ПК (RNG в InstantDeathBehavior и др.);
  - сдвиг CRC на чекпоинт (m_skippedOne, isMultiplayer из m_originalGameMode);
  - NaN в REAL_TO_INT_FLOOR; lroundf 32/64 бит (UCRT против bionic);
  - sinf расходится на 1,236% углов (Trig.cpp);
  - экран «Replay check» в лаунчере.
- **Лицензия кода GeneralsOnline в MZ не проверена** → пока не копировать, писать свой код по образцу архитектуры.

## 3. Наш дизайн

### 3.0 Частота логики: 5 Гц против 30 Гц
- Движок ZH — 30 Гц (см. 1.1). Для BFME1 retail 5 Гц логики известны только из README Open-BFME-1 (031-earlysend) в пересказе RESEARCH — **[не проверено]**. В gamedata.ini есть только `FramesPerSecondLimit=30`, а `legolas.ini:204` («3 seconds (90 frames)») говорит о 30 кадрах клиента, но не логики.
- Предложение: логика 30 Гц (как ZH; длительности INI в мс переводятся в кадры). Тогда RA квантуется по 33 мс вместо 200 мс. Решение — за Лидом/Геймплеем. Алгоритм ниже параметризован `T_logic`.
- Если выберут 5 Гц: RA 1–2 кадра (200–400 мс) покроет RTT 150 мс, но минимальная задержка ввода ≥200 мс.

### 3.1 (а) Адаптивный run-ahead под LTE
Входные данные по каждому пиру и пути (раз в 250 мс): RTT (GNS `m_nPing` для P2P, собственный PING/PONG для релея), джиттер (`m_usecMaxJitter` или RFC 3550 mean deviation), потери (1 − `m_flConnectionQualityRemote` / счётчик seq), очередь (`m_cbPendingUnreliable`), локальный frame time (троттлинг Android).

Расчёт **[оценка, параметры подобрать в netem]**:
```
owd_p   = RTT_p95(p)/2                      # прямой путь; через релей — (RTT_A,relay + RTT_relay,B)/2
J_p     = p95 джиттера
R       = минимальное K, при котором loss^(K+1) < 1e-4   # 3% → K=2
D       = max_p(owd_p + 2·J_p) + K·T_pkt + T_send/2 + T_proc
RA_need = ceil(D / T_logic) + 1                          # +1 кадр запаса
```
Гистерезис:
- повышать сразу (в том же окне);
- понижать на 1 кадр, только если RA_need ≤ RA − 2 держится 5 с;
- не чаще одного понижения в 2 с;
- минимум RA в мс (≥ 66 мс), максимум 64 кадра.

Пример: RTT 150 мс, джиттер 20 мс, 3% потерь, 30 пакетов/с → D ≈ 75 + 40 + 66 + 17 ≈ 198 мс → RA = 7 кадров (233 мс). Формула TSH дала бы 5 кадров, но с фризами до 2 с на каждую потерю.

Отличия от TSH:
- RA считают все: каждый клиент отправляет свои метрики, решает packet router или сервер-релей;
- берём p95, а не среднее по 200 записям;
- для прямого пути закладываем одностороннюю задержку, а не полный RTT;
- кадр замедляется только при реальной нехватке cushion.

Ранняя отправка:
- GAMECOMMAND уходит немедленно, без ожидания frameGrouping (`Connection.cpp:243`); FRAMEINFO — раз в тик;
- `network_do_immediate_flush_per_frame` у MZ — аналог.

Избыточность и повторы:
- каждый пакет несёт неподтверждённые команды и FRAMEINFO последних K кадров (команды мелкие);
- RTO = SRTT + 4·RTTVAR в пределах [2·T_pkt, 1000 мс] вместо 2000 мс (`Connection.cpp:41`);
- до 5 повторов (урок Arena 30.08.2026: потерянная команда повторяется 5 раз, после более короткого ожидания).

На GNS — `k_nSteamNetworkingSend_UnreliableNoNagle`: надёжность обеспечивают ACK SAGE и K-избыточность. Reliable в GNS упорядочен в пределах lane, поэтому при потерях все пакеты за потерянным ждут его повтора. MZ по умолчанию шлёт `Reliable|AutoRestartBrokenSession` (`NetworkMesh.cpp:1108`).

### 3.2 (б) CRC и отчёт о рассинхроне
- Интервал в release `N=30` кадров (≈1 с), в QA-сборке `N=1`. Стоимость `getCRC(CRC_RECALC)` на телефоне надо измерить (QA) **[не проверено]**; если дорого — N=60.
- Обязательно перенести фиксы MZ: сдвиг первого чекпоинта (Recorder.cpp:122 / m_skippedOne).
- Кольцевой буфер последних 64 CRC + лёгкие под-CRC (RNG seed, число объектов, ресурсы игроков) для бисекции.
- При рассинхроне (`setSawCRCMismatch`, Network.cpp:365):
  1. продолжать или завершать матч — решает Лид;
  2. дамп `XferDeepCRC` на кадре рассинхрона (GameLogic.cpp:4134+);
  3. пакет отчёта: реплей, история CRC всех игроков, дамп (zstd), build/commit, хэш данных, ABI/SoC/ОС, параметры матча и seed, логи сети;
  4. выгрузка `POST /v1/matches/{id}/desync` (HTTPS, токен матча), сервер группирует отчёты всех участников;
  5. реплей попадает в корпус QA, CI прогоняет его на x86_64 и aarch64 («Replay check» по образцу MZ).
- Детерминизм ARM — внешняя зависимость (владельцы Лид/QA); сеть только детектирует и собирает данные.

### 3.3 (в) Реконнект со сменой IP
- Идентификация: `match_id + slot + reconnect_secret` (выдаёт сервер в MatchStart). Транспорт адресует по слоту, как MZ (адрес = слот, NextGenTransport.cpp:345).
- Android: ConnectivityManager.NetworkCallback (onAvailable/onLost/onCapabilitiesChanged) → JNI → транспорт (вопрос к Android-платформе).
- При смене сети:
  - путь релея: новый HELLO с токеном из нового сокета, релей переносит адрес после проверки AEAD (как миграция в QUIC); для WebSocket — переподключение;
  - путь P2P: закрыть соединение GNS, новый ICE через WS-сигналинг (до 3 попыток).
  - Поведение GNS при смене интерфейса не документировано **[не проверено]**, поэтому реконнект делаем на своём уровне.
- Lockstep при этом стоит; неподтверждённые команды хранятся и досылаются, FRAMERESENDREQUEST уже есть.
- Таймауты (мобильные): индикатор «плохая связь» через 1 с, экран дисконнекта через 5 с (TSH 5000, BFME1 15000), окно переподключения 60 с (PlayerTimeout), дальше голосование (`DisconnectManager.cpp:359–470`).
- Вместо мгновенного `disconnectPlayer` (MZ NetworkMesh.cpp:92 и др.) — состояние RECONNECTING.
- Фон: onPause → PAUSE_NOTICE пирам (экран «игрок свернул игру», таймер 60 с); onResume → реконнект.
- Гибель процесса (OOM в фоне) → повторный вход быстрым прогоном журнала команд с релея — **Э6** [не проверено, нужна оценка скорости прогона на телефоне].
- Урок Arena (10.08.2026): «reconnect can follow you if your IP changes mid-game». 08.05 исправлен stale slot при реконнекте, поэтому слот очищаем явно.

### 3.4 (г) P2P и релей с первого дня
| Вариант | Плюсы | Минусы | Решение |
|---|---|---|---|
| GNS SDR | готов в GNS | только для Steam-партнёров (README GNS) | нет |
| TURN (coturn) внутри ICE | почти без кода; MZ так и делает | только UDP; без TCP-fallback; своя аллокация на каждую пару; без TURN channels ≈+36 Б/пакет [расчёт по RFC 5766]; один путь (ICE выбирает одну пару) | фаза 1 |
| Свой релей UDP + WS | двойная отправка, TCP/WS-fallback, реконнект по токену, fan-out, запись журнала для реплея/результата | свой код (Go) | фаза 2 (Э5) |

- Сигналинг ICE даёт наш сервер (WS-хаб SERVER-ARCH): пересылает непрозрачные блобы `ISteamNetworkingConnectionSignaling` только между участниками матча. Нужен постоянный push-канал, доставка best-effort (README_P2P).
- Двойная отправка (Arena 03.05.2026: «always sending packets on both the P2P and relay channels»):
  - каждый пакет уходит по обоим путям, приёмник отбрасывает дубль по (slot, path-independent seq);
  - при N>4 шлём через релей с fan-out (одна копия вверх), иначе uplink mesh растёт как N−1.
- STUN — наш (coturn или pion/stun на VPS), Google STUN как запасной. Доступность из РФ [не проверено].

### 3.5 (д) Пакеты, версии, безопасность, трафик
- Внутри — формат SAGE (TransportMessageHeader 6 Б, NetPacket) без изменений на Э5.
- Обёртка нашего транспорта: `[ver:1][chan:1][seq:4]` + payload (P2P, внутри GNS).
- Обёртка релея (UDP/WS): `magic 'BR':2 | ver:1 | type:1 | session_id:8 | seq:4 | dst_mask:1 | payload | AEAD tag:16`.
- Версия: proto_ver + build hash + хэш игровых данных (BIG/INI) сверяются сервером при входе в лобби; несовпадение = отказ (иначе гарантированный рассинхрон).
- Шифрование:
  - GNS — AES-GCM-256 + Curve25519 (README GNS); identity = наш userId, сертификаты нашим CA — позже [не проверено];
  - до этого первое прикладное сообщение = HMAC(match_key, match_id|slot|nonce);
  - релей — AEAD с ключом матча (выдаётся по WSS, OpenSSL уже в зависимостях GNS); XOR из Transport.cpp:34–60 за защиту не считаем.
- Бюджет трафика **[оценка]**. Допущения: 30 пакетов/с на поток; payload ≈ 50 Б (FRAMEINFO×3 с избыточностью + ACK + ~2 команды/с по 40 Б); накладные IPv4/UDP 28 + GNS ≈30 (заголовок + тег GCM; точно [не проверено]) + 7 → ≈115 Б/пакет → **r ≈ 3,5 КБ/с ≈ 28 кбит/с на поток**.
  - 1v1, двойная отправка: ≈7 КБ/с вверх и вниз (56 кбит/с), за 20 мин ≈ 8,4 МБ в каждую сторону.
  - 4v4 mesh с двойной отправкой: до 49 КБ/с вверх (≈390 кбит/с), за 40 мин ≈ 118 МБ — много для мобильного тарифа, поэтому при N>4 — fan-out через релей: вверх ≈ 1,3·r + P2P только к packet router [оценка].
  - Цель: ≤64 кбит/с вверх в 1v1, ≤200 кбит/с в 4v4.

### 3.6 (е) План Э5 и готовность
**Итоговый критерий Э5 (PLAN.md:75):** «два телефона в разных сетях (Wi-Fi и LTE) играют 20 минут без рассинхрона и без зависаний дольше 1 с при RTT ≤150 мс, а реплей матча автоматически оказывается на сервере».

Промежуточные шаги и их критерии (предложение «Сети»):

| Шаг | Содержание | Готово, когда |
|---|---|---|
| N1 | BfmeTransport (подкласс Transport, адрес=слот) на GNS, свой WS-сигналинг, свой STUN/TURN | 2 клиента на боксе (Linux) и 2 телефона играют 20 мин без рассинхрона |
| N2 | Сервер MVP: инвайты, лобби, сигналинг, coturn | матч из лобби стартует без ручного ввода IP |
| N3 | Ранняя отправка, K-избыточность, адаптивный RTO и RA | профиль B: ни одного фриза >1 с, p95 задержки ввода ≤ RA+1 кадр |
| N4 | Свой релей UDP+WS, двойная отправка, дедуп | UDP заблокирован → матч идёт по WS; P2P обрывается → без фриза >1 с |
| N5 | CRC N=30, отчёт о рассинхроне, выгрузка реплея | искусственный рассинхрон → отчёт от всех игроков на сервере за ≤60 с; реплей каждого матча автоматически на сервере |
| N6 | Реконнект: смена IP, фон ≤60 с | Wi-Fi↔LTE посреди матча → продолжение ≤5 с без рассинхрона |
| N7 | Полевой тест: 2 телефона, Wi-Fi + LTE, затем 4 | выполнен итоговый критерий PLAN.md:75; 10 матчей без обрывов, телеметрия RA/фризов собрана |

Тестовая матрица (netem в network namespaces на боксе; на телефонах — реальная LTE):

| Профиль | Условия |
|---|---|
| A | LAN, 0 потерь |
| B | RTT 150 мс ±20 мс, потери 3% (базовый) |
| C | RTT 300 мс, 5% |
| D | пачки потерь (Gilbert-Elliott) |
| E | UDP заблокирован |
| F | смена IP/интерфейса посреди матча |
| G | обрыв на 30 с (фон) |
| H | симметричный NAT/CGNAT → P2P не устанавливается, работает релей |
| I | 4v4 со смешанными профилями |

Метрики: максимальный фриз, число фризов >1 с, RA (среднее и максимум), задержка от тапа до исполнения (p50/p95), рассинхроны, байты/с вверх и вниз, время реконнекта.

### 3.7 (ж) Открытые вопросы
**Лиду движка:**
1. Частота логики 30 или 5 Гц.
2. Можно ли перенести расчёт RA с packet router на всех или на релей.
3. После рассинхрона: стоп или продолжение.
4. Лицензия кода GeneralsOnline в MZ: переиспользуем или пишем своё.
5. Уходим ли от звезды packet router к mesh.
6. Нужен ли headless-билд для проверки реплеев на сервере.

**QA:**
1. Стоимость getCRC/XferDeepCRC на устройствах.
2. Профили netem.
3. Формат корпуса реплеев и CI на aarch64.
4. Расхождение лимитов CP 300/600 против 250/500 в ini.big.

**Android-платформе:**
1. NetworkCallback и JNI.
2. Поведение сокетов в фоне и в Doze.
3. Нужен ли foreground service на время матча.
4. Хранение токенов в Keystore.
5. Wake lock.
6. Выбор сети при одновременных Wi-Fi и LTE.

## 4. Источники
- Код: TSH и MZ (пути выше).
- GNS: https://github.com/ValveSoftware/GameNetworkingSockets (README, README_P2P.md, include/steam/steamnetworkingtypes.h).
- Arena: https://www.bfmeladder.com/changelog ; github.com/MarcellVokk/BfmeFoundationProject (OnlineKit, только чтение; сетевых внутренностей там нет).
- RESEARCH.md §4.1, §5; PLAN.md:75 (критерий Э5).
- Реплеи ZH: https://github.com/TheSuperHackers/GeneralsReplays.
