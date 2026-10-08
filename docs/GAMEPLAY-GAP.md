# GAMEPLAY-GAP: геймплей BFME1 против движка ZH и OpenSAGE

- **Автор:** Геймплей (39b6b49c-96d8-486c-b35d-e6eecb02f28e), 02.10.2026, 17:45 МСК; обновлено 02.10.2026, 18:00 МСК: решения Лида движка, Оркестратора и Форматов (§2.2–2.4), закрытые вопросы (§6), два потока Э2/Э3 (§7); **r3** 02.10.2026, ~22:00 МСК: все цифры пересчитаны по datacheck Форматов со слоем 2.22 (`tools/datacheck/out-all/`), новые решения Лида (§2.2 п. 5–10), ответы Форматов по механикам (§2.4), порядок, риски и вопросы (§3, §5, §6), потоки (§7). Draw-модули вынесены в отдельную таблицу для Лида: `docs/bfme-draw-modules.md`. **Вечер 02.10.2026:** решения Лида по `BeginScript`, `ClientBehavior`, Draw и счёту 124 (§2.2 п. 6, п. 11–13) и Оркестратора по командным очкам, доходу ферм и потоку C (§2.3, §7). После `c70bfa1`: обе CRC только через `crc()`, флага `isLogicCRCDump` нет; в данные 2.22 добавлены MP4 200/400 и горный тролль 15 CP. После бэклога v3.5 (`32cd01aa4`): E1-56 — 5948 блоков `BeginScript` и загрузка INI, связь E1-36 → E1-56 остаётся; `PRODUCTION 1.2 %` — гипотеза ×1,2. Черновик для Оркестратора и Лида движка.
- **Связано:** PLAN.md §4 (Э2, Э3), adr/ADR-0001-base-engine.md (наш код в `engine/bfme/`, правки апстрима только с пометкой `// BFME:`), adr/ADR-0002-target-devices.md §7 (логика 30 Гц, только arm64), adr/ADR-0003-determinism.md (правила детерминизма, «Предложено»), NET-DESIGN.md §1.5, §2, §3.2 (CRC, детерминизм), E1-BACKLOG.md (E1-34, E1-36, E1-37, E1-45, E1-49, E1-50), fpcr-audit.md (R1, R3, R6), formats/BFME1_vs_ZH.md (скрипты карт), bfme-draw-modules.md (источник истины реестра Draw Лида), UPSTREAM-PATCHES.md (реестр правок апстрима, файл есть в `bfme`).
- **Источники (проверено rg/grep на боксе):**
  - ZH: `src/GeneralsGameCode` (TheSuperHackers, коммит `2817000`, 02.10.2026). Пути ниже относительно этого каталога. Базовый форк `src/GeneralsZH-Android-Port` (MYSOREZ, `8ef2349`) использован для флагов сборки и `cmake/gamemath.cmake` (готовой GameMath в нём нет, см. §4.1 и ADR-0003).
  - OpenSAGE: `src/OpenSAGE/src/OpenSage.Game` (master `588ac477`, 16.12.2025). Пути ниже относительно `OpenSage.Game/`.
  - LibreSAGE: рабочее дерево на боксе пустое (в индексе staged-удаления, его не трогал). По `git ls-tree HEAD` (4063 файла) кода под BFME нет: только общие модули Generals/ZH и обложки `Common/Tools/Launcher/assets/covers/bfme1.jpg`, `bfme2.jpg`. Для геймплея BFME1 **пользы нет**.
  - Данные: datacheck Форматов со слоем 2.22 — `tools/datacheck/out-all/` (`report.md`, `report.json`, `gameplay.md`, `patch222_diff.md`); база без патча — `tools/datacheck/out/`. Смонтированы по порядку `_patch222.big`, `_patch222bases.big`, `_patch222libraries.big`, `ini.big`, `maps.big` (`out-all/report.md:11–17`). Числа ниже взяты из `out-all`, если не сказано иначе; ссылки `out-all/…` означают `tools/datacheck/out-all/…`. Точечные проверки строк INI я делал скриптом в памяти (`/workspace/gp-scratch/vread.py` поверх `tools/datacheck/datacheck/vfs.py`), на диск ничего не распаковывал. Ссылки вида `файл.ini:строка` указывают путь внутри смонтированных `.big` (`data\ini\…`) с учётом перекрытия патчем. `_patch222maps.big` пришёл на бокс уже после прогона datacheck, поэтому частот по картам 1v1 2.22 пока нет (§5 №4).
- **Чем не является этот документ:** это не отчёт datacheck. Отчёт ведут Форматы (`out-all/`), здесь из него взяты числа и выводы для геймплея. Мой ранний подсчёт по базовому `ini.big` (`/workspace/gp-scratch/bigcount.py`, `freq.tsv`) заменён им; его ошибки перечислены в §0.

Оценка объёма: **S** ≤ 2 чел.-дня, **M** 3–7, **L** 8–15, **XL** > 15 (чел.-день = день работы одного агента с ревью). Это **[оценка]**, сделанная без прототипа.

---

## 0. Сводка по данным (datacheck out-all, слой 2.22)

- **Объекты:** 3158 определений `Object/ChildObject/ObjectReskin`, после наследования 2843 итоговых объекта (`out-all/report.md:171`; в базе без патча 3000 и 2686).
- **Модули объектов:** в объявлениях 216 имён и 17 944 объявления, включая 24 модуля частиц. У итоговых объектов 192 имени (`report.json`: `ini.modules_declared`, `ini.modules_final_after_inheritance`; таблица — `out-all/report.md:169–194`).
- **Есть в ZH: 68 модулей** (5425 объявлений). **Писать: 124 модуля** (5355 объявлений): это список `bfme_vs_zh.not_in_generals_zh_cpp` (`out-all/report.md:200`). Я сверил его с `addModule` в ZH (223 имени), расхождений нет. Источник — datacheck out-all со слоем 2.22, поэтому пометка «предварительно» снята. Было 67 и 123 (мой подсчёт по базе), стало 68 и 124.
  - **Счёт (решение Лида, вечер 02.10.2026, §2.2 п. 13):** 124 = **113** (108 Behavior + 5 Body, реестр Геймплея) + **11 Draw** (реестр Лида).
  - **4 заглушки `ClientBehavior`** идут сверх 124, в том же `BfmeModuleRegistry` (§2.2 п. 12). Критерий E1-50 их тоже учитывает.
  - **11 Draw** — 7 новых и 4 расширенных модуля ZH (`W3DTruckDraw`, `W3DTankDraw`, `W3DTreeDraw`, `W3DLaserDraw`). Источник истины реестра Лида — `docs/bfme-draw-modules.md` (§2.2 п. 6).
  - Отдельно **24 модуля частиц** (`Draw/Update/Physics/Volume/Velocity` внутри `FXParticleSystem`, 7164 объявления). Это не модули объектов и не входят в 124. В Э1 их пропускает Лид (`bfme-draw-modules.md` §5, §2.2 п. 6).
- **Самые частые новые модули** (объявления): `W3DScriptedModelDraw` 2483 (у 2490 итоговых объектов), `AttributeModifierAuraUpdate` 247, `SpecialPowerModule` 204, `CastleMemberBehavior` 196, `AnimationSoundClientBehavior` 165, `LargeGroupAudioUpdate` 162, `GettingBuiltBehavior` 129, `HordeContain` 84, `CastleBehavior` 81.
- **У всех 2843 объектов** через `DefaultThingTemplate` стоят 4 новых модуля (`out-all/report.md:175–178`; `data\ini\default\object.ini:281`, `:298`, `:306`, `:327`): `AttributeModifierPoolUpdate`, `ShareExperienceBehavior`, `TemporarilyDefectUpdate` (по 1 объявлению) и `AttributeModifierUpgrade` (55 объявлений, 17 144 экземпляра). Их заглушки нужны первыми, а их логика будет работать в тике каждого объекта.
- **Блоки верхнего уровня BFME1, которых нет в ZH** (`out-all/report.md:38–136`, ZH `Core/GameEngine/Source/Common/INI/INI.cpp`, `theTypeTable`): `ExperienceLevel` 648, `ModifierList` 359, `ChildObject` 331, `LargeGroupAudioMap` 117, `EmotionNugget` 15, `PlayerAIType` 15, `FactionVictoryData` 14, `LivingWorld*` (кампания), `FXParticleSystem` 1069 (у Лида). Есть в ZH, но с новыми полями BFME: `Object` 2181, `ObjectReskin` 646, `Weapon` 609, `Upgrade` 182, `SpecialPower` 146, `Rank` 85, `Locomotor` 82, `Science` 49, `CrateData` 9, `PlayerTemplate` 7.
- **Оружие BFME1 построено на «нагетах»** (в ZH такого нет вообще; поля `WeaponTemplate`): `DamageNugget` 554, `ProjectileNugget` 168, `MetaImpactNugget` 126, `HordeAttackNugget` 12, `GrabNugget` 6, `AttributeModifierNugget` 5, `SpawnAndFadeNugget` 3, `SpecialModelConditionNugget` 3, `DamageFieldNugget` 1. Поля оружия: `MeleeWeapon` 203, `HitStoredTarget` 95, `CanFireWhileMoving` 24, `FinishAttackOnceStarted` 11.
- **Поля:** объекта — `CommandPoints` 308, `EmotionRange` 86, `CrushRevengeWeapon` 42; локомотора — `FormationPriority` 44, `ChargeAvailable` 8, `WaitForFormation` 2; `ProductionUpdate.GiveNoXP` 56; `PlayerTemplate` — `BuildableHeroesMP` 4, `SpellBook` 4.
- **Ошибки моего раннего подсчёта** (`/workspace/gp-scratch/bigcount.py`; разбор Форматов — `out-all/gameplay.md:172–308`):
  - регулярка не ловила модули частиц: их 24, а не «7 Draw частиц» (имён тогда было 6, а не 7);
  - не учитывались строки `Behavior X Tag` без `=` (`out-all/gameplay.md:239`, `:280`, `:292`) и `Draw ==` (`out-all/gameplay.md:288`, пример `civilianprop.ini:863`);
  - пропущен `ModelConditionUpgrade` (15 объявлений, модуль ZH), отсюда 67 → 68 модулей ZH;
  - 40 «лишних» Object в базе (42 со слоем 2.22) были вложенными блоками, а не определениями объектов (`out/gameplay.md:205`, `out-all/gameplay.md:308`, пример `livingworld.ini:142`);
  - 11 меток «НЕТ_В_OS» ошибочны (`out-all/gameplay.md:294–306`). В OpenSAGE есть частицы `DefaultDraw`, `RenderObjectDraw`, `StreakDraw`, `ButterflyDraw`, `LightningDraw`, `QuadDraw` и Body `RespawnBody`, `DelayedDeathBody`, `PorcupineFormationBodyModule`, `SymbioticStructuresBody`, `DetachableRiderBody`;
  - не учитывались наследование и слой патча, поэтому почти все частоты в §1 выросли.
- Сверка с фабриками ZH: `GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp` (`ModuleFactory::init`, 204 `addModule`) + `Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp` (Draw/ClientUpdate). **Важно:** `ModuleFactory::findModuleTemplate` на неизвестном имени делает `DEBUG_CRASH("Module name '%s' not found")` и возвращает `nullptr`. Значит, с первого дня нужна регистрация заглушек для всех 113 логических имён и 4 `ClientBehavior` (реестр Геймплея) и 11 Draw (реестр Лида), иначе объекты BFME1 не загрузятся. **Решено** (§2.2 п. 1, п. 6, п. 12): логический реестр и заглушки client update в `engine/bfme/`, Draw — у Лида; таблица имён генерируется из datacheck.
- Сверка с OpenSAGE: таблица разбора `Logic/Object/BehaviorModule.cs` (`BehaviorParseTable`, 335 имён) + `Logic/Object/Body/BodyModule.cs` + Draw/ClientUpdate. Атрибут `[AddedIn(SageGame.Bfme)]` стоит у 97 модулей без аналога в ZH, но **поведение (класс с `CreateModule` и `Update`) есть только у ~12 из них**, остальное только разбор полей. Атрибуты AddedIn в OpenSAGE местами неточны (например, `MetaImpactNugget` помечен Bfme2, но 63 раза встречается в INI BFME1).

---

## 1. Таблица пробелов по системам

Статусы: **ZH** — есть в ZH, берём как есть (иногда с новыми полями); **≈ZH** — похожее есть, переиспользуем; **0** — пишем с нуля. Число в скобках — объявления модуля в INI со слоем 2.22 (datacheck `out-all`, `report.json` `ini.modules_declared`); где важно, указано и число итоговых объектов.

### 1.1 Ядро, без которого BFME1-объект не живёт

| # | Система / модули BFME1 (частота) | Статус | ZH: где | OpenSAGE: где и насколько готово | Объём | Риски |
|---|---|---|---|---|---|---|
| 1 | **Заглушки и регистрация модулей BFME1**: 113 логических имён (108 Behavior + 5 Body, §0) и 4 `ClientBehavior` сверх 124 в `BfmeModuleRegistry`; 11 Draw — в реестре Лида (§2.2 п. 6, п. 12, п. 13) | 0 | `ModuleFactory::init`, `findModuleTemplate` (DEBUG_CRASH на неизвестном), `ModuleFactory::newModuleDataFromINI`, `addModuleInternal` (`ModuleFactory.cpp:689`, молча перезаписывает) | имена и поля: `Logic/Object/BehaviorModule.cs`, `Body/BodyModule.cs` | M (5–6 дн.: E1-36 3–4 + E1-50 и генератор таблицы из datacheck, флаг `BFME_SOFT_MODULES`, проверка дублей) | **Решено** (§2.2 п. 1, п. 5, п. 12): явный реестр логики и 4 заглушек `ClientBehavior`, `BfmeModuleRegistry::registerAll(ModuleFactory&)` в конце `ModuleFactory::init()`, падение на повторном имени во всех сборках, замена модуля ZH только через `overrides`; схемы полей у Форматов, поведение у меня. В CI неизвестное имя — ошибка. Первыми — 4 модуля из `DefaultThingTemplate` (§0). Draw в этот реестр не входят |
| 2 | **AttributeModifier / ModifierList** (359 блоков; `AttributeModifierAuraUpdate` 247, `AttributeModifierUpgrade` 55 (у всех 2843 объектов, 17 144 экземпляра), `AttributeModifierNugget` 5, `AttributeModifierPoolUpdate` 1 (у всех 2843 объектов), а также `ExperienceLevel.AttributeModifiers`, `HordeContain.AttributeModifiers`). На этом держатся лидерство героев, бонусы орд, баннеры, ауры, бафы сил и рост дохода ферм по уровню (`Modifier = PRODUCTION`, №13) | ≈ZH | только бонусы оружия: `WeaponBonusUpdate.cpp`, `WeaponBonusUpgrade.cpp`, `WeaponBonusSet` в `Weapon.cpp`; бонус «орды» ZH: `HordeUpdate.cpp` | `Logic/ModifierList.cs` (разбор: категории, типы модификаторов, длительность), модули только данные | **L** (8–12 дн.) | ключевой системный слой: множители урона, брони, скорости, обзора, опыта, производства. Порядок применения и округление влияют на CRC (§4). `PRODUCTION 1.2 %` — множитель ×1,2, не +1,2 %: гипотеза высокой уверенности по данным Форматов (`report.md` §9), подтвердить замером (§5 №10, §6 Форматам №10). ZH `parsePercentToReal` дал бы 0,012. **Решено** (§2.2 п. 4): владелец — Геймплей, логика в `engine/bfme/`, хуки `// BFME:` в `Weapon.cpp`/`Damage.cpp` на ревью Лида |
| 3 | **Оружие на нагетах**: `DamageNugget`, `ProjectileNugget`, `MetaImpactNugget` (отбрасывание), `HordeAttackNugget`; поля `MeleeWeapon`, `HitStoredTarget`, `CanFireWhileMoving`, `FinishAttackOnceStarted` и др. (31 поле BFME в `WeaponTemplate.cs`) | ≈ZH | `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Weapon.cpp`, `WeaponSet.cpp`, `Core/GameEngine/Source/GameLogic/System/Damage.cpp`, `Object/Armor.cpp`, снаряды `DumbProjectileBehavior` | `Logic/Object/Weapon/WeaponEffects/*.cs` (678 строк): `Execute` только у `DamageNugget`, `ProjectileNugget`; `MetaImpactNugget` только разбор | **L** (10–15 дн.) | ближний бой, отбрасывание (физика, float), кривая `BezierProjectileBehavior` (118). Без этого нет боя Э2. **Решено** (§2.2 п. 4): владелец — Геймплей, минимальные хуки `// BFME` |
| 4 | **Опыт и уровни** (`ExperienceLevel` 648 блоков: до 10 уровней у героев, 5 у юнитов, `RequiredExperience`, `ExperienceAward`, `Upgrades`, `AttributeModifiers`, `LevelUpFx`; `LevelUpUpgrade` 50, `ExperienceLevelCreate` 13, `ShareExperienceBehavior` 1 (у всех 2843 объектов); здания тоже растут в уровне: ферма Гондора — `ExperienceLevelCreate LevelToGrant = 2`, `structures\gondor\farm.ini:116–118`; `ProductionUpdate.GiveNoXP` 56) | ≈ZH | `GameLogic/Object/ExperienceTracker.cpp`; уровни жёстко заданы enum `VeterancyLevel` (REGULAR..HEROIC, 4 шт.) в `Core/GameEngine/Include/Common/GameCommon.h`; `VeterancyGainCreate.cpp`, `ExperienceScalarUpgrade.cpp` | `Logic/ExperienceLevel.cs` (разбор), `Logic/Object/ExperienceTracker.cs`, `Create/ExperienceLevelCreate.cs` (15 строк) | M (5–7 дн.) | **Решено** (§2.2 п. 2): enum `VeterancyLevel` не трогаем; отдельный счётчик BFME (опыт и уровень героя 1–10, `xfer` и CRC), `VeterancyLevel` не трогаем, одностороннее отображение в него для старых эффектов ZH (§2.2 п. 9) |
| 5 | **Локомоторы BFME** (22 новых поля: `FormationPriority` 44, `ChargeAvailable` 8, `ChargeSpeed`, `BackingUpSpeed`, `SlowTurnRadius`/`FastTurnRadius`, `WaitForFormation` 2 и др.) | ≈ZH | `GameLogic/Object/Locomotor.cpp` (244 упоминания `Real`) | `Logic/Object/LocomotorTemplate.cs` (только разбор) | M (4–6 дн.) | движение строем и разворот орды зависят от этих полей |
| 6 | **Командные очки (CommandPoints)**: поле объекта (308), `CommandPointBonus` (здания поднимают лимит), лимиты в `gamedata.ini` из `_patch222.big`: сверено с `tools/datacheck/out-all/report.md` §4 (строки отчёта 291–300). SOLO `GoodCommandPoints`/`EvilCommandPoints` = 130/240 (`gamedata.ini:294–295`); ИИ `GoodCommandPointsAI`/`EvilCommandPointsAI` = 900/1500 (`:301–302`); MP2 = 230/460 (`:306–307`); MP3 = 200/400 (`:309–310`); MP4 = 200/400 (`:312–313`); MP56 = 160/320 (`:315–316`); MP78 = 150/300 (`:318–319`); бонусы Living World 20/50 (`:297–298`); `GoodCommandPointLimit`/`EvilCommandPointLimit` = 600/1400 — бонусы кампании, комментарий `CAMPAIGN BONUS CAPS` на `:281`, значения на `:282–283`. Горный тролль в 2.22 стоит 15 CP (в 1.03 было 10) | 0 | лимита юнитов в ZH нет; ближе всего проверки в `Common/System/BuildAssistant.cpp`, `Common/RTS/ProductionPrerequisite.cpp`, `MaxSimultaneousOfType` в `Core/GameEngine/Source/Common/Thing/ThingTemplate.cpp`, а схема «здание даёт ресурс игроку» как у энергии `Common/RTS/Energy.cpp` | поля `CommandPoints`, `CommandPointBonus` в `Logic/Object/ObjectDefinition.cs`; **логики лимита в Player.cs нет** | S–M (2–3 дн.) | эталон 1v1 — сетевые MP2 230/460 (`gamedata.ini:306–307`, §2.3). Окончательно закрепим, когда Форматы прогонят полный дифф 2.22 и покажут, откуда движок берёт лимит по числу игроков. Для одиночной игры против ИИ в данных 130/240. Datacheck подписывает 600/1400 как «лимит» (`out-all/report.md:256–263`), хотя это бонусы кампании (§6 Форматам №11) |
| 7 | **Объекты орды vs члены** (выделение, команды, урон), коллизии `HordeMemberCollide` (42) | 0 | — | выделение орды: `Logic/Player.cs` (`ParentHorde`) | входит в №8 | |

### 1.2 Орды и построения

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 8 | **HordeContain** (84): `RankInfo` (позиции рядов), `InitialPayload`, `RanksToReleaseWhenAttacking`, `ComboHorde`, `SplitHorde`, `BannerCarrier*`, `UseSlowHordeMovement`, `MeleeAttackLeashDistance`, `BackUp*`; плюс `HordeAIUpdate` (71), `HorseHordeContain` (19), `SlaughterHordeContain` (10), `HordeGarrisonContain` (9), `AODHordeContain` (4), `HordeTransportContain` (4), `HordeSiegeEngineContain` (1), `HordeWorkerAIUpdate` (5); рендер `W3DHordeModelDraw` (59) — у Лида (`bfme-draw-modules.md`) | ≈ZH | ближайший аналог — «толпа» GLA: `GameLogic/Object/Contain/MobNexusContain.cpp` + `Object/Update/MobMemberSlavedUpdate.cpp` + `Object/Behavior/SpawnBehavior.cpp` (члены держатся у «нексуса»); групповое движение `GameLogic/AI/AIGroup.cpp`; контейнеры `Object/Contain/OpenContain.cpp`, `TransportContain.cpp` | `Logic/Object/Contain/HordeContain.cs` (444 строки): разбор полный; поведение базовое: смещения формации из `RankInfo`, `Unpack` (создание членов), `SetTargetPoints` (каждому члену своя точка с поворотом), регистрация из `ProductionUpdate`. `Update()` пуст (`TODO(Port)`). `HordeAIUpdate` и прочие только разбор | **XL** (20–30 дн.: базовая версия Э2 ≈ 10–12, бой рядами, отступление, комбо/сплит, баннер, конница ≈ 10–15) | главный риск геймплея: pathfinding ZH ведёт отдельные юниты, а орда BFME должна двигаться как один объект с членами-«хвостами». Нагрузка на CPU (Mali, см. ADR-0002): 15–30 членов на батальон. Вид и плавность (`W3DHordeModelDraw`) делает Лид, в потоки A и B они не входят (§7). Переключение построения `HORDE_TOGGLE_FORMATION` — №31. **Решено** (§2.2 п. 3): прототип на настоящих объектах, решение по замеру (тик логики ≤ 10 мс p95 на Pixel 8 Pro, 4 игрока); не влезаем — виртуальные члены |
| 9 | **Бонусы орды / большой группы** (`LargeGroupBonusUpdate` 4, `LargeGroupAudioUpdate` 162 — только звук, `RousingSpeechUpdate` 3, ауры героев через №2) | ≈ZH | `HordeUpdate.cpp` (`HordeUpdateModuleData`: `m_kindof`, `m_minCount`, `m_minDist`, `m_rubOffRadius`, флаг; бонус через WeaponBonus) | только разбор | S (2 дн.) поверх №2 | `LargeGroupAudioUpdate` отдать клиенту (звук, не логика) |
| 10 | **Формации (FormationAssistant)** | 0 | — | блок `FormationAssistant` помечен Bfme2 | — | в BFME1 не используется: блока нет и со слоем 2.22 (`out-all/report.md:38–136`) |

### 1.3 Экономика, застройка, крепости

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 11 | **Крепость/лагерь** `CastleBehavior` (81): распаковка лагеря из файла `bases\<Camp>\<Camp>.bse` (формат map с `CastleTemplates`), захват нейтрального лагеря по `ScanDistance`, `CastleToUnpackForFaction`, `FilterValidOwnedEntries`, `PreBuiltList`; `CastleMemberBehavior` (196; в графе скирмиша у 22 объектов Гондора и 16 Мордора), `CastleUpgrade` (3), `KeepObjectDie` (ZH, 157) | 0 | ближайшее: заготовки базы ИИ/скриптов (`AIPlayer.cpp` build lists), `RebuildHoleBehavior.cpp` | `Logic/Object/Behaviors/CastleBehavior.cs`: рабочая распаковка `.bse` и смена владельца по ближайшему юниту (с `TODO`: не проверяются все соседи) | **L** (8–12 дн.) | `.bse` есть только в `_patch222bases.big`; в графе скирмиша 13–16 раскладок `camp_*`/`castle_*` на фракцию (`out-all/gameplay.md:150`, `:156`, `:162`, `:168`). Разбор `.bse` у Форматов (DataChunk, как `.map`, вместе с E1-25); поведение у потока B. Стартовые лагеря 1v1 появляются именно отсюда: без этого нет старта матча |
| 12 | **Слоты застройки и продажа построек** (KindOf `BASE_FOUNDATION`, `BASE_SITE`, `NEED_BASE_FOUNDATION`; `FoundationAIUpdate` 17, `GettingBuiltBehavior` 129 — здание строится само, без рабочего; `BuildingBehavior` 71; радиальное меню через CommandSet). Продажа: кнопка `Command_Sell` (`commandbutton.ini:2363`, `Command = SELL`) у 55 построек, `SellPercentage = 50%` (`gamedata.ini:484`); **продажи юнитов нет** ни в базе, ни в 2.22, полей `Sell*` вне GameData нет (`out-all/gameplay.md:7–31`) | ≈ZH | постановка здания: `Common/System/BuildAssistant.cpp`; строительство рабочим `Object/Update/AIUpdate/DozerAIUpdate.cpp`, `WorkerAIUpdate`; очереди `ProductionUpdate.cpp`; продажа ZH: `GUI_COMMAND_SELL` (`Core/GameEngine/Include/GameClient/ControlBar.h:185`), `BuildAssistant::sellObject` (`GeneralsMD/Code/GameEngine/Source/Common/System/BuildAssistant.cpp:1504`) | `Update/AIUpdate/FoundationAIUpdate.cs` (прячет слот, если на нём стоит здание; 49 строк), `ObjectKinds.cs`; `GettingBuiltBehavior` только разбор | M–L (6–9 дн.) + S (1–2 дн.) на продажу | заменить «бульдозер ставит здание» на «слот превращается в здание»; при продаже слот должен вернуться (проверить на ПК Андрея вместе с замером дохода) |
| 13 | **Ресурсы: фермы, лесопилки, печи** (`AutoDepositUpdate` 21 с полями BFME `Upgrade`, `UpgradeBonusPercent`, `UpgradeMustBePresent`; сборщиков нет). Ферма Гондора в мультиплеере (`GondorFarmMultiplayer`, `structures\gondor\farm.ini:102`): **17 каждые 5400 мс** (`farm.ini:134–135` → `#define` в `gamedata.ini:1434`, `:1436`), бонус апгрейда `Upgrade_MarketplaceUpgradeGrandHarvest` (`farm.ini:138–139`). Рост по уровню здания: `ExperienceLevel` фермы (`experiencelevels.ini:5913`, `:5917`, `:5947`, `:5957`) → `ModifierList GondorFarmLevel2Production`/`Level3Production` с `Modifier = PRODUCTION` (`attributemodifier.ini:1162`, `:1167`; макросы 1.2 и 1.23 — `gamedata.ini:1456–1457`). По данным Форматов (`report.md` §9) это множители ×1,2 и ×1,23, не +1,2 %; гипотеза высокой уверенности, подтвердить замером. Кузница уровня 3 тоже ×1,23 (`attributemodifier.ini:1179`, `gamedata.ini:1481`). ZH `parsePercentToReal` (`INI.cpp:1021–1026`) делит на 100 в `scanPercentToReal` (`INI.cpp:1887`); вместе с OpenSAGE это дало бы 0,012 | ZH (+поля) | `GameLogic/Object/Update/AutoDepositUpdate.cpp` (`DepositTiming`, `DepositAmount`, `InitialCaptureBonus`, `ActualMoney`, `UpgradedBoost`); деньги `Common/RTS/Money.cpp` | `Update/AutoDepositUpdate.cs`: доход = `DepositAmount` × `ProductionModifier`, без соседей; плюс опыт за выплату (`AddExperiencePoints`) | S–M (3 дн.); +1–3 только если замер найдёт зависимость от соседей | **Ни один источник не подтверждает падение дохода от соседних построек в BFME1** (`ResourceModifier*` в данных 0, `out-all/gameplay.md:89`). Радиусы `TerrainResourceBehavior` и таблица `ResourceModifierValues` относятся к BFME2. Замер (`docs/farm-income-measurement.md`) сначала проверяет, есть ли зависимость вообще. До ответа B8 считает доход без соседей. **Риск понижен** (§5 №5) |
| 14 | **Найм и пополнение батальонов** (`ProductionUpdate` 122, `QueueProductionExitUpdate` 57; найм орды по `HordeContain.InitialPayload`; `ProductionSpeedBonus` 1). Пополнение (`out-all/gameplay.md:55–84`): знаменосцы `BannerCarrierUpdate` (10; `IdleSpawnRate`, `MeleeFreeUnitSpawnTime`, `DiedRespawnTime`; `mordor\banner.ini:764`, `isengard\bannerorc.ini:789`), колодцы с `ReplenishNearbyHorde` (`gondor\well.ini:198`, `rohan\well.ini:206`, `ScanHordeDistance = 200`), сила лечения `HealSpellHordeReplenishPing` (`system.ini:1094`, `ReplenishAllNearbyHordes` — `:1119–1121`). `ReplenishUnitsBehavior` (5) — **не пополнение у здания**, а посадка на лошадей без седока (`ReplenishStatii = RIDERLESS`, `gondor\knight.ini:71`, `:75`), см. №21 | ZH (+) | `ProductionUpdate.cpp`, `QueueProductionExitUpdate`, `DefaultProductionExitUpdate`, `SpawnPointProductionExitUpdate`; аналог возрождения членов — `SpawnBehavior.cpp` | `Update/ProductionUpdate.cs` (612 строк, `ParentHorde`, `SpawnPayload` для орд); `BannerCarrierUpdate` 20–40 строк | M (3–5 дн.) найм; пополнение — S–M (2–3 дн.) поверх Horde API (поток A, A10) | пополнение идёт в поле (знаменосец, колодец, сила), а не у казармы: члены орды «доращиваются» внутри неё, это касается Horde API (§7.4 п. 2) |
| 15 | **Апгрейды и кузница** (`SubObjectsUpgrade` 168, `StatusBitsUpgrade` 100, `WeaponSetUpgrade` 77, `GeometryUpgrade` 49, `CommandSetUpgrade` 36, `GiveUpgradeUpdate` 35, `ArmorUpgrade` 34, `CostModifierUpgrade` 15, `ModelConditionUpgrade` 15 (ZH), `TooltipUpgrade` 11, `AudioLoopUpgrade` 8, `PlayerUpgradeSpecialPower` 3, `BaseUpgrade` 2; огненные стрелы = `WeaponSetUpgrade`/бонус оружия) | ZH в основном | `GameLogic/Object/Upgrade/*` (`ArmorUpgrade`, `WeaponSetUpgrade`, `WeaponBonusUpgrade`, `CommandSetUpgrade`, `SubObjectsUpgrade`, `StatusBitsUpgrade`, `CostModifierUpgrade`) | реализованы ZH-модули; BFME-модули только разбор | M (4–6 дн.) | апгрейды объекта vs игрока (кузница даёт игроку, юнит применяет): механика ZH подходит |
| 16 | **Победа/поражение** (`VictorySystemData`, `FactionVictoryData` 14) | ≈ZH | `GameLogic/System/GameLogic.cpp` + `ScriptEngine.cpp` (условия скирмиша) | разбор | S (2 дн.) | BFME1: поражение при потере всех зданий/юнитов (уточнить по `VictorySystemData`) |

### 1.4 Герои и силы

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 17 | **Герои: найм, лимит 1, возрождение** (`HeroDie` 2, `RespawnUpdate` 39, `RespawnBody` 55, `ImmortalBody` 158/`HighlanderBody` 48 (ZH); `BuildableHeroesMP` в PlayerTemplate (4); кнопки «воскресить» (`REVIVE`) в крепости; в графе скирмиша героев: Гондор 4 — Boromir, Faramir, Gandalf, Pippin; Мордор 4 — FellBeast, Gothmog, WitchKingOnFellBeast, Gollum, `out-all/gameplay.md:146`, `:158`) | ≈ZH | `ImmortalBody`, `HighlanderBody`, `UndeadBody` (ZH); лимит `MaxSimultaneousOfType` (`ThingTemplate.cpp`); возрождения в ZH **нет** | `RespawnUpdate.cs`, `HeroDie.cs` только разбор; `RespawnBody` есть в OpenSAGE (`out-all/gameplay.md:294–306`) | M (5–7 дн.) | хранить «мёртвого героя» с уровнем и опытом; стоимость воскрешения |
| 18 | **Способности героев** (`SpecialAbilityUpdate` ZH 28, `WeaponFireSpecialAbilityUpdate` 25, `ToggleMountedSpecialAbilityUpdate` 25, `LevelGrantSpecialPower` 14, `ModelConditionSpecialAbilityUpdate` 10, `HeroModeSpecialAbilityUpdate` 8, `ToggleHiddenSpecialAbilityUpdate` 5, `GrabPassengerSpecialPower` 6, `WeaponChangeSpecialPowerModule` 2, `DeflectSpecialPower` 2, `DominateEnemySpecialPower` 1 и т.д.; общий `SpecialPowerModule` 204 (в ZH это базовый класс без регистрации, в BFME1 — модуль); `AutoAbilityBehavior` 30) | ≈ZH | `GameLogic/Object/SpecialPower/SpecialPowerModule.cpp`, `OCLSpecialPower`, `Object/Update/SpecialAbilityUpdate.cpp`, шаблоны `Common/RTS/SpecialPower.cpp` (перезарядки) | `SpecialPower/SpecialPower.cs` (`SpecialPowerModule`, 170 строк); остальные только разбор | **L** (10–15 дн.) на 1v1-набор (по 5–8 способностей у ~4 героев на фракцию) | у каждого героя свои способности, разблокировка по уровню героя (через `ExperienceLevel.Upgrades` и условия кнопок; точный механизм проверить по данным) |
| 19 | **Силы (Spellbook) и очки сил** (`Science` 49 блоков, `SciencePurchasePointCost`; `PlayerTemplate.SpellBook`, `PurchaseScienceCommandSet`; очки копятся за опыт/убийства через `Rank` 85) | ≈ZH | система генерала ZH: `Common/RTS/Science.cpp`, `Common/RTS/Player.cpp` (skill points, science purchase points), `Core/GameEngine/Source/GameLogic/System/RankInfo.cpp` | `Logic/Science.cs`, `Logic/Rank.cs` (начисление очков по рангу), `Player.SciencePurchasePoints` | M (5 дн.) для механики; UI дерева — Дизайнер | дерево сил BFME1 = ветка наук ZH, это хорошее совпадение |
| 20 | **Сами силы** (BFME1: `ArrowStormUpdate`, `CloudBreakSpecialPower`, `ElvenWoodSpecialPower`, `TaintSpecialPower`, `DarknessSpecialPower`, `FreezingRainSpecialPower`, `DevastateSpecialPower`, `PlayerHealSpecialPower`, `RainOfFireUpdate`, `PartTheHeavensUpdate`, `RepairSpecialPower`, `ScavengerSpecialPower`, `ManTheWallsSpecialPower`, `FellBeastSwoopPower` — по 1–4 объявления; призыв войск — `OCLSpecialPower` (ZH, 41)) | ≈ZH / 0 | `OCLSpecialPower.cpp`, `FireWeaponPower` (ZH) | все перечисленные — только разбор | **L** (10–14 дн.), по 0,5–1,5 дня на силу | многие силы — это ModifierList + область + длительность, поэтому сначала №2 |

### 1.5 Бой, осада, стены

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 21 | **Конница и раздавливание** (`CrusherLevel`, `CrushableLevel`, `CrushRevengeWeapon` 42; `SquishCollide` 211 (ZH); `HorseHordeContain` 19; `AODCrushCollide` 4; `DetachableRiderUpdate`/`Body` 2; `ReplenishUnitsBehavior` 5 — всадники без лошади снова садятся на лошадей без седока, `ReplenishStatii = RIDERLESS`, `gondor\knight.ini:71`, `:75`) | ZH (+) | `Object/Collide/SquishCollide.cpp`, `Object/Die/CrushDie.cpp`, давление по `CrusherLevel/CrushableLevel` в `Object/Object.cpp`, `Object/Update/PhysicsUpdate.cpp`, `Object/Update/AIUpdate.cpp` (танки давят пехоту) | `Collide/` (SquishCollide 15 строк); BFME-модули только разбор | M (6–8 дн., +1 на `ReplenishUnitsBehavior`) | в BFME давят «на скорости» (атака с разгона), отбрасывание через `MetaImpactNugget`; эффект сильно зависит от физики на float |
| 22 | **Стрелки, огненные стрелы, залпы** | ZH | `Weapon.cpp` (разброс, `DumbProjectileBehavior`), `WeaponSetUpgrade` | `BezierProjectileBehavior.cs` (124 строки, 6 TODO) | S–M (3 дн.) поверх №3 | |
| 23 | **Осада: тараны, катапульты, лестницы, осадные башни** (`SiegeDockingBehavior` 17, `SiegeEngineContain` 4, `SiegeAIUpdate` 2, `SiegeDeploySpecialPower` 3, `SiegeDeployHordeSpecialPower` 12) | ≈ZH | артиллерия = оружие с дальним снарядом (`Weapon.cpp`), башни/лестницы ≈ `TransportContain.cpp` + выход на стену | только разбор | **L** (8–12 дн.) | связано со стенами (№24) |
| 24 | **Стены и ворота крепости** (`GateOpenAndCloseBehavior` 11, `GateProxyBehavior` 2, `WallUpgradeUpdate` 2, `ManTheWallsSpecialPower` 1; хождение по стенам) | 0 | KindOf `WALK_ON_TOP_OF_WALL` есть (`GeneralsMD/Code/GameEngine/Include/Common/KindOf.h`), учитывается в `Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp` и `GameLogic.cpp` **[насколько это покрывает стены BFME, не проверял]** | `Behaviors/GateOpenAndCloseBehavior.cs` (137 строк, базово: открытие для своих) | **L** (8–10 дн.) | в скирмише BFME1 стены есть у Хельмовой Пади/Минас Тирита и крепостей на картах; для первых 1v1 можно отложить |
| 25 | **Гарнизон** (`GarrisonContain` 1, `HordeGarrisonContain` 9, `EvacuateDamage` 9; башни и укрепления) | ZH | `Object/Contain/GarrisonContain.cpp` | `Contain/GarrisonContain` (91 строка) | S (2–3 дн.) | орда в здании = все члены как один пассажир |
| 26 | **Страх, оглушение, эмоции** (`EmotionTrackerUpdate` 98, `EmotionNugget` 15 блоков, `EmotionRange` 86; 2.22: страх и оглушение до 3-го уровня) | 0 | ближе всего состояние паники в AI (`GameLogic/AI/AIStates.cpp`, `Include/GameLogic/AIStateMachine.h`), `PoisonedBehavior` | `Update/EmotionTrackerUpdate.cs` только разбор; `EmotionNugget.cs` | M (5–7 дн.) | влияет на исход боя (террор назгулов, тролли) |
| 27 | **Смерть, обломки, трупы** (`SlowDeathBehavior` 728, `StructureCollapseUpdate` 255, `DestroyDie` 225, `TransitionDamageFX` 142, `FXListDie` 41, `CreateObjectDie` 34, `RefundDie` 15 (у 12 построек Гондора в графе), `RubbleRiseUpdate` 12, `ClearanceTestingSlowDeathBehavior` 14) | ZH в основном | `Object/Behavior/SlowDeathBehavior` и др. (ZH) | реализованы ZH-модули | S–M (3 дн.) для BFME-модулей | |
| 28 | **Прочее поведение юнитов** (`HitReactionBehavior` 79, `DualWeaponBehavior` 30, `AimWeaponBehavior` 7, `AutoPickUpUpdate` 12, `RunOffMapBehavior` 11, `DynamicPortalBehaviour` 11, `BannerCarrierUpdate` 10 (пополнение, №14), `SpecialEnemySenseUpdate` 3, `PorcupineFormationBodyModule` 12 (пикинёры), `SymbioticStructuresBody` 3, `DelayedDeathBody` 17, `AnimalAIUpdate` 30, `OathbreakersFadeAwayBehavior` 7, `PassiveAreaEffectBehavior` 4) | 0 | — | только разбор (у `BannerCarrierUpdate`, `PassiveAreaEffectBehavior` по 20–40 строк) | M–L (по 0,5–2 дня на модуль, суммарно 8–12) | `DualWeaponBehavior` (переключение лук/меч) и `HitReactionBehavior` заметны в бою, остальное можно позже |

### 1.6 AI скирмиша, скрипты карт, команды

| # | Система | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 29 | **AI скирмиша** (`PlayerAIType` 15 блоков, `AIData` в `aidata.ini`; скрипты карт) | ≈ZH | `GameLogic/AI/AISkirmishPlayer.cpp`, `AIPlayer.cpp` (build lists, команды, атаки), `Squad.cpp`, скриптовый движок `GameLogic/ScriptEngine/ScriptEngine.cpp` | `Logic/AI/SkirmishAIPlayer.cs`, `AIPlayer.cs` (~154 строки), `PlayerAIType.cs`; почти пусто | **XL** (15–25 дн.) | AI ZH строит бульдозерами по списку, а BFME1 строит на слотах крепости, нанимает орды, тратит очки сил. План: Э3 — свой простой скриптовый AI (порядок слотов + волны), затем данные BFME1. Скрипты карт — №30 |
| 30 | **ScriptEngine BFME: скрипты карт.** 126 действий и 41 условие BFME1, которых нет в ZH (≈ 18 % и 13 % вызовов; `docs/formats/BFME1_vs_ZH.md:77`; список — `docs/formats/data/bfme1_script_names_maps_big.tsv`). Бинарный формат совпадает с ZH: Script v2, Condition v4, Action v2 (`BFME1_vs_ZH.md:16`, `:76`). Чанки `Teams`, `PlayerScriptsList`, `BuildLists` регистрирует E1-52 (Форматы) | ≈ZH | `GameLogic/ScriptEngine/ScriptEngine.cpp` (343 действия, 103 условия), `ScriptActions.cpp`, `ScriptConditions.cpp` | разбор чанков карт; исполнения действий BFME нет | **XL** (15–30 дн., оценка Форматов: `BFME1_vs_ZH.md:316`) | **Решено** (§2.2 п. 10): в Э1 неизвестные действия и условия пропускаются (E1-37, `E1-BACKLOG.md:114`), реализация в Э3. Подсчёт сделан по базовому `maps.big`; по картам 2.22 и `_patch222libraries.big` его надо повторить (§6 Форматам №9). Частично пересекается с №29 (скрипты AI) |
| 31 | **Стойки и команды орды** (`out-all/gameplay.md:33–54`): `Command_HoldGroundUnit` и `Command_HoldGroundHero` (`commandbutton.ini:6305`, `:6318`) с `Command = TOGGLE_NO_AUTO_ACQUIRE` — в 19 наборах команд героев и в 8 наборах осадных орудий, троллей и `MordorWorkerCommandSet` (рабочий Мордора); переключение построения `HORDE_TOGGLE_FORMATION` (21 кнопка, первая — `commandbutton.ini:30`); охрана `Command_Guard` (`commandbutton.ini:613`) в 155 наборах | ≈ZH | `GUARD` есть (`Core/GameEngine/Include/GameClient/ControlBar.h:175`); `TOGGLE_NO_AUTO_ACQUIRE` и `HORDE_TOGGLE_FORMATION` в ZH нет; возможный аналог «не атаковать самому» — `AttitudeType` (`GeneralsMD/Code/GameEngine/Include/GameLogic/AI.h:321`) **[применимость не проверял]** | кнопки разбираются (`CommandButton`) | S–M (2–4 дн.) | новые значения `GUICommandType` — это правка апстрима (`// BFME:`, ревью Лида); флаг «стоять» должен входить в `xfer` и CRC объекта/орды |

### 1.7 Нелогическое, но мешающее загрузке (не моя область, для полноты)

- **Draw и частицы** — у Лида (§2.2 п. 6), в потоки A, B и C не входят (§7). Источник истины реестра — `docs/bfme-draw-modules.md`: 11 Draw (7 новых: `W3DScriptedModelDraw` 2483, `W3DFloorDraw` 84, `W3DHordeModelDraw` 59, `W3DStreakDraw` 26, `W3DLightDraw` 25, `W3DBuffDraw` 17, `W3DQuadrupedDraw` 3; 4 расширенных ZH: Truck, Tank, Tree, Laser) и 24 модуля частиц. В Э1 Draw отображаются на `W3DModelDraw`; новые поля расширенных парсер пропускает с записью в лог; частицы пропускаются, в лог идёт одна строка со списком. На CRC логики не влияют.
- **`ClientBehavior`** — не Draw и не входит в 124 (§2.2 п. 12). Это синоним ключа `ClientUpdate`: одна строка в таблице полей `ThingTemplate` с `MODULETYPE_CLIENT_UPDATE`, пометка `// BFME:`. Четыре заглушки делает Геймплей в `BfmeModuleRegistry`. В CRC логики не входят, сохраняются с `Drawable`. Текста `BeginScript` у него нет: сырой блок приходит одной `AsciiString` только в Draw. В Э1 этот текст только хранится и игнорируется, Lua не исполняется. Лид в E1-34 делает только сопоставление состояний анимации с состояниями ZH (§2.2 п. 11).
- `DelayedLuaEventUpdate` 1, `CritterEmitterUpdate` 1, `CivilianSpawnUpdate` 1 / `CivilianSpawnCollide` 2, `FadeAndDieOrnamentUpdate` 1: заглушки.
- `OneRingPenaltyUpdate` 2 (Кольцо) и `LivingWorld*`: в скирмише 1v1 не нужны.

### 1.8 Итог по объёму

| Блок | Объём |
|---|---|
| Ядро (№1–7) | ≈ 35–50 чел.-дней |
| Орды (№8–10) | ≈ 22–32 |
| Экономика и застройка (№11–16) | ≈ 26–38 |
| Герои и силы (№17–20) | ≈ 30–41 |
| Бой, осада, стены (№21–28) | ≈ 41–56 (+1 на `ReplenishUnitsBehavior`) |
| AI (№29) | ≈ 15–25 |
| Скрипты карт (№30), поток C | ≈ 15–30 (оценка Форматов); на критическом пути 1v1 только минимум, список после datacheck |
| Стойки и команды орды (№31) | ≈ 2–4 |
| Продажа построек (№12); падение дохода от соседей — только если замер найдёт зависимость (риск понижен, §5 №5) | ≈ 1–2 на продажу; +1–3 условно |
| **Всего геймплей BFME1, Э2+Э3** | **≈ 190–280 чел.-дней [оценка]** (сумма строк 188–281; было 170–240) |
| Сверх этого в Э1 у Геймплея (поток A, §7.2): E1-49 GameMath + FPCR R1 (5–7), E1-36 (3–4), E1-50 (1–2, моё уточнение), E1-37 (1–2) | ≈ 10–15 |

Рост против r2 (≈ +20–40): скрипты карт (№30) впервые учтены отдельно, добавились стойки (№31) и продажа. Дни на падение дохода от соседей в вероятный итог не входят: риск понижен (§5 №5), +1–3 только если замер найдёт зависимость. Draw-модули в итог не входят, это работа Лида. Скрипты отчасти пересекаются с №29, так что верхняя граница завышена.

Это больше, чем PLAN.md закладывает на Э2+Э3 (10–16 недель) для одного агента. Поэтому §3 режет объём: только то, что нужно для 1v1 на 2.22, с «похожим» поведением, и с Э2 работают двое: второго исполнителя подключат к старту Э2, если Э1 пройдёт точку «идём» (Оркестратор, §2.3). Разбиение на потоки A, B и C — §7.

### 1.9 Граф скирмиша 2.22 (datacheck, для выбора пары 1v1)

Граф Форматов: PlayerTemplate → стартовые здания, герои, CommandSet → кнопки найма и застройки → объекты, рекурсивно, плюс раскладки `.bse` (`out-all/gameplay.md:135–170`). Спавн через OCL, призыв силами и объекты карт граф не прослеживает.

| Фракция | герои | орды | юниты | постройки | раскладок `.bse` | источник |
|---|---|---|---|---|---|---|
| Гондор | 4 | 5 | 6 | 23 | 13 | `out-all/gameplay.md:141`, `:150` |
| Изенгард | 3 | 4 | 9 | 11 | 14 | `:142` |
| Мордор | 4 | 4 | 12 | 16 | 16 | `:143` |
| Рохан | 8 | 6 | 11 | 30 | 14 | `:144` |

Мой разбор того же графа по модулям (`/workspace/gp-scratch/skirmish_mods.json`, итоговые объекты после наследования):
- **Пара Гондор + Мордор использует 120 разных модулей: 44 из ZH и 76 новых** (66 Behavior, 3 Body, 4 Draw, 3 ClientBehavior). Все 4 фракции вместе — 86 новых. Из 124 новых в графе не встречаются 38: силы, снаряды и призыв (их граф не видит, поэтому список занижен), Living World, Кольцо, редкие модули карт.
- Чаще всего у объектов Гондора и Мордора встречаются новые модули (объектов Г/М): `CastleMemberBehavior` 22/16, `GettingBuiltBehavior` 18/13, `HitReactionBehavior` 9/11, `EmotionTrackerUpdate` 7/8, `SpecialPowerModule` 4/8, `RefundDie` 12/0, `BuildingBehavior` 12/0, `HordeContain` 4/4, `RespawnBody`/`RespawnUpdate` 4/4, а также `LevelUpUpgrade`, `HordeMemberCollide`, `HordeAIUpdate`.
- 4 модуля из `DefaultThingTemplate` (§0) есть у каждого объекта графа.
- Частоты по объектам карт 1v1 2.22 будут после прогона datacheck с `_patch222maps.big` (`out-all/gameplay.md:170`; §6 Форматам №9).

---

## 2. Решения

### 2.1 Мои исходные решения (не противоречат ADR и NET-DESIGN)

1. **Логика 30 Гц** (ADR-0002 §7: `LOGICFRAMES_PER_SECOND = 30`, без `libmain60`). Все длительности INI BFME1 (в мс) переводим в кадры через `ConvertDurationFromMsecsToFrames` (`Core/GameEngine/Include/Common/GameCommon.h`) с `ceil`, как в ZH. На вопрос NET-DESIGN §3.0 («30 или 5 Гц») мой ответ как владельца геймплея: **30 Гц** — я не нашёл в данных ничего, что требует 5 Гц, а 30 Гц дают меньшую задержку ввода. Отдельного решения Лида по этому вопросу ещё нет (§6 Лиду №1), но ADR-0002 §7 и черновик ADR-0003 уже исходят из 30 Гц.
2. **Код BFME в `engine/bfme/`** (ADR-0001 §3), регистрация в фабриках движка через одну точку расширения в `ModuleFactory::init` с пометкой `// BFME:`. Каждый модуль BFME — отдельный класс `Bfme<Имя>` с собственным `ModuleData` и `xfer`/`crc`, чтобы он попадал в CRC и сейвы.
3. **Поведение «похоже на BFME1», а не побайтно** (PLAN §5). Эталоны поведения: данные INI 2.22, OpenSAGE как справочник разбора, наблюдение за игрой на ПК Андрея (только с его согласия). Open-BFME-1 не используем.
4. **Сначала данные, потом поведение.** Каждый модуль сначала парсится полностью (заглушка без логики), потом получает минимальное поведение, и только затем доводится.

### 2.2 Решения Лида движка (02.10.2026), касающиеся геймплея

Детерминированная математика, флаги компилятора, flush-to-zero и FPCR, запрет `long`, `lroundf` и `long double`, правила CRC вынесены в **adr/ADR-0003-determinism.md**; при расхождении действует ADR. Здесь — то, что касается модулей и геймплея (п. 1–4 — первая серия решений, п. 5–10 — вторая).

1. **Модули BFME: явный реестр** (п. 1 Лида; закрывает §6 Лиду №4 и Форматам №4).
   - В `engine/bfme/` — явный реестр всех 113 логических модулей, которых нет в ZH (108 Behavior + 5 Body по datacheck out-all, §0; в r2 было «123 имени» вместе с клиентскими). Таблицу имён (имя, тип модуля, интерфейсы) генерируем из вывода datacheck Форматов, а не пишем руками.
   - Каждая заглушка без ошибок разбирает свой INI-блок по схеме Форматов: неизвестные поля пропускает и считает (в лог: модуль, поле, число), в логике ничего не делает. `xfer`/`crc` версионированы с первого дня.
   - Подключение одной точкой: `BfmeModuleRegistry::registerAll(ModuleFactory&)` вызывается в конце `ModuleFactory::init()` (`GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp`). Это единственная правка апстрима для реестра, с пометкой `// BFME` и строкой в `docs/UPSTREAM-PATCHES.md`.
   - Страховка: мягкий режим `findModuleTemplate` под флагом `BFME_SOFT_MODULES`. В dev включён (неизвестное имя → заглушка, в лог имя и частота), в CI и релизе выключен: **в CI неизвестное имя модуля — ошибка сборки данных**.
   - Найдено при проверке: `ModuleFactory::addModuleInternal` (`ModuleFactory.cpp:689`) молча перезаписывает шаблон с тем же именем. Поэтому `registerAll` падает на повторном имени (п. 5). Draw-модули в этот реестр не входят (п. 6). Сверх 124 в него входят 4 заглушки `ClientBehavior` (п. 12). E1-36 зависит от E1-56 (`E1-BACKLOG.md:113`, п. 11): `BeginScript` сам по себе нужен только E1-34, но без остального объёма E1-56 INI 2.22 не загрузятся, и карта M1 для заглушек не поднимется.
2. **Опыт и уровни** (п. 4 Лида; закрывает §6 Лиду №3).
   - Enum `VeterancyLevel` (`Core/GameEngine/Include/Common/GameCommon.h`, 4 уровня) не трогаем: он в сейвах, сети и десятках `switch`.
   - Заводим отдельный счётчик в модуле BFME: опыт (`Int`) и уровень 1–10, с `xfer` и CRC. Пороги, награды, `Upgrades`, `AttributeModifiers` берём из блоков `ExperienceLevel`.
   - Для старых эффектов ZH (иконки ветеранства, `VeterancyGainCreate`, бонусы WeaponBonus по ветеранству) — одностороннее отображение уровня BFME в `VeterancyLevel`; обратного пути нет. Таблицу отображения согласуем с Лидом при ревью.
   - Опыт орды: члены отдают опыт контейнеру через имеющийся в ZH «приёмник» (`ExperienceTracker::setExperienceSink`, `GeneralsMD/Code/GameEngine/Include/GameLogic/ExperienceTracker.h:59`), начисление за убийство — `Object::scoreTheKill` (`Object.h:233`, `Object.cpp:3008`).
3. **Орда** (п. 5 Лида; закрывает §6 Лиду №5).
   - Прототип с настоящими объектами-членами (по образцу `MobNexusContain` + `MobMemberSlavedUpdate`), решение по замеру.
   - Бюджет: тик логики **≤ 10 мс по p95 на Pixel 8 Pro** в тяжёлой сцене на 4 игроков (треть кадра при 30 FPS). Сценарий замера (число батальонов, бой, pathfinding, длительность) согласовываю с QA до начала прототипа.
   - Не влезаем — переходим на виртуальных членов (один логический объект, члены только для отрисовки и попаданий). Это +8–12 дн. к №8 [оценка] и меняет точку стыковки «события смерти» (§7.4).
4. **ModifierList и нагеты оружия** (п. 6 Лида; закрывает §6 Лиду №6).
   - Владелец — Геймплей. Логика в `engine/bfme/`. В `Weapon.cpp` и `Damage.cpp` — минимальные хуки с пометкой `// BFME`; каждый хук в апстримном файле проходит ревью Лида и записывается в `docs/UPSTREAM-PATCHES.md`.
   - Порядок применения модификаторов фиксирован (§4.1 п. 5), чтобы не зависеть от порядка прихода аур.
5. **Дубли в реестре** (закрывает §6 Лиду №11 в части логики).
   - `registerAll` во всех сборках (dev, CI, релиз) падает с понятной ошибкой на повторном имени: какое имя, кто его уже зарегистрировал (ZH или BFME).
   - Намеренная замена модуля ZH возможна только через явный список `overrides`. У каждой строки есть комментарий «почему»: что меняем и чем не подходит модуль ZH.
6. **Draw-модули и частицы — реестр Лида в GameEngineDevice** (закрывает §6 Лиду №11; уточнено вечером 02.10.2026).
   - Лиду отходят **11 Draw** (7 новых и 4 расширенных: `W3DTruckDraw`, `W3DTankDraw`, `W3DTreeDraw`, `W3DLaserDraw`) и **24 модуля частиц**. `ClientBehavior` сюда не входит (п. 12).
   - `BfmeDrawModuleRegistry::registerAll` вызывается из `W3DModuleFactory::init` (`Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp:55`). Правила про дубли и `overrides` те же, что в п. 5.
   - Логический реестр ничего не знает про W3D. В потоки A, B и C draw не входят (§7).
   - В Э1 (E1-34, зависит от E1-56 и E1-56b; строка бэклога — `E1-BACKLOG.md:111`) все Draw отображаются на `W3DModelDraw`. Новые поля расширенных модулей парсер пропускает и пишет в лог. Частицы в Э1 пропускаются: в лог идёт одна строка со списком. `AnimationState` и `IdleAnimationState` в ZH нет; сопоставление с состояниями ZH делает Лид здесь же (п. 11).
   - Источник истины для реестра Лида — `docs/bfme-draw-modules.md` (шапка таблицы §1).
7. **Правки апстрима и порядок кода** (закрывает §6 Лиду №12).
   - Любая правка апстрим-файла (`ProductionUpdate.cpp`, `Player.cpp`, `Weapon.cpp`, `Damage.cpp`, `GameLogic.cpp` и др.) помечается `// BFME:` и вносится в `docs/UPSTREAM-PATCHES.md`, ревью — у Лида.
   - Код в `engine/bfme/` Лид ревьюит выборочно.
   - Код идёт PR-ами в ветку `bfme`; в `main` не пушим.
8. **GameMath своя, на libm из musl (MIT)** (закрывает §6 Лиду №9, №10).
   - Задача E1-49 (`E1-BACKLOG.md:127`) — за Геймплеем, 5–7 дн. Сюда же входят проверка FPCR R1, запрет `long`/`long double` в CI (§4.1) и проход CI по телам `::crc(Xfer*)`: вызов `xfer(` ловится при любом имени аргумента. В allowlist явно стоит `AIUpdateInterface` (`AIUpdate.cpp:5086`); в `engine/bfme/` список пустой (§4.3 п. 8).
   - `SAGE_USE_DETERMINISTIC_MATH` остаётся OFF до готовности GameMath. Флаги компилятора уже стоят (`cmake/compilers.cmake`, E1-10).
   - E1-45 (сравнение CRC x86_64 и arm64) идёт после E1-49; до неё — только контрольный прогон с флагами.
   - Подробности (состав функций, каталог `Core/Libraries/Source/GameMath/`, побитовый тест на x86_64 и arm64 под qemu, правила CRC) — в **adr/ADR-0003-determinism.md**.
9. **Уровень героя 1–10** — отдельный счётчик в модуле BFME; enum `VeterancyLevel` не трогаем (подтверждение п. 2).
10. **Скрипты карт.** 126 действий и 41 условие BFME1 отсутствуют в ZH (≈ 18 % и 13 % вызовов; `docs/formats/BFME1_vs_ZH.md:77`).
   - В Э1 их пропускаем (E1-37, `E1-BACKLOG.md:114`), реализуем в Э3 (§1 №30, §3.2).
   - Бинарный формат совпадает с ZH: Script v2, Condition v4, Action v2 (`BFME1_vs_ZH.md:16`, `:76`), так что работа — в исполнении, а не в разборе. Исполнение с вечера 02.10.2026 — поток C (§2.3, §7.3a), не поток B.
11. **Загрузка INI 2.22 и `BeginScript`** (закрывает §6 Лиду №15; бэклог v3.5, `E1-BACKLOG.md:102`). Этот документ бэклог не меняет.
   - Делают Форматы в `INI.cpp`. Оценка Лида — 5–6,75 дня, зависимость — E1-21. Мелкий синтаксис и `MapCache.ini` — отдельная E1-56b (`E1-BACKLOG.md:103`), её надо закончить до E1-34.
   - `BeginScript` нужен только Draw, то есть E1-34. В Behavior и Body таких блоков нет. Связь E1-36 → E1-56 Лид оставил (`E1-BACKLOG.md:113`): без остального объёма E1-56 INI 2.22 не загрузятся, и карта M1 для заглушек не поднимется.
   - Блоков в 2.22 — 5948, в базе — 1783 (`tools/datacheck/out-all/report.md` §8.1). Старые 2092 — только блоки прямо в Draw-модуле, без обёрток `ReplaceModule` (3785) и `AddModule` (71).
   - Все блоки сидят в Draw: `W3DScriptedModelDraw` 3863, `W3DHordeModelDraw` 1920, `W3DQuadrupedDraw` 102, `W3DTruckDraw` 63.
   - `parseScriptBlock` ставится в FieldParse состояний `AnimationState` (5436), `IdleAnimationState` (484) и `TransitionState` (28), а не в FieldParse самого модуля. `AnimationState` и `IdleAnimationState` в ZH нет: в `W3DModelDraw.cpp` из этих трёх есть только `TransitionState` (`:1214`). Сопоставление с состояниями ZH делает Лид в E1-34. В E1-56 сырое чтение `BeginScript` срабатывает раньше пропуска неизвестных вложенных блоков, иначе блок состояния закроется и скрипт не прочитается.
   - Lua-строки `end` (1017 блоков) и `;` (531) требуют читать блок сырым до токенизации. Конец блока ZH ищет через `stricmp` с `End` (`INI.cpp:1642`, ещё одно сравнение — `:2246`); `;` обнуляет хвост строки (`INI.cpp:602–606`).
   - Сверх `BeginScript` в ту же E1-56 входят: `#define` и макросы (10149 числовых подстановок и 423 нечисловых, `report.md` §8.2), пропуск неизвестных блоков верхнего уровня (8045, вместо `throw` на `INI.cpp:503–507`), `ChildObject` (609 потомков через путь `ObjectReskin`), пропуск модуля, когда `ModuleFactory::newModuleDataFromINI` вернул `nullptr` (`ModuleFactory.cpp:594`, без разыменования в `ThingTemplate.cpp:623`), пропуск вложенных блоков с учётом глубины (35 590 блоков).
   - Контракт заглушек Геймплея: Behavior и Body текста скрипта не получают. Текст `BeginScript` приходит одной `AsciiString` только в Draw. В Э1 этот текст только хранится и игнорируется, Lua не исполняется. Лид в E1-34 делает только сопоставление состояний анимации с состояниями ZH.
12. **`ClientBehavior`** (закрывает §6 Лиду №14).
   - Синоним ZH-ключа `ClientUpdate`, не отдельный вид модуля и не Draw.
   - Одна строка в таблице полей `ThingTemplate` с `MODULETYPE_CLIENT_UPDATE`, пометка `// BFME:`.
   - Эту строку и 4 заглушки client update делает Геймплей в `BfmeModuleRegistry` (вызов из `ModuleFactory::init`).
   - В CRC логики не входят. Сохраняются вместе с `Drawable`.
13. **Счёт 124** (закрывает §6 Лиду №13).
   - 124 = 113 (108 Behavior и 5 Body, реестр Геймплея) + 11 Draw (реестр Лида).
   - 4 заглушки `ClientBehavior` идут сверх 124. Критерий E1-50 их тоже учитывает. Строку E1-50 в `docs/E1-BACKLOG.md` правит Лид.

### 2.3 Ответы Оркестратора (02.10.2026)

- Сроки в PLAN.md Оркестратор пересматривает сам; оценки здесь — вход для него.
- **Второй исполнитель на геймплей** подключается к старту Э2, если Э1 пройдёт точку «идём» (закрывает §6 Лиду №7 условно). Разбиение работы — §7.
- Порядок Э2/Э3 из §3 — базовый.
- **Версия данных:** на ПК Андрея 2.22; `_patch222*.big` перекрывают базовый `ini.big`. Теперь они на боксе, и datacheck прогнан со слоем патча (`out-all/`, §0). Сверка баланса только по слою с патчем (`docs/formats/bfme1-big-order.md`, E1-22). Закрывает §6 Форматам №1 и №8.
- **Разбиение на потоки принято** (r3): поток A («юнит в бою») — мой, поток B уходит второму исполнителю на старте Э2 (§7).
- **Командные очки 1v1** (вечер 02.10.2026; закрывает §6 Оркестратору №3 с оговоркой). Эталон — MP2 230/460 (`gamedata.ini:306–307`). Полный набор 2.22 уже сверен с `report.md` §4 (§1 №6). Оговорка одна: откуда движок берёт набор по числу игроков, Форматы ещё показывают.
- **Падение дохода фермы** (закрывает §6 Оркестратору №2). Ни один источник не подтверждает падение от соседних построек в BFME1: радиусы `TerrainResourceBehavior` и `ResourceModifierValues` — это BFME2, в OpenSAGE доход = `DepositAmount` × `ProductionModifier` без соседей. Реверс exe не делаем. Замер чёрным ящиком с согласия Андрея ближе к Э3 сначала проверяет, есть ли зависимость вообще (`docs/farm-income-measurement.md`). Риск понижен.
- **Скрипты карт — поток C** (закрывает перекос §7). Исполнителя назначат на Э2/Э3. На критическом пути 1v1 остаётся только минимум скриптов скирмиш-карт, остальное уходит в C. Точный список минимума — после datacheck по картам 1v1 2.22 (`_patch222maps.big`).

### 2.4 Ответы Форматов (02.10.2026)

- `.bse` берут Форматы (скорее всего DataChunk, как `.map`, вместе с E1-25). Закрывает §6 Форматам №3.
- Схемы полей модулей — у Форматов в `formats/`, поведение — у Геймплея в `engine/bfme/`. Закрывает §6 Форматам №4.
- `ChildObject` наследует модули с заменой и удалением по тегу; частоты считаются по итоговому объекту (мои частоты в §0 этого не учитывают). Числа парсятся без зависимости от локали: `std::from_chars`, без `strtof`. Закрывает §6 Форматам №5 и снимает пометку в §4.1 п. 6.
- **Механики 2.22 с файл:строка** пришли в `out-all/gameplay.md` §1 (закрывает §6 Форматам №6 и №7):
  - продажа: только постройки (55, `SellPercentage = 50%`, `gamedata.ini:484`), юнитов продавать нельзя (§1 №12);
  - стойки: `TOGGLE_NO_AUTO_ACQUIRE` (19 наборов героев, 8 наборов осадных орудий, троллей и рабочего Мордора), `HORDE_TOGGLE_FORMATION`, `Command_Guard` (§1 №31);
  - пополнение: `BannerCarrierUpdate`, колодцы `ReplenishNearbyHorde`, `HealSpellHordeReplenishPing`; `ReplenishUnitsBehavior` — посадка на лошадей без седока (§1 №14, №21);
  - падение дохода от соседних построек в BFME1 ни один источник не подтверждает: радиусы относятся к BFME2, OpenSAGE считает `DepositAmount` × `ProductionModifier` без соседей (§1 №13, §5 №5, `docs/farm-income-measurement.md`);
  - командные очки 2.22 сверены с `report.md` §4: SOLO 130/240, ИИ 900/1500, MP2 230/460, MP3 и MP4 200/400, MP56 160/320, MP78 150/300, бонусы кампании 600/1400 (§1 №6). Горный тролль стоит 15 CP (в 1.03 было 10).
- **Граф скирмиша 2.22** и разбор ошибок моего подсчёта — `out-all/gameplay.md` §2–3 (§0, §1.9). Закрывает §6 Форматам №2 частично: карт 1v1 2.22 ещё нет.

---

## 3. Порядок реализации Э2 и Э3

**Уточнено по datacheck out-all со слоем 2.22** (§0, §1.9): граф скирмиша Форматов, итоговые объекты после `ChildObject`, частоты 2.22. Ещё не учтены объекты карт 1v1 2.22: datacheck прогнан до прихода `_patch222maps.big` (§6 Форматам №9). Оркестратор подтвердил порядок как базовый (§2.3). Как он делится на два потока — §7.

Что изменилось по данным:
- Первыми в реестре идут 4 модуля из `DefaultThingTemplate`: они есть у всех 2843 объектов (§0). Без них не загрузится ни один объект, а `AttributeModifierPoolUpdate` и `AttributeModifierUpgrade` к тому же связаны с ModifierList (A1).
- Для Гондора и Мордора `CastleMemberBehavior` и `GettingBuiltBehavior` — самые частые новые модули (22/16 и 18/13 объектов), что подтверждает высокий приоритет крепости и слотов в Э3.
- Пополнение батальонов идёт через знаменосцев и колодцы, а не через казарму: оно переезжает из найма (B5) в орду (A10).
- Скрипты карт (№30) впервые стоят отдельным шагом Э3.

**Пара фракций для первого 1v1:** **Гондор против Мордора** (Добро против Зла: разные лимиты CP, у Добра фермы через `AutoDepositUpdate`, у Зла орки дешёвые и большие орды). Rohan и Isengard идут вторыми, когда работают модули, общие для фракции. По графу 2.22 (§1.9) у Гондора 4/5/6/23 (герои/орды/юниты/постройки), у Мордора 4/4/12/16; пара требует 76 новых модулей из 124. Пару подтверждает Оркестратор/Андрей.

### 3.1 Э2 «Юниты и бой» (цель PLAN: два батальона разных фракций бьются до конца, 10 мин без падений, реплей совпадает по CRC)

| Шаг | Что | Пункты §1 | Объём | Готово, когда |
|---|---|---|---|---|
| Э2.1 | Явный реестр 113 логических модулей и 4 `ClientBehavior` (`BfmeModuleRegistry`, таблица из datacheck out-all, §2.2 п. 1, п. 5, п. 12; E1-36, E1-50; E1-36 зависит от E1-56, §2.2 п. 11: не из-за текста скрипта, а потому что без E1-56 INI 2.22 и карта M1 не грузятся): заглушки разбирают блок, неизвестные поля пропускают и считают; `xfer`/`crc` пустые, но версионированные; падение на дубле во всех сборках, `overrides` с комментарием; `BFME_SOFT_MODULES` только в dev. Первыми — 4 модуля `DefaultThingTemplate`. В Behavior и Body `BeginScript` нет; текст скрипта в Э1 есть только у Draw: он хранится и игнорируется, Lua не исполняется. Лид в E1-34 делает только сопоставление состояний анимации с состояниями ZH | 1 | M (стартует в Э1) | все 2843 итоговых объекта со слоем 2.22 создаются без падения; в CI неизвестное имя — ошибка. 11 Draw — в реестре Лида (E1-34, после E1-56). 4 `ClientBehavior` — в нашем реестре |
| Э2.2 | Детерминизм по ADR-0003, **делается в Э1 как E1-49** (§2.2 п. 8): своя GameMath на libm из musl в `Core/Libraries/Source/GameMath/`, побитовый тест x86_64 и arm64 (qemu), перевод `Trig.cpp`/`wwmath.h`/`PhysicsUpdate.cpp:793` в логике; хелперы `int32_t`; FPCR R1 (`fp::checkAndReset` в начале тика на базе `setFPMode`, `fp::assertExpected` в конце тика и в `getCRC`); CI-грепы на `long`, `long double`, `lroundf`; проход по телам `::crc(Xfer*)` ловит вызов `xfer(` при любом имени аргумента, в allowlist явно `AIUpdateInterface` (`AIUpdate.cpp:5086`); `SAGE_USE_DETERMINISTIC_MATH` OFF до готовности, потом ON | §4 | M (5–7 дн., оценка Лида; было 6–9) | E1-45 после E1-49 + QA: CRC x86_64 = arm64 на реплее; CI ловит `GameClientRandomValue`/`rand()`/`sinf`/`lroundf`/`long`/`long double` в логике |
| Э2.3 | Оружие на нагетах: `DamageNugget`, `ProjectileNugget`, ближний бой, `BezierProjectileBehavior`; броня BFME через ZH `Armor.cpp` | 3, 22 | L | одиночный мечник бьёт одиночного орка, лучник стреляет дугой |
| Э2.4 | Опыт и уровни `ExperienceLevel` (без героев): свой счётчик 1–10 + отображение в `VeterancyLevel` (§2.2 п. 2) | 4 | M | юнит набирает уровни, получает бонусы из списка; уровень в CRC и сейве |
| Э2.5 | ModifierList, базовая версия: урон, броня, скорость, по длительности и навсегда | 2 | L (первая половина) | бонус уровня и ауры работают и видны в CRC-дампе |
| Э2.6 | Орда, базовая версия (+ стойки `TOGGLE_NO_AUTO_ACQUIRE` и `HORDE_TOGGLE_FORMATION`, №31, в конце шага): `HordeContain` (ряды из `RankInfo`, выпуск рядов в бой, смерть членов, распад), `HordeAIUpdate` (приказы орде → членам), `HordeMemberCollide`, локомоторы BFME (минимум полей) | 5, 7, 8, 31 | XL (базовая часть ≈ 10–12 дн.) + S–M (2–4) | сначала прототип на настоящих объектах и замер с QA (≤ 10 мс p95, Pixel 8 Pro, 4 игрока, §2.2 п. 3) — точка решения; затем два батальона идут строем, сходятся, бьются до конца |
| Э2.7 | Конница: раздавливание по `CrusherLevel/CrushableLevel` (ZH) + `MetaImpactNugget` (отбрасывание), `HorseHordeContain`, `ReplenishUnitsBehavior` (посадка на лошадей без седока) | 21 | M | рыцари проходят сквозь орков, урон и отбрасывание повторяются в реплее |
| Э2.8 | Смерть и обломки BFME-модули, `HitReactionBehavior`, `DualWeaponBehavior` | 27, 28 | M | без «застывших» трупов и пропавших юнитов |

Итого Э2 ≈ 42–59 чел.-дней (было 40–55; +2–4 на стойки). Э2.1 и Э2.2 делаются в Э1 (E1-36, E1-50, E1-49) и в эту сумму не входят. В план Э2 (4–6 недель) остальное укладывается только при двух потоках (§7).

### 3.2 Э3 «Экономика и строительство» — минимум для 1v1 Гондор vs Мордор на карте 2.22

Порядок «чтобы можно было сыграть матч», затем «чтобы было похоже»:

| Шаг | Что | Пункты §1 | Объём |
|---|---|---|---|
| Э3.1 | Крепость: `CastleBehavior` + `.bse` (вместе с Форматами), `CastleMemberBehavior`, `KeepObjectDie`; стартовый лагерь на точке старта | 11 | L |
| Э3.2 | Слоты застройки: `BASE_FOUNDATION/BASE_SITE`, `FoundationAIUpdate`, `GettingBuiltBehavior`, радиальное меню через CommandSet (UI — Дизайнер); продажа построек (`SELL` ZH, 50 %) | 12 | M–L + S |
| Э3.3 | Ресурсы: `AutoDepositUpdate` + поля BFME (ферма 17 за 5400 мс), рост по уровню через `Modifier = PRODUCTION` (нужен A1), рабочие Зла через `WorkerAIUpdate`; падение от соседей ни один источник для BFME1 не подтверждает (радиусы — BFME2; OpenSAGE: `DepositAmount` × `ProductionModifier`). Замер (`docs/farm-income-measurement.md`) сначала проверяет, есть ли зависимость; до ответа без падения. Риск понижен | 13 | S–M (+1–3 только если зависимость найдётся) |
| Э3.4 | Найм орд (`ProductionUpdate` + `InitialPayload`), командные очки и лимит: эталон 1v1 — MP2 230/460 (`gamedata.ini:306–307`, §2.3). Остальные наборы 2.22 — §1 №6 (`report.md` §4): SOLO 130/240 (`:294–295`), ИИ 900/1500 (`:301–302`), MP3 и MP4 200/400, MP56 160/320, MP78 150/300, бонусы кампании 600/1400. Горный тролль 15 CP (в 1.03 было 10). Откуда движок берёт набор по числу игроков — ещё не показано | 6, 14 | M |
| Э3.5 | Победа/поражение (`VictorySystemData`) | 16 | S |
| Э3.6 | Простой AI: свой скриптовый (на ZH `AISkirmishPlayer`/`AIPlayer`): занять слоты по списку, строить ферму/казарму/кузницу, собирать волну из N батальонов, атаковать ближайшую цель | 29 (часть) | L |
| Э3.6a | Минимум скриптов скирмиш-карт (поток C): только то, без чего карта 1v1 2.22 не играется. Список — после datacheck с `_patch222maps.big` | 30 | входит в 15–30 потока C, отдельной оценки нет |
| — | **Точка «первый матч»: Андрей может сыграть Гондор vs Мордор от старта до победы** | | |
| Э3.7 | Герои: найм, лимит 1, уровни, возрождение (`RespawnUpdate`, `HeroDie`), 2–3 героя на фракцию с 3–4 способностями | 17, 18 | M + L |
| Э3.8 | Силы: очки сил (ветка наук ZH), дерево (Spellbook), 4–6 первых сил каждой стороны, затем остальные | 19, 20 | M + L |
| Э3.9 | Апгрейды и кузница (огненные стрелы, броня, баннеры), `LevelUpUpgrade` | 15 | M |
| Э3.10 | Эмоции: страх и оглушение (по правилам 2.22 — до 3-го уровня) | 26 | M |
| Э3.11 | Гарнизон, ворота и стены, осада | 23, 24, 25 | L + L |
| Э3.12 | AI на данных BFME1 (`PlayerAIType`, `aidata.ini`), применение сил и героев | 29 | L–XL |
| Э3.12a | Остаток ScriptEngine BFME после минимума Э3.6a: прочие из 126 действий и 41 условия. Поток C, вне критического пути 1v1 | 30 | остаток от 15–30 |
| Э3.13 | Rohan и Isengard: их уникальные модули (по datacheck) | — | M–L |

Итого Э3 ≈ 107–165 чел.-дней (было 90–130; +15–30 скрипты карт, +2–5 продажа и падение дохода), из них поток C — все 15–30 скриптов. Минимум до точки «первый матч» по базе и бою (Э3.1–Э3.6) ≈ 36–53 чел.-дня плюс минимум скриптов C (Э3.6a): дней на него нет, пока нет списка карт 1v1 2.22. На критическом пути к «первому матчу» из скриптов остаётся только этот минимум (§7.3a). Точный список — после datacheck по `_patch222maps.big` (§6 Форматам №9). Остальные действия и условия в этот путь не входят.

**Что сознательно откладываю за Э3:** Кольцо (`OneRingPenaltyUpdate`), Living World, `DynamicPortalBehaviour`, `CivilianSpawn*`, `CritterEmitterUpdate`, `DelayedLuaEventUpdate`, редкие силы (по 1 использованию).

---

## 4. Детерминизм: правила для всего геймплейного кода

Контекст. Lockstep ZH обменивается только командами, рассинхрон ловится по CRC (`GameLogic.cpp` `getCRC(CRC_RECALC)`, интервал по NET-DESIGN §3.2 — N=30 кадров в release, N=1 в QA). Все игроки на одной сборке arm64 (ADR-0002 §2 п. 1: Android только `arm64-v8a`), Linux x86_64 — для CI и десктопной отладки. Значит, главная цель — **одинаковый результат на всех arm64-телефонах и в CI-прогоне реплеев**. Совпадение headless Linux x86_64 и arm64 нужно для CI и отладки; эмулятор x86_64 не используем (ADR-0002 §2 п. 1, ADR-0003 §1 п. 2). На устройстве прогон — Pixel 8 Pro. Детерминизм ARM по NET-DESIGN §3.2 — зона Лида и QA, сеть только детектирует. Ниже мои правила для `engine/bfme/` и предложения к общей сборке.

**Обновление 02.10.2026.** Лид принял решения по математике, флагам, flush-to-zero и `long` (пункты 2, 3, 7, 8). Обязательная редакция правил теперь в **adr/ADR-0003-determinism.md** (статус «Предложено»): при расхождении с этим разделом действует ADR. Главное: правила обязательны для всего кода логики, в том числе апстримного, а не только для `engine/bfme/`. Во всех целях и на всех платформах — `-ffp-contract=off -fno-strict-aliasing -fwrapv`. Флаги уже есть в `cmake/compilers.cmake` (строки 58 и 69, включая `-fno-delete-null-pointer-checks`). **r3:** `SAGE_USE_DETERMINISTIC_MATH` остаётся OFF, пока своя GameMath на musl (E1-49, Геймплей, 5–7 дн.) не готова; GameMath только в логике. FPCR/MXCSR — по R1 из `docs/fpcr-audit.md` (§4.1 п. 1). `long`, `long double` и `lroundf` в логике запрещены.

### 4.1 Плавающая точка

**Решение: строгий IEEE-754 single (float), без fixed-point.** Вся логика ZH на `Real` = float: позиции `Coord3D`, здоровье, урон, скорость. Подсчёт `\bReal\b` на `f79e624` (ADR-0003 §1 п. 3): `PhysicsUpdate.cpp` 145, `Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp` 145, `Locomotor.cpp` 252, `Weapon.cpp` 89. Перевести движок на fixed-point — это XL-переписывание без гарантии успеха. Для нового кода BFME fixed-point тоже не берём: он смешивался бы с float движка на каждой границе. Исключение — **счётчики** (деньги, командные очки, опыт, очки сил, кадры, таймеры): только целые.

Правила:
1. **Флаги компилятора для всего симуляционного кода** (уже в MYSOREZ, `src/GeneralsZH-Android-Port/cmake/compilers.cmake`, ветка не-MSVC): `-ffp-contract=off` (запрет слияния `a*b+c` в FMA), а также `-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks` (там же, с пояснением: движок читает float через `*(unsigned*)&f` и полагается на переполнение Int). В TSH (`src/GeneralsGameCode/cmake/compilers.cmake`) этих флагов **нет**, поэтому при cherry-pick из TSH их нельзя потерять (ADR-0001 §5: из TSH только `cherry-pick -x`, изменения детерминизма идут через QA). Мой модуль `engine/bfme/` наследует их, а CI проверяет флаги в `compile_commands.json`.
   - Почему это важно на arm64: clang по умолчанию (`-ffp-contract=on`) сливает `a*b+c` внутри выражения в `fmadd` на AArch64, где FMA есть всегда, а на x86_64 без `-mfma` — нет. Результат отличается в последнем бите, и CRC расходится между CI (x86_64) и телефоном, а при разной версии NDK — и между сборками.
   - Запрещено: `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations`, `-ffinite-math-only`, `-freciprocal-math`, `-fassociative-math` (поиск по `CMakeLists.txt`/`*.cmake` MYSOREZ ничего такого не нашёл). Векторизация (`-O2`, `-O3`) допустима, если не включает переассоциацию сумм: clang без fast-math этого не делает.
   - x86_64: только SSE2 (по умолчанию). Никакого x87 (`-mfpmath=387`) и `-march=native`. В ZH есть x87-путь `#if defined(_MSC_VER) && defined(_M_IX86)` (`fld/fsin/fsqrt/fistp` в `Core/Libraries/Source/WWVegas/WWMath/wwmath.h`). Файла `Core/Libraries/Include/Lib/BaseFunctions.h` в дереве нет (ADR-0003 §2.4). На наших сборках x87-путь не компилируется.
   - Denormals: не включаем flush-to-zero (`FPCR.FZ` на arm64, `MXCSR.FTZ/DAZ` на x86), режим округления только round-to-nearest. **Решено** (Лид, ADR-0003; R1 из `docs/fpcr-audit.md:55–57`): регистры у каждого потока свои, опасен только поток логики; SDL3, звук и драйвер проверяет Android-платформа, RAII-страж `fp::Guard` (R2) и запрет `-ffast-math` в CI (R4) — тоже она. Геймплей в E1-49 делает R1: `fp::checkAndReset("tick-start")` в начале `GameLogic::update()` строится на имеющемся `setFPMode()` (вызов `setFPMode()` в `GameLogic::update` стоит на `:4126`, функция начинается на `:4110`, так что это не первая строка; `checkAndReset` заменяет этот вызов. Что `fesetenv(FE_DFL_ENV)` на bionic arm64 пишет FPCR = 0, в дереве не проверено: это вывод аудита, ADR-0003 §2.3): отклонение пишется в лог и сбрасывается. `fp::assertExpected("tick-end")` — в конце тика и в `getCRC`, фатально в CI и debug, в release — лог. Норма R3 (`fpcr-audit.md:67–69`): arm64 `FPCR == 0`; x86_64 `(MXCSR & 0xFFC0) == 0x1F80` и `(x87 CW & 0x0F3F) == 0x003F`. Интерфейс `fp::` уже согласован с Платформой: заголовок `Core/Libraries/Source/GameMath/include/fp.h` (`fpcr-audit.md:94–96`, §8).
2. **Трансцендентные функции только свои, одинаковые для всех платформ.** libm разный: bionic (Android), glibc (CI), UCRT (Windows). В ZH логика зовёт libm напрямую:
   - `GeneralsMD/Code/GameEngine/Source/Common/System/Trig.cpp`: на не-MSVC `sin`/`cos`/`tan`/`acos`/`asin` считаются в double и сужаются к `Real` (`Trig.cpp:80–122`). `sinf`/`cosf`/`tanf`/`acosf`/`asinf` остаются только в ветке `_MSC_VER && _M_IX86`. Расхождение `sinf` на 1,236% углов — замер DEV_BLOG (NET-DESIGN §2), не описание нынешней не-MSVC ветки (ADR-0003 §1 п. 4);
   - `Core/Libraries/Source/WWVegas/WWMath/wwmath.h`: `Atan`/`Atan2` → `atan`/`atan2` (double). Таблицы `_FastSinTable` заполняются при `WWMath::Init()`. Ветки `USE_DETERMINISTIC_MATH` — заглушки с откатом на CRT (ADR-0003 §1 п. 4);
   - `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/PhysicsUpdate.cpp:793` — `atan2(xvec.Z, xy)` напрямую.
   - **Исправление после проверки:** готовой GameMath в MYSOREZ нет. `cmake/gamemath.cmake` (опция `SAGE_USE_DETERMINISTIC_MATH`, по умолчанию OFF, строка 23) тянет `Core/GameMath` из TSH на коммите `838f9d0f`, но такого каталога там нет (`git ls-tree`). Ветки `#ifdef USE_DETERMINISTIC_MATH` в `wwmath.h` форка — заглушки `// TODO: return GameMath::…` с откатом на CRT. **Решено** (Лид, ADR-0003; r3): своя GameMath на libm из musl (MIT) в `Core/Libraries/Source/GameMath/` (E1-49, Геймплей, 5–7 дн.); `gamemath.cmake` переходит с FetchContent на `add_subdirectory`; `SAGE_USE_DETERMINISTIC_MATH` OFF до её готовности, затем ON во всех целях; обязательный побитовый тест на ≥ 10^6 входов на x86_64 и arm64 (qemu-aarch64); `Trig.cpp`, `wwmath.h`, `PhysicsUpdate.cpp:793` переводятся на неё только в логике, рендер и клиент остаются на libm. Для `engine/bfme/` — только `bfme::math::{sin,cos,atan2,acos,asin,pow,exp,log}` поверх той же GameMath. Прямой вызов libm в `engine/bfme/` ловит линтер в CI.
   - `sqrtf`, `+ - * /`, `floorf/ceilf/truncf`, `fabsf`, `fminf/fmaxf` по IEEE точны (корректное округление), их можно звать напрямую.
3. **Преобразования float→int.** `fast_float2long_round` лежит в `Core/Libraries/Include/Lib/BaseType.h:180` и возвращает `long`: 32 бита на Windows и 64 бита на arm64/x86_64 Linux. Ветка `_MSC_VER && _M_IX86` вызывает `lroundf` (`:190`). Не-Windows ветка (`:192–208`) `lroundf` не вызывает: берёт `roundf` и при выходе за диапазон возвращает 0. `REAL_TO_INT_CEIL`/`FLOOR` — там же (`:292–296`), пример в логике `Core/GameEngine/Include/GameLogic/AIPathfind.h:1015–1016`. Файла `BaseFunctions.h` в дереве нет (ADR-0003 §2.4). На ширину `long` MZ уже натыкался (NET-DESIGN §2). **Решено** (Лид, ADR-0003): `lroundf`, `lrintf`, `long`/`unsigned long` и `long double` (R6: на arm64 это программный fp128, на x86 — x87) в логике и сериализации запрещены; вместо них хелперы с `int32_t` в GameMath (определённое поведение на NaN/±inf/переполнении) и CI-греп по GameLogic. Никаких C-кастов float→int за пределами диапазона (это UB).
4. **Никакого `double` в логике BFME** без явной необходимости и без явного приведения обратно к float в той же точке. Смешанные выражения float/double дают разные результаты при разной оптимизации.
5. **Порядок операций с float не зависит от порядка контейнеров** (см. 4.3): сумма модификаторов `ModifierList` считается в фиксированном порядке (по имени/ID модификатора), а не по порядку прихода аур.
6. **Значения из INI парсим один раз и одинаково:** длительности → целые кадры с `ceil`. Обычный процент вроде `50%` ZH делит на 100 (`INI::parsePercentToReal`, `INI.cpp:1021–1026`). Для `Modifier = PRODUCTION` так нельзя: `1.2 %` — это множитель ×1,2, а `parsePercentToReal` дал бы 0,012 (§5 №10). Парсер не зависит от локали: **решено Форматами** — `std::from_chars`, без `strtof` (на Android локаль приложения может быть `ru_RU` с запятой).

### 4.2 Случайные числа

- В ZH три независимых генератора с общим алгоритмом (`Core/GameEngine/Source/Common/RandomValue.cpp`, `randomValue()` на 6 словах `UnsignedInt` с переносом):
  - `theGameLogicSeed` → `GetGameLogicRandomValue/…Real` (макросы `GameLogicRandomValue(lo, hi)`, `GameLogicRandomValueReal(lo, hi)` в `Core/GameEngine/Include/GameLogic/LogicRandomValue.h`). **Единственный допустимый источник случайности для логики.** Сид общий для матча: `InitRandom(seed)` из настроек игры (`SkirmishGameOptionsMenu.cpp:444` `TheSkirmishGameInfo->getSeed()`, сеть `GeneralsMD/Code/GameEngine/Source/GameNetwork/GameSpyGameInfo.cpp:566` `InitGameLogicRandom(...getSeed())`, реплей `GeneralsMD/Code/GameEngine/Source/Common/Recorder.cpp:1586` `InitRandom(m_gameInfo.getSeed())`). CRC сида — `GetGameLogicRandomSeedCRC()`, он входит в CRC логики.
  - `theGameClientSeed` → `GameClientRandomValue` (`Core/GameEngine/Include/GameClient/ClientRandomValue.h`): только визуал. Его можно вызывать сколько угодно раз, у каждого игрока по-разному, поэтому **результат никогда не должен влиять на состояние логики**.
  - `theGameAudioSeed` → звук.
  - TSH добавил `GameLogicRandomValueUnchanged/RealUnchanged`: значение из логического сида **без сдвига** состояния, для «желательно одинаково, но не критично» (скриптовый звук). В логике BFME не используем.
  - `InitRandom()` без аргумента сидит от `time(nullptr)`: в матче его вызывать нельзя.
- В логических файлах ZH 19 вызовов `GameClientRandomValue` / `GameClientRandomValueReal` на 17 строках (`ActiveBody.cpp:1025` — комментарий, не вызов; ADR-0003 §2.5 п. 5) (например, `Object/Update/AutoDepositUpdate.cpp:190` — позиция текста денег, `Object/Body/ActiveBody.cpp:1026` — кость для эффекта с комментарием «Moved back to GameClientRandomValue because of desync problems», `StealthUpdate.cpp:135`, `StructureCollapseUpdate`, `StructureToppleUpdate`, `EMPUpdate`). Это допустимо, только пока результат идёт в клиент. **Решено** (ADR-0003 §2.5 п. 5, влит `c70bfa1`): обе CRC строятся только через `crc()`, не через `xfer()` модулей. Флага `Xfer::isLogicCRCDump` нет. Поля клиентского RNG в `crc()` не пишем, в `xfer()` они остаются. `m_pulsePhase` (`StealthUpdate.cpp:135`) вне обеих CRC и на логику не влияет: QA закрыл это 02.10.2026, перевод на `GameLogicRandomValue` не нужен. `m_nextBurstFrame` тоже вне CRC и остаётся в сейве (`StructureToppleUpdate.cpp:610`). `SpectreGunshipUpdate.cpp:656–657` по-прежнему на разборе с QA. В ADR-0003 список — 19 вызовов на 17 строках, а не «14 визуальных и три на проверку».
- Правила для `engine/bfme/`:
  1. Случайность в логике — только `GameLogicRandomValue*`. Запрещены `rand()`, `std::random_device`, `std::mt19937` без сида из матча, `GameClientRandomValue` в коде, который пишет состояние объекта/игрока. Линтер в CI.
  2. Число вызовов логического RNG не должно зависеть от клиента: нельзя звать его из кода, который исполняется только при видимости объекта на экране, только у локального игрока, только при включённом звуке и т.п. (классика рассинхрона в SAGE).
  3. Разброс членов орды (`HordeContain.RandomOffset`), разброс урона, шанс в `HitReactionBehavior`, задержки `BackUpMin/MaxDelayTime` — всё через логический RNG и только в update логики.
  4. Для отладки рассинхрона включать `DEBUG_RANDOM_LOGIC` (лог каждого вызова с файлом/строкой в `RandomValue.cpp`) в QA-сборке.

### 4.3 Порядок, адреса, время

1. **Никаких обходов hash-контейнеров в логике** (`std::unordered_map/set`, `hash_map`): порядок зависит от реализации STL и от адресов. В логике ZH я их не нашёл (`rg` по `GameLogic/`). Для BFME: `std::map`/`std::vector` с сортировкой по стабильному ключу (`ObjectID`, `NameKeyType`, индекс в INI).
2. **Никакого порядка по адресам указателей.** Ключи `std::map<T*, …>` и сортировки по `T*` запрещены, если по ним итерируются с влиянием на состояние. В ZH есть `AttackPriorityMap = std::map<const ThingTemplate*, Int>` (`GeneralsMD/Code/GameEngine/Include/GameLogic/ScriptEngine.h:102`): в логике чтение — `find` (`ScriptEngine.cpp:189`), это допустимо. Запись в сейв идёт в `AttackPriorityInfo::xfer` (`:292–307`): пары идут в порядке адресов. Отладочный дамп (`:206`) обходит так же. `AttackPriorityInfo::crc` (`:236`) пустой, как в ZH. Deep CRC карту не видит: `ScriptEngine::crc` (`ScriptEngine.cpp:8616`) пустой и `m_priorityMap` не хеширует. Обе CRC идут через `crc()`, не через `xfer()`. **Решено:** при записи в `xfer` пары сортируются по имени шаблона, загрузка и схема сейва не меняются (ADR-0003 §2.5 п. 2). Такой же шаблон в BFME допустим только для `find`.
3. **Порядок обхода объектов** — порядок списка объектов `GameLogic` (детерминированный, по созданию). Для орд: члены в порядке рядов из `RankInfo`, а не в порядке регистрации из `ProductionUpdate`.
4. **Запросы «ближайший/все в радиусе»** (`PartitionManager.cpp`) при равных расстояниях дают ничью: разрешать по `ObjectID`.
5. **Никакого времени стены в логике:** только номер кадра `TheGameLogic->getFrame()`. В `GameLogic.cpp` `timeGetTime()` стоит в загрузке объектов карты (`:2004`, `:2100`, `:2105`) и в таймауте прогресса загрузки сети (`:5384`, `:5395`, `:5425`), не в шаге симуляции (ADR-0003 §2.5 п. 3). В BFME запрещены `time()`, `clock()`, `std::chrono`, `timeGetTime` в коде логики.
6. **Никакого состояния из клиента в логике:** видимость камеры, LOD, FPS, настройки графики, «мобильный слой» UI. Мобильный слой INI (короткий матч) — часть данных матча, его хеш должен входить в проверку при старте (NET-DESIGN §3.2 «хэш данных»).
7. **Ленивая инициализация** (кэши, таблицы) — при загрузке, а не при первом вызове в матче, если от неё зависит результат.
8. **Каждый модуль BFME реализует `crc()` и `xfer()`** со всеми полями состояния логики (особенно таймеры, накопленные модификаторы, позиции в формации). Обе CRC, лёгкая и Deep, строятся только через `crc()`: у `XferDeepCRC` нет своего `xferSnapshot`, он наследует `XferCRC::xferSnapshot` (`XferCRC.cpp:110–121`), а тот зовёт `snapshot->crc()`. Флага `Xfer::isLogicCRCDump` нет. Инвариант: ни один `crc()` не вызывает `xfer()`. Исключение ZH — только `AIUpdateInterface::crc` (`AIUpdate.cpp:5086`, аргумент назван `x`, строка `xfer(x)`); для модулей BFME это не образец. Проход CI по телам `::crc(Xfer*)` — часть E1-49: он ловит вызов `xfer(` при любом имени аргумента. В allowlist явно стоит `AIUpdateInterface`, в `engine/bfme/` список пустой (ADR-0003 §2.5 п. 5). `m_pulsePhase` в CRC не входит и на логику не влияет (QA, 02.10.2026).

### 4.4 Проверка (вместе с QA)

- Реплей-тесты с Э2.1: на каждый новый модуль BFME — короткий реплей (2–3 мин) с CRC каждые N=1 кадр, прогон headless на Linux x86_64 (CI) и на arm64 (Pixel 8 Pro; эмулятора x86_64 нет), сравнение потоков CRC. Это критерий Э2 в PLAN.md.
- Тест «двойной прогон» на одном устройстве с разным порядком загрузки ассетов и разным разрешением/LOD: CRC должны совпасть.
- Линтер `engine/bfme/`: запрет `sinf|cosf|tanf|atan2|acos|asin|powf|expf|logf|rand\(|GameClientRandomValue|unordered_|time\(|chrono|long double|lroundf|lrintf` в логических файлах (с явным списком исключений); греп по `GameLogic` на `long` — в E1-49. Туда же проход по телам `::crc(Xfer*)`: вызов `xfer(` при любом имени аргумента, allowlist явно содержит `AIUpdateInterface` (`AIUpdate.cpp:5086`), для `engine/bfme/` он пустой (§4.3 п. 8).
- Правила CRC (решение Лида, ADR-0003): расхождение CRC между архитектурами — Major, на одной архитектуре — блокер. E1-45 (сравнение архитектур) идёт после E1-49.

### 4.5 Известные проблемные места ZH (для QA и Лида)

| Что | Где | Почему риск |
|---|---|---|
| libm в тригонометрии логики | `Trig.cpp` на не-MSVC: double `sin`/`cos`/`acos`/`asin` (`:80–122`); `wwmath.h`: double `atan`/`atan2` | bionic ≠ glibc ≠ UCRT; готовой GameMath в MYSOREZ нет; своя на libm из musl — E1-49, флаг OFF до неё (ADR-0003) |
| прямой `atan2` в физике | `GameLogic/Object/Update/PhysicsUpdate.cpp:793` | то же, плюс double |
| `lroundf` → `long` | `Core/Libraries/Include/Lib/BaseType.h:180` (`fast_float2long_round`; `lroundf` только в ветке MSVC x86, `:190`) | ширина `long` и поведение на NaN различаются по платформам (MZ: NaN в REAL_TO_INT_FLOOR). `BaseFunctions.h` в дереве нет |
| чтение float через `*(int*)&f` | `wwmath.h` (`Fabs`, `Float_To_Int_Floor/Chop`, `Is_Valid_Float`) | strict aliasing; закрыто флагом `-fno-strict-aliasing` в MYSOREZ. `BaseFunctions.h` в дереве нет (ADR-0003 §2.4) |
| FMA | любой float-код без `-ffp-contract=off` | флаг стоит в `cmake/compilers.cmake` (строки 58, 69); при слияниях с TSH следить, Платформа проверяет `compile_commands.json` |
| клиентский RNG в логических файлах | 19 вызовов на 17 строках; `StealthUpdate.cpp:135` — поле `m_pulsePhase` | в `crc()` своего модуля не пишется. `m_pulsePhase` вне CRC и на логику не влияет (QA, 02.10.2026). `SpectreGunshipUpdate.cpp:656–657` ещё на разборе с QA (ADR-0003 §2.5 п. 5) |
| float повсюду в физике и pathfinding | `PhysicsUpdate.cpp`, `AIPathfind.cpp`, `Locomotor.cpp` | детерминирован при правилах 4.1; на ретейле работал (x87/SSE одна сборка), на arm64 проверяют реплеи |

---

## 5. Главные риски геймплея

1. **Орды на pathfinding ZH** (§1 №8). Pathfinding ZH рассчитан на отдельные юниты и малые группы (`AIGroup.cpp`). Орда BFME1 — объект-контейнер с членами по слотам формации. Нужен ранний прототип (Э2.6, первые 2–3 недели Э2) и замер с QA: тик логики ≤ 10 мс p95 на Pixel 8 Pro в тяжёлой сцене на 4 игроков (§2.2 п. 3). Не влезаем — виртуальные члены (+8–12 дн.).
2. **ModifierList — системный слой** (№2), на нём держатся уровни, ауры, силы, баннеры, кузница. Ошибка в дизайне всплывёт везде, поэтому дизайн ревьюит Лид до кода.
3. **Объём** ≈ 190–280 чел.-дней (было 170–240; выросло за счёт скриптов карт, стоек, продажи) против 10–16 недель Э2+Э3 в PLAN.md. Второй исполнитель будет только при «идём» Э1 (§2.3). Без него прототип Э3 сдвигается. Мера: §3 режет до минимума 1v1, редкие модули — заглушки, работа делится на потоки A, B и C (§7).
4. **Данные карт 2.22 ещё не учтены.** Слой патча на боксе, datacheck прогнан с ним (§0), но `_patch222maps.big` пришёл позже прогона. Поэтому объекты карт 1v1 2.22, `CastleBehavior` на картах и скрипты карт 2.22 пока посчитаны только по базовому `maps.big` (§6 Форматам №9).
5. **Падение дохода от соседних построек — риск понижен.** Ни один источник не подтверждает его в BFME1 (`ResourceModifier*` в данных 0, `out-all/gameplay.md:89`). Радиусы `TerrainResourceBehavior` и `ResourceModifierValues` относятся к BFME2. В OpenSAGE доход = `DepositAmount` × `ProductionModifier` без соседей. Реверс exe не делаем. Замер (`docs/farm-income-measurement.md`) сначала проверяет, есть ли зависимость вообще; до ответа B8 считает доход без соседей. Дополнительные 1–3 дня нужны, только если зависимость найдётся. Остальные механики, которые r2 считал ненайденными (продажа, стойки, пополнение), найдены в данных (§2.4).
6. ~~Предел enum `VeterancyLevel`~~ — **снят**: отдельный счётчик BFME (§2.2 п. 2).
7. **GameMath пишем сами** на libm из musl (MIT), E1-49, 5–7 дн. в Э1 (§2.2 п. 8). Флаг OFF до её готовности, E1-45 (сравнение архитектур) идёт после неё. Если E1-49 задержится, до неё будет только контрольный прогон с флагами, а реплеи arm64 и x86_64 могут расходиться.
8. **INI 2.22 не загрузится, пока нет E1-56.** `BeginScript` есть только в Draw (5948 блоков в 2.22, из них 2092 прямо в модуле; `report.md` §8.1). Lua-`end` (1017 блоков) совпадает с концом блока ZH (`stricmp` с `End`, `INI.cpp:1642`), а `;` (531 блок) обрезает строку (`INI.cpp:602–606`). Сырое чтение должно сработать раньше пропуска неизвестных вложенных блоков. Плюс `#define` (10149 числовых подстановок), пропуск неизвестных блоков верхнего уровня (8045), `ChildObject` (609 через `ObjectReskin`) и пропуск модуля при `nullptr` из `ModuleFactory`. **Решено** (§2.2 п. 11): E1-56, 5–6,75 дня, от E1-21 (`E1-BACKLOG.md:102`). `BeginScript` нужен E1-34; E1-36 зависит от E1-56, потому что иначе не поднимется карта M1. В Behavior и Body скрипта нет.
9. **Ключа `ClientBehavior` в ZH нет** (`ThingTemplate.cpp:179–182`, ключ клиента — `ClientUpdate` на `:182`). **Решено** (§2.2 п. 12): это синоним `ClientUpdate`, одна строка `MODULETYPE_CLIENT_UPDATE` с пометкой `// BFME:`. Четыре заглушки делает Геймплей в `BfmeModuleRegistry`. В CRC логики не входят, сохраняются с `Drawable`.
10. **`Modifier = PRODUCTION 1.2 %` — множитель ×1,2, не +1,2 %.** Гипотеза высокой уверенности: по данным Форматов, `report.md` §9; подтвердить замером (`docs/farm-income-measurement.md`). Ферма и кузница Гондора уровня 3 в 2.22 дают ×1,23 (`gamedata.ini:1457`, `:1481`; `attributemodifier.ini:1167`, `:1179`). `INI::parsePercentToReal` (`INI.cpp:1021–1026`, деление на 100 в `scanPercentToReal`, `:1887`) и OpenSAGE прочли бы 0,012. Так делать нельзя (§1 №13, §6 Форматам №10).
11. **Командные очки зависят от режима.** Наборы 2.22 сверены с `report.md` §4 (§1 №6): эталон 1v1 — MP2 230/460 (`gamedata.ini:306–307`). MP3 и MP4 — 200/400 (`:309–310`, `:312–313`), MP56 — 160/320 (`:315–316`), MP78 — 150/300 (`:318–319`), SOLO — 130/240, ИИ — 900/1500, бонусы кампании — 600/1400 (`:282–283`). Горный тролль стоит 15 CP (в 1.03 было 10). Оговорка: откуда движок берёт набор по числу игроков, Форматы ещё показывают. До этого риск — взять SOLO, MP4 или бонусы кампании вместо MP2.
12. **Скрипты карт — поток C** (15–30 дн., §1 №30, §7.3a). На критическом пути 1v1 только минимум скирмиш-карт; список после datacheck по `_patch222maps.big`. Остальное в путь «первого матча» не входит. Пока списка нет, дни минимума не назначены, и срок матча может сдвинуться.

---

## 6. Открытые вопросы

Статус на 02.10.2026, вечер (r3 и решения Лида и Оркестратора того же дня). Закрытые вопросы оставлены для истории, ответ указан рядом. `docs/E1-BACKLOG.md` здесь не менялся. Цитаты строк сверены с v3.5 (`32cd01aa4`): E1-56 `:102`, E1-56b `:103`, E1-34 `:111`, E1-36 `:113`, E1-37 `:114`, E1-49 `:127`.

**Лиду движка:**
1. **Открыт.** Подтверждаем ли логику 30 Гц для BFME1 (ADR-0002 §7) как окончательную? С моей стороны возражений нет (§2.1), это закрывает вопрос NET-DESIGN §3.0. Отдельного ответа ещё нет; ADR-0002 §7 и ADR-0003 исходят из 30 Гц.
2. ✅ **Решён, решение изменено** (ADR-0003, §2.2 п. 8). Включаем ли `SAGE_USE_DETERMINISTIC_MATH`? — OFF до готовности своей GameMath (E1-49), затем ON во всех целях; на GameMath переводим только логику. (В r2 стояло «ON с Э1».)
3. ✅ **Решён** (§2.2 п. 2, п. 9). Как расширяем уровни? — Enum не трогаем, отдельный счётчик BFME 1–10 с `xfer`/CRC, одностороннее отображение в `VeterancyLevel`.
4. ✅ **Решён** (§2.2 п. 1). Точка расширения фабрики и `DEBUG_CRASH`? — Явный реестр, `BfmeModuleRegistry::registerAll(ModuleFactory&)` в конце `ModuleFactory::init()`; мягкий режим только под `BFME_SOFT_MODULES` в dev.
5. ✅ **Решён** (§2.2 п. 3). Орда? — Прототип на настоящих объектах, решение по замеру (≤ 10 мс p95, Pixel 8 Pro, 4 игрока), иначе виртуальные члены.
6. ✅ **Решён** (§2.2 п. 4). Кто владеет `ModifierList` и нагетами? — Геймплей, хуки `// BFME:` в `Weapon.cpp`/`Damage.cpp` на ревью Лида.
7. ✅ **Решён** (Оркестратор, §2.3, §7). Второй исполнитель? — К старту Э2 на поток B, если Э1 пройдёт точку «идём».
8. ✅ **Решён** (ADR-0003, §4.1 п. 1). FPCR/MXCSR? — R1 из `fpcr-audit.md` делает Геймплей в E1-49; R2 и R4 — Платформа.
9. ✅ **Решён** (§2.2 п. 8). Источник GameMath? — libm из musl (MIT), `Core/Libraries/Source/GameMath/`, E1-49 за Геймплеем, 5–7 дн.
10. ✅ **Решён** (§2.2 п. 8). E1-45? — Идёт после E1-49; до неё только контрольный прогон с флагами.
11. ✅ **Решён** (§2.2 п. 6). Draw-модули? — Лиду 11 Draw (7 новых и 4 расширенных: Truck, Tank, Tree, Laser) и 24 модуля частиц. В Э1 Draw отображаются на `W3DModelDraw`; новые поля расширенных парсер пропускает с записью в лог; частицы пропускаются, в лог одна строка со списком. Источник истины — `docs/bfme-draw-modules.md`.
12. ✅ **Решён** (§2.2 п. 7). Хуки в `ProductionUpdate.cpp`, `Player.cpp`? — Та же процедура: `// BFME:`, `docs/UPSTREAM-PATCHES.md`, ревью Лида, PR в `bfme`.
13. ✅ **Решён** (§2.2 п. 13). Счёт: 124 = 113 (108 Behavior и 5 Body, реестр Геймплея) + 11 Draw (реестр Лида). 4 заглушки `ClientBehavior` идут сверх 124, критерий E1-50 их тоже учитывает. Строку E1-50 в бэклоге правит Лид.
14. ✅ **Решён** (§2.2 п. 12). `ClientBehavior` — синоним `ClientUpdate`: одна строка `MODULETYPE_CLIENT_UPDATE` с `// BFME:`. Строку и 4 заглушки делает Геймплей в `BfmeModuleRegistry` из `ModuleFactory::init`. Это не Draw. В CRC логики не входят, сохраняются с `Drawable`.
15. ✅ **Решён, объём расширен Лидом** (§2.2 п. 11, бэклог v3.5 `E1-BACKLOG.md:102`). E1-56 — 5–6,75 дня, от E1-21: сырой `BeginScript` (5948 блоков, не 2092) и загрузка INI 2.22. `BeginScript` нужен E1-34. Связь E1-36 → E1-56 остаётся: без E1-56 карта M1 не поднимется (`E1-BACKLOG.md:113`). В Э1 текст скрипта есть только у Draw.

**Форматам и данным:**
1. ✅ **Решён** (§2.3). Какая версия в `ini.big`? — Базовый слой; 2.22 в `_patch222*.big`, теперь на боксе и в datacheck (`out-all`).
2. ◐ **Решён частично** (§1.9). Разбивка по объектам, доступным фракциям в скирмише, и по итоговым объектам со слоем 2.22 есть (`out-all/gameplay.md` §2). Нет разбивки по картам 1v1 2.22 — см. №9.
3. ✅ **Решён** (§2.4). Кто разбирает `.bse`? — Форматы, вместе с E1-25.
4. ✅ **Решён** (§2.4). Схемы полей модулей? — У Форматов в `formats/`, поведение у Геймплея.
5. ✅ **Решён** (§2.4). Наследование и локаль? — `ChildObject` с заменой и удалением по тегу, частоты по итоговому объекту; числа через `from_chars`.
6. ✅ **Решён** (§2.4, `out-all/gameplay.md:7–84`). Продажа, стойки, пополнение — найдены (продажа только построек).
7. ✅ **Решён** (§2.4, §2.3, §5 №5). Падение дохода фермы от соседей в BFME1 ни один источник не подтверждает. Радиусы — BFME2, OpenSAGE считает без соседей. Замер только проверяет, есть ли зависимость (§6 Оркестратору №2).
8. ✅ **Решён** (§2.3). `_patch222*.big` на боксе.
9. **Новый.** Прогнать datacheck ещё раз с `_patch222maps.big` (пришёл после прогона) и дать: список карт 1v1 2.22, частоты модулей по объектам этих карт, `CastleBehavior` на картах и пересчёт 126/41 действий и условий скриптов по картам 2.22 и `_patch222libraries.big`. Из этого списка нужен минимум скриптов скирмиш-карт для потока C (§7.3a): без него дни C1 не назначаются.
10. ◐ **Гипотеза высокой уверенности** (§5 №10, `report.md` §9). `PRODUCTION 1.2 %` — множитель ×1,2, не +1,2 %. Ферма и кузница уровня 3 в 2.22 — ×1,23 (`gamedata.ini:1456–1457`, `:1480–1481`; `attributemodifier.ini:1162`, `:1167`, `:1174`, `:1179`). ZH `parsePercentToReal` и OpenSAGE дали бы 0,012 — так нельзя. Подтвердить замером. Способ проверки Форматов с Оркестратором ещё не согласован.
11. ✅ **Сверено** (`out-all/report.md` §4, строки отчёта 291–300). 600/1400 — бонусы кампании, блок `CAMPAIGN BONUS CAPS` на `gamedata.ini:281`, значения `:282–283`. Боевые наборы 2.22: SOLO 130/240 (`:294–295`), ИИ 900/1500 (`:301–302`), MP2 230/460 (`:306–307`), MP3 200/400 (`:309–310`), MP4 200/400 (`:312–313`), MP56 160/320 (`:315–316`), MP78 150/300 (`:318–319`).

**Оркестратору:**
1. ✅ **Решён** (§2.3, §7). Разбиение на потоки A и B принято в r3. Вечером скрипты вынесены в поток C.
2. ✅ **Решён** (§2.3, §5 №5). Падение дохода от соседних построек в BFME1 ни один источник не подтверждает: радиусы `TerrainResourceBehavior` и `ResourceModifierValues` — BFME2, в OpenSAGE доход = `DepositAmount` × `ProductionModifier` без соседей. Реверс exe не делаем. Замер по `docs/farm-income-measurement.md` ближе к Э3, с согласия Андрея, сначала проверяет, есть ли зависимость вообще. До ответа B8 считает доход без соседей. Риск понижен.
3. ✅ **Решён с оговоркой** (§2.3, §1 №6). Эталон 1v1 — MP2 230/460 (`gamedata.ini:306–307`). Наборы 2.22 уже сверены с `report.md` §4. Оговорка: откуда движок берёт набор по числу игроков, Форматы ещё показывают. SOLO 130/240 (`:294–295`) и ИИ 900/1500 (`:301–302`) — не эталон сетевого 1v1.

**QA:**
1. **Открыт.** Сценарий замера орды (§2.2 п. 3): тяжёлая сцена на 4 игроков — сколько батальонов, какие фракции, длительность, что считаем «тиком» (только `GameLogic::update()` или вместе с pathfinding в фоне). Нужен до прототипа Э2.6.
2. **Открыт.** Общий тестовый сценарий 1v1 для двух потоков (§7.5): карта и фиксированный сид.
3. ◐ **Частично закрыт.** `m_pulsePhase` (`StealthUpdate.cpp:135`) вне CRC и на логику не влияет (QA, 02.10.2026, ADR-0003): перевод на `GameLogicRandomValue` не нужен. Открыт разбор `SpectreGunshipUpdate.cpp:656–657`: если смещение влияет на логику, переводим на `GameLogicRandomValue`.

---

## 7. Потоки Э2/Э3

**Условия.** Разбиение A и B принято Оркестратором (r3). Вечером 02.10.2026 скрипты карт вынесены в поток C: исполнителя назначат на Э2/Э3 (§2.3). Поток A («юнит в бою») — мой, поток B уходит второму исполнителю на старте Э2, если Э1 пройдёт точку «идём». До этого я в Э1 делаю общую основу как задачи потока A: реестр модулей (E1-36, E1-50, E1-37; E1-36 зависит от E1-56, потому что иначе не грузится карта M1, §2.2 п. 11) и детерминизм E1-49 (GameMath на musl + FPCR R1). **Draw и частицы не входят ни в A, ни в B, ни в C:** их делает Лид (§2.2 п. 6, источник истины — `docs/bfme-draw-modules.md`), в том числе `W3DHordeModelDraw` для орд. Четыре `ClientBehavior` входят в реестр Геймплея, не в Draw (§2.2 п. 12). Оценки — в чел.-днях **[оценка]**. Недели считаются от старта Э2 (Н1 — первая неделя Э2), календарные даты назначит Оркестратор при пересмотре PLAN.md.

### 7.1 Разбиение

Предложено: поток A — орды, бой, нагеты, ModifierList, опыт и уровни, герои и силы; поток B — экономика, крепости и слоты, найм, CP, стены и осада, AI.

**Предлагаю изменённый вариант** с тем же смыслом: A — «юнит в бою», B — «игрок, база, прогрессия, AI». Переношу из A в B три вещи:

1. **Опыт и уровни (№4) → B.** Опыт — общий счётчик. Пишут в него оба потока: убийства идут из A, доход ферм (`AutoDepositUpdate`, `GiveNoXP`) — из B. Читают его найм и возрождение героев, `LevelUpUpgrade`, очки сил (`Rank`), то есть в основном B. Кроме того, у B появляется настоящая работа в Э2, пока крепость ждёт `.bse` от Форматов.
2. **Найм, лимит и возрождение героев (№17), очки сил и Spellbook (№19) → B.** Это `ProductionUpdate`, `Player`, кнопки крепости и `Science`, то есть тот же код, что найм и CP. **Способности героев (№18) и эффекты сил (№20) остаются в A**: они строятся на ModifierList, нагетах и областях действия.
3. **Смерть и обломки (№27) → B.** Это в основном здания: `StructureCollapseUpdate` 241, `RubbleRiseUpdate`, `RefundDie`.

| Вариант | Поток A | Поток B | Работа B в Э2 |
|---|---|---|---|
| исходный | ≈ 103–145 чел.-дн. | ≈ 61–90 | только задел на Э3 (крепость), цель Э2 целиком в A |
| предлагаемый (r2) | ≈ 79–115 | ≈ 80–114 | уровни, смерть и обломки, CP, крепость |
| принят, r3 | ≈ 83–121 (+ Э1: 10–15) | ≈ 97–149 | уровни, смерть и обломки, CP, крепость |
| **принято, вечер: C отдельно** | **≈ 83–121** (+ Э1: 10–15) | **≈ 82–119** | уровни, смерть и обломки, CP, крепость; скрипты 15–30 — поток C |

В r3 B включал скрипты карт (B19, 15–30), и из-за них поток был тяжелее. Вечером 02.10.2026 Оркестратор забрал скрипты в поток C (§7.3a): B стал ≈ 82–119, A остался ≈ 83–121 (+ Э1 10–15), C ≈ 15–30. Сумма Э2+Э3 по трём потокам ≈ 180–270, как у A+B в r3: скрипты перенесены, а не добавлены. На критическом пути 1v1 из C только минимум скирмиш-карт. Падение дохода от соседей в оценку B больше не заложено как вероятная работа: риск понижен (§5 №5), +1–3 дня у B8 только если замер найдёт зависимость.

Исполнители: **я беру поток A**. Орды и замер — главный риск, а бой ZH и OpenSAGE я уже разобрал. **Второй исполнитель берёт поток B.** Общая основа (реестр, E1-49) делается мной в Э1, до Н1, и дальше поддерживается мной. Таблица реестра генерируется из datacheck, поэтому каждый поток правит в ней только строки своих модулей.

### 7.2 Поток A: «юнит в бою» (Геймплей)

| ID | Задача | №§1 | Шаг §3 | Оценка | Зависит от |
|---|---|---|---|---|---|
| A0a | **E1-49:** GameMath на libm из musl в `Core/Libraries/Source/GameMath/`, побитовый тест x86_64/arm64 (qemu), перевод `Trig.cpp`/`wwmath.h`/`PhysicsUpdate.cpp:793` в логике, хелперы `int32_t`; **FPCR R1**: `fp::checkAndReset` в начале тика на базе `setFPMode`, `fp::assertExpected` в конце тика и в `getCRC`; CI-грепы `long`, `long double`, `lroundf`; проход по телам `::crc(Xfer*)` (allowlist — `AIUpdateInterface`) | §4 | Э1 (Э2.2) | 5–7 | E1-10; интерфейс `fp::` с Платформой |
| A0b | **E1-36 + E1-50:** реестр 113 логических имён и 4 `ClientBehavior` сверх 124, генератор таблицы из datacheck out-all, падение на дубле, `overrides`, `BFME_SOFT_MODULES`. `BeginScript` в этих модулях нет; зависимость от E1-56 — чтобы загрузились INI 2.22 и карта M1 | 1 | Э1 (Э2.1) | 3–4 + 1–2 | E1-56 (от E1-21, 5–6,75; `E1-BACKLOG.md:102`, `:113`) |
| A0c | **E1-37:** скрипты карт и AI в Э1 безопасно пропускаются | 30 | Э1 | 1–2 | E1-36 |
| | *Итого в Э1* | | | *10–15* | |
| A1 | ModifierList: API и базовая версия (урон, броня, скорость; на время и навсегда; фиксированный порядок применения) | 2 | Э2.5 | 4–6 | заголовок с Н1 (§7.5) |
| A2 | Хуки `// BFME` в `Weapon.cpp`/`Damage.cpp`, ревью Лида | 3 | Э2.3 | 1–2 | ревью Лида |
| A3a | Минимальный ближний бой: `DamageNugget`, `MeleeWeapon` | 3 | Э2.3 | 3–4 | A2 |
| A4 | **Прототип орды на настоящих объектах + замер с QA** (≤ 10 мс p95, Pixel 8 Pro, 4 игрока) → **решение** | 8 | Э2.6 | 6–8 | A3a, сценарий QA (§6 QA №1) |
| A5 | Орда базовая: ряды `RankInfo`, выпуск рядов, смерть членов, распад, `HordeAIUpdate`, `HordeMemberCollide`, минимум локомоторов | 5, 7, 8 | Э2.6 | 6–8 (+8–12 при виртуальных членах) | A4 |
| A3b | Остальные нагеты: `ProjectileNugget`, `HordeAttackNugget` | 3 | Э2.3 | 5–8 | A3a |
| A6 | Стрелки, `BezierProjectileBehavior` | 22 | Э2.3 | 3 | A3b |
| A7 | Конница, раздавливание, `MetaImpactNugget`, `ReplenishUnitsBehavior` (посадка на лошадей без седока) | 21 | Э2.7 | 6–8 | A5 |
| A8 | `DualWeaponBehavior`, `HitReactionBehavior` | 28 (часть) | Э2.8 | 3–5 | A3b |
| A16 | Стойки и команды орды: `TOGGLE_NO_AUTO_ACQUIRE` (Hold Ground), `HORDE_TOGGLE_FORMATION`; `Command_Guard` через `GUARD` ZH; флаги в `xfer`/CRC | 31 | Э2.6 | 2–4 | A5; кнопки — B (§7.4 п. 5) |
| | *Итого в Э2* | | | *39–56* | |
| A9 | ModifierList полностью: ауры `AttributeModifierAuraUpdate` (247), `AttributeModifierUpgrade` (у всех объектов; для кузницы B), пулы `AttributeModifierPoolUpdate`, `Modifier = PRODUCTION` для ферм B8 | 2 | Э3.9 | 4–6 | A1 |
| A10 | Бонусы орды, баннер, `LargeGroupBonusUpdate`; **пополнение батальонов**: `BannerCarrierUpdate`, колодцы `ReplenishNearbyHorde`, `HealSpellHordeReplenishPing` | 8, 9, 14 | Э3.9–3.10 | 4–6 | A5, A9 |
| A11 | Орда: комбо и сплит, отступление, орда в контейнерах (гарнизон, осадные машины) | 8 | Э3.11 | 6–10 | A5 |
| A12 | Страх и оглушение (`EmotionTrackerUpdate`, `EmotionNugget`) | 26 | Э3.10 | 5–7 | A5 |
| A13 | Способности героев (набор для 1v1) | 18 | Э3.7 | 10–15 | A1, B1 (уровни) |
| A14 | Эффекты сил | 20 | Э3.8 | 10–14 | A9, B12 |
| A15 | Остальное поведение юнитов | 28 | после «первого матча» | 5–7 | — |
| | *Итого в Э3* | | | *44–65* | |

Итого A: Э1 10–15, Э2 39–56, Э3 44–65; Э2+Э3 ≈ 83–121 (было 79–115).

**Критический путь A (Э2):** заглушки интерфейсов (Н1) → A2 → A3a → **A4: замер и решение (конец Н3 — Н4)** → A5 → реплей боя с совпадением CRC (QA) = цель Э2. Это ≈ 17–23 дн., то есть Н4–Н5. Хвост Э2 (A1, A3b, A6–A8, A16, ≈ 23–34 дн.) идёт параллельно критическому пути B к «первому матчу» и его не задерживает. В Э3 у A критический путь A9 → A13 → A14: герои и силы, от них зависит AI B17.

### 7.3 Поток B: «игрок, база, прогрессия, AI» (второй исполнитель)

| ID | Задача | №§1 | Шаг §3 | Оценка | Зависит от |
|---|---|---|---|---|---|
| B1 | Прогрессия: счётчик опыта и уровня 1–10, `xfer`/CRC, отображение в `VeterancyLevel`, `ExperienceLevel`, `LevelUpUpgrade`, `ExperienceLevelCreate` | 4 | Э2.4 | 5–7 | API ModifierList (заглушка) |
| B2 | Опыт из событий: убийство (`Object::scoreTheKill`, приёмник орды `setExperienceSink`), доход фермы | 4, 13 | Э2.4 | 1–2 | Horde API (A4), события |
| B3 | Смерть и обломки, модули BFME | 27 | Э2.8 | 3 | — |
| B4 | Командные очки и лимит: эталон 1v1 — MP2 230/460 (`gamedata.ini:306–307`, §2.3); остальные наборы — §1 №6 (SOLO 130/240, ИИ 900/1500, MP4 200/400). Горный тролль 15 CP (в 1.03 было 10), `CommandPointBonus` зданий | 6 | Э3.4 | 2–3 | откуда движок берёт набор по числу игроков — ещё не показано (§5 №11) |
| B5 | Найм: `ProductionUpdate` + `InitialPayload` через Horde API, очередь (пополнение батальонов — в A10) | 14 | Э3.4 | 3–5 | A5 |
| B6 | Крепость: `CastleBehavior` + `.bse`, `CastleMemberBehavior`, `KeepObjectDie` | 11 | Э3.1 | 8–12 | Форматы: `.bse` (вместе с E1-25) |
| | *Итого в Э2 (B6 и B4–B5 — задел на Э3)* | | | *22–32* | |
| B7 | Слоты застройки, `FoundationAIUpdate`, `GettingBuiltBehavior`, CommandSet радиального меню (UI — Дизайнер) | 12 | Э3.2 | 6–9 | B6 |
| B8 | Ресурсы: `AutoDepositUpdate` с полями BFME (17 за 5400 мс у фермы Гондора), рост по уровню через `PRODUCTION` (A1). Рабочая формула — `DepositAmount` × `ProductionModifier`, без соседей. Замер (`docs/farm-income-measurement.md`) сначала проверяет, есть ли зависимость; +1–3 дня только если она есть. Риск понижен | 13 | Э3.3 | 3 (+1–3 только если зависимость найдётся) | A1; замер ближе к Э3 (§5 №5) |
| B9 | Победа и поражение | 16 | Э3.5 | 2 | B6 |
| B10 | Простой AI: слоты по списку, ферма/казарма, волны батальонов | 29 (часть) | Э3.6 | 8–10 | B5, B7 |
| | **Точка «первый матч»** | | | | |
| B11 | Герои: найм, лимит 1, возрождение с сохранением уровня и опыта | 17 | Э3.7 | 5–7 | B1, B5 |
| B12 | Очки сил и Spellbook (ветка наук ZH, `Rank`) | 19 | Э3.8 | 5 | B1 |
| B13 | Апгрейды и кузница | 15 | Э3.9 | 4–6 | A9 |
| B14 | Гарнизон | 25 | Э3.11 | 2–3 | A11 |
| B15 | Стены и ворота | 24 | Э3.11 | 8–10 | правки pathfinding через A (§7.4 п. 9) |
| B16 | Осада | 23 | Э3.11 | 8–12 | A3b, A11 |
| B17 | AI на данных BFME1: применение сил и героев | 29 | Э3.12 | 7–15 | A13, A14, B11, B12 |
| B18 | Продажа построек: `Command_Sell` у 55 построек, 50 % (`SELL` и `BuildAssistant::sellObject` ZH), возврат слота | 12 | Э3.2 | 1–2 | B7 |
| | *Итого в Э3* | | | *60–87* | |

Итого B: Э2 22–32, Э3 60–87; Э2+Э3 ≈ 82–119. Скрипты карт (бывший B19, 15–30) ушли в поток C (§7.3a). Условные +1–3 дня B8 в 60–87 уже сидят как верх диапазона r3; отдельно к сумме их не прибавляю, и при пониженном риске они, скорее всего, не понадобятся.

**Критический путь B (до «первого матча»):** `.bse` от Форматов → B6 → B7 → B8 → B4 + B5 (нужна орда A5) → B9 → B10. Это ≈ 32–44 дн. от начала B6, без скриптов. К той же точке добавляется минимум скриптов C1 (§7.3a); его дни в 32–44 не входят. Если `.bse` готов к Н1, B начинает с B6, а B1 делает параллельно с ожиданием ревью. Если нет — сначала B1, B3, B4, а B6 стартует, как только Форматы отдадут `.bse`. Вся задержка `.bse` ложится на «первый матч» один в один.

### 7.3a Поток C: скрипты карт (исполнитель с Э2/Э3)

Оркестратор вынес скрипты из B. Исполнителя назначат к Э2/Э3. В Э1 неизвестные действия по-прежнему пропускает Геймплей (A0c, E1-37). Чанки `Teams`, `PlayerScriptsList`, `BuildLists` регистрирует E1-52 (Форматы).

| ID | Задача | №§1 | Шаг §3 | Оценка | Зависит от |
|---|---|---|---|---|---|
| C1 | Минимум скриптов скирмиш-карт 1v1: только действия и условия, без которых выбранная карта не играется. Список — после datacheck с `_patch222maps.big` | 30 | Э3.6a, на критическом пути к «первому матчу» | часть 15–30, отдельно не оценено | E1-52, список Форматов (§6 Форматам №9) |
| C2 | Остальные из 126 действий и 41 условия, включая скрипты AI вне минимума карты | 30 | Э3.12a, вне критического пути 1v1 | остаток 15–30 | C1 |
| | *Итого C* | | | *15–30* | |

Критический путь 1v1 удлиняется на C1. Дней на C1 нет, пока нет списка. C2 календарь «первого матча» не держит.

### 7.4 Точки стыковки

Правила для всех интерфейсов: вызовы синхронные, внутри тика логики, в порядке кода. Никаких очередей и подписок, упорядоченных по адресам (§4.3). Сигнатуры меняются только по согласию обоих потоков и записываются в журнал изменений (§7.5 п. 6).

| # | Интерфейс | Владелец | Кому нужен | Заглушка | Рабочая версия нужна к |
|---|---|---|---|---|---|
| 1 | **ModifierList** (`bfme::modifiers`: наложить и снять по источнику, множитель по категории, фиксированный порядок) | A | B: бонусы уровня (B1, `ExperienceLevel.AttributeModifiers`), кузница (B13, `AttributeModifierUpgrade`) | Н1: множитель всегда 1.0 | база A1 — к **Н3** (B1); ауры и апгрейды A9 — к **Э3.9** (B13) |
| 2 | **Horde API** (`bfme::horde`: создать орду из `InitialPayload`, член → орда, обход членов в порядке рядов, «член погиб», «орда распалась») | A | B: опыт орды (B2), найм (B5), AI (B10), гарнизон и осада (B14, B16) | Н1: орда — один объект без членов | после решения по замеру, к **Н4**; найм B5 — к **Э3.4** (≈ Н6–Н7). При переходе на виртуальных членов сигнатуры не меняются, меняется только реализация. Ради этого API и фиксируем в Н1 |
| 3 | **События логики** (`bfme::events`: `onDamage`, `onKill(killer, victim)`, `onLevelUp(obj, from, to)`, `onIncome(player, amount, source)`) | каркас — B; источники: `onDamage`/`onKill` — A (хуки `Damage.cpp`, `scoreTheKill`), `onLevelUp`/`onIncome` — B | A: способности по уровню (A13); B: опыт (B2) | Н1: пустые вызовы | `onKill` — к **Н2–Н3** (B2); `onLevelUp` — к **Э3.7** (A13) |
| 4 | **Прогрессия** (`bfme::progression`: уровень, опыт, `addXP`, отображение в `VeterancyLevel`) | B | A: способности героев (A13), бонусы по уровню через ModifierList | Н1: уровень всегда 1 | B1 — к **Н3**, герои — к **Э3.7** |
| 5 | **CommandSet и найм** (смена CommandSet, радиальное меню слотов, кнопки крепости, очередь `ProductionUpdate`, проверка CP перед наймом, кнопки героев и воскрешения) | B | A: кнопки способностей героев (A13); стойки и построения орды (A16: `TOGGLE_NO_AUTO_ACQUIRE`, `HORDE_TOGGLE_FORMATION`) | CommandSet ZH как есть | B7 — к **Э3.2**; кнопки способностей — к **Э3.7** |
| 6 | **Силы и способности**: исполнение через стандартный путь ZH (`SpecialPowerModule` + `MSG_DO_SPECIAL_POWER*`, `Core/GameEngine/Include/Common/MessageStream.h:547`) | A (эффекты) | B: AI (B17), Spellbook (B12 открывает силу через науку) | путь ZH уже есть; сила без эффекта = заглушка | первые силы — к **Э3.8**, все нужные AI — к **Э3.12**. AI зовёт силы только командой, как игрок, а не прямым вызовом эффекта: так реплей и сеть видят то же, что игрок |
| 7 | **Очки сил и науки** (очки покупки наук `Player`, `Rank`) | B | A: доступ к эффектам сил (A14) | механика ZH | **Э3.8** |
| 8 | **Урон по зданиям и стенам** (нагеты по постройкам, ворота, осадное оружие) | A (нагеты, `Damage.cpp`) | B: стены, ворота, осада (B15, B16) | — | A3b — к **Н4**; для осады — к **Э3.11** |
| 9 | **Pathfinding и стены** (`AIPathfind.cpp`, `WALK_ON_TOP_OF_WALL`, ворота) | A (движение орд); правки по заявке B | B15 | — | **Э3.11** |
| 10 | **Детерминизм** (ADR-0003, E1-49: GameMath на musl, хелперы `int32_t`, CI-грепы, FPCR R1 через `fp::`) | A (Э1) | оба | — | до **Н1** |
| 11 | **Реестр модулей** (113 логических имён и 4 `ClientBehavior` сверх 124, таблица из datacheck out-all; E1-36, E1-50, зависит от E1-56: загрузка INI 2.22, не текст `BeginScript`) | A (Э1); дальше каждый поток правит строки своих модулей. 11 Draw и 24 модуля частиц — реестр Лида (`docs/bfme-draw-modules.md`) | оба, плюс C для своих строк | — | до **Н1** |
| 12 | **Хуки в апстримных файлах** — у каждого файла один автор: A — `Weapon.cpp`, `Damage.cpp`, `AIPathfind.cpp`, `GameLogic.cpp` (FPCR); B — `ProductionUpdate.cpp`, `Player.cpp` (CP, очки сил), `AutoDepositUpdate.cpp`; реестр — `ModuleFactory.cpp` (я); `W3DModuleFactory.cpp` — Лид. Все хуки — `// BFME:`, строка в `docs/UPSTREAM-PATCHES.md`, ревью Лида, PR в `bfme` (§2.2 п. 7) | — | — | — | по мере задач |

### 7.5 Совместный старт: Н1, первые 3–5 рабочих дней вдвоём

1. **Заглушки интерфейсов** 1–4 из §7.4 (ModifierList, Horde API, события, прогрессия): заголовки в `engine/bfme/include/bfme/` с нейтральными реализациями. Сборка и CI-грепы ADR-0003 зелёные. Лид смотрит заголовки до появления кода. С этого момента оба потока пишут код против заголовков и друг друга не ждут.
2. **Карта владения файлами.** Подкаталоги `engine/bfme/`: A — `combat/`, `horde/`, `modifiers/`, `powers/`; B — `progression/`, `economy/`, `castle/`, `ai/`. Апстримные файлы — по §7.4 п. 12. Ни один файл не правят двое.
3. **Общий тестовый сценарий 1v1** (вместе с QA, §6 QA №2). Одна небольшая карта 1v1 из 2.22 (выбор после datacheck), Гондор против Мордора, фиксированный сид, реплей с CRC на каждом кадре (N = 1) на x86_64 и arm64. На одной карте три варианта:
   - «бой»: скрипт ставит по 2 батальона с каждой стороны (A, цель Э2);
   - «база»: крепость, слот, ферма, найм (B);
   - «мини-матч»: оба варианта вместе, собирается к «первому матчу».
   Каждый поток дополняет свой вариант и не трогает чужой. Тот же сценарий с 4 игроками — основа замера орды (A4).
4. **Отладочные команды** (только dev): поставить орду, дать денег, опыта или уровень, открыть силу. Их пишем вдвоём в первые дни, чтобы каждый мог проверять свою часть без готовой части другого.
5. **Соглашения:** версии `xfer` и порядок полей в `crc`, имена логов, формат отчёта о неизвестных полях (общий с реестром).
6. **Синхронизация:** короткий статус раз в день через Оркестратора и журнал изменений интерфейсов (подраздел в этом файле), куда записывается каждая смена сигнатуры.

### 7.6 Сводный критический путь и риски

- **Э2:** критический путь идёт через поток A (A2 → A3a → A4 → решение → A5 → реплей), ≈ 17–23 дн., Н4–Н5. B в Э2 не на критическом пути: он снимает работу с Э3 (B1, B3, B6).
- **Э3 до «первого матча»:** путь базы идёт через поток B (B6 → B7 → B8 → B4/B5 → B9 → B10), ≈ 32–44 дн. от начала B6. Он зависит от Форматов (`.bse`) и от A5 (найм орд). К той же точке добавляется C1 (минимум скриптов скирмиш-карт). Его длительность не входит в 32–44: список ещё не известен.
- **После «первого матча»:** A13/A14 (герои, силы), B15–B17 (стены, осада, AI) и C2 (остаток скриптов); B17 ждёт A13 и A14.
- **Календарь:** A ≈ 83–121 чел.-дня (плюс 10–15 в Э1), B ≈ 82–119, C ≈ 15–30. Сумма Э2+Э3 ≈ 180–270, как у A+B в r3. Против 10–16 недель Э2+Э3 в PLAN.md длиннее A и B (примерно 17–24 недели каждый при работе в одного). За Э3 при пересмотре плана можно вынести A11, A15, B15–B16 и часть B17. C2 уже вне критического пути 1v1. C1 на пути остаётся, пока Форматы не покажут состав минимума.
- **Риски стыковки:**
  - замер орды провален → виртуальные члены: +8–12 дн. в A, меняется реализация Horde API, но не сигнатуры;
  - задержка `.bse` → B6 стоит, B переключается на B1, B3, B4, B11;
  - очередь хуков на ревью у Лида — общая для A и B;
  - зависимость дохода фермы от соседей, если замер её всё же найдёт → +1–3 дн. у B8; рабочая гипотеза — её нет, риск понижен (§5 №5);
  - карты 1v1 2.22 ещё не в datacheck → список C1 и объекты карт уточнятся; до этого дни C1 не назначены.
