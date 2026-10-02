# GAMEPLAY-GAP: геймплей BFME1 против движка ZH и OpenSAGE

- **Автор:** Геймплей (39b6b49c-96d8-486c-b35d-e6eecb02f28e), 02.10.2026, 17:45 МСК; обновлено 02.10.2026, 18:00 МСК: решения Лида движка, Оркестратора и Форматов (§2.2–2.4), закрытые вопросы (§6), два потока Э2/Э3 (§7). Черновик для Оркестратора и Лида движка.
- **Связано:** PLAN.md §4 (Э2, Э3), ADR-0001-base-engine.md (наш код в `engine/bfme/`, правки апстрима только с пометкой `// BFME:`), ADR-0002-target-devices.md §7 (логика 30 Гц, только arm64), ADR-0003-determinism.md (правила детерминизма, «Предложено»), NET-DESIGN.md §1.5, §2, §3.2 (CRC, детерминизм).
- **Источники (проверено rg/grep на боксе):**
  - ZH: `src/GeneralsGameCode` (TheSuperHackers, коммит `2817000`, 02.10.2026). Пути ниже относительно этого каталога. Базовый форк `src/GeneralsZH-Android-Port` (MYSOREZ, `8ef2349`) использован для флагов сборки и `cmake/gamemath.cmake` (готовой GameMath в нём нет, см. §4.1 и ADR-0003 §1).
  - OpenSAGE: `src/OpenSAGE/src/OpenSage.Game` (master `588ac477`, 16.12.2025). Пути ниже относительно `OpenSage.Game/`.
  - LibreSAGE: рабочее дерево на боксе пустое (в индексе staged-удаления, его не трогал). По `git ls-tree HEAD` (4063 файла) кода под BFME нет: только общие модули Generals/ZH и обложки `Common/Tools/Launcher/assets/covers/bfme1.jpg`, `bfme2.jpg`. Для геймплея BFME1 **пользы нет**.
  - Данные: `gamedata/ini.big` (BIG4, 242 INI, SHA-256 `f5220a93…` по game-files-access.md). Читал **только в памяти** Python-скриптом, на диск ничего не распаковывал. **Это базовый слой.** Лимиты `GoodCommandPointLimit = 300`, `EvilCommandPointLimit = 600` в `data\ini\gamedata.ini` — базовые значения, а не признак версии 1.03. На ПК Андрея стоит 2.22: её INI лежат в архивах `_patch222*.big`, которые перекрывают базовый `ini.big`; на бокс их ещё не скопировали (ответ Оркестратора, 02.10.2026). Частоты ниже посчитаны по базовому слою. Баланс (цены, CP, урон) сверяем только по слою с патчем, после datacheck Форматов с учётом порядка `.big` (`docs/formats/bfme1-big-order.md`, E1-22).
- **Чем не является этот документ:** это не отчёт datacheck. Частоты ниже — мой быстрый подсчёт строк `Behavior/Body/Draw/ClientUpdate/ClientBehavior = <Модуль>` по всем INI из `ini.big`, без учёта наследования, `#include`, карт и того, какие объекты реально участвуют в 1v1. Когда «Форматы» выдадут datacheck, порядок в §3 уточню по нему.

Оценка объёма: **S** ≤ 2 чел.-дня, **M** 3–7, **L** 8–15, **XL** > 15 (чел.-день = день работы одного агента с ревью). Это **[оценка]**, сделанная без прототипа.

---

## 0. Сводка по данным (ini.big, подсчёт Геймплея)

- 2734 определения `Object/ChildObject/ObjectReskin`. Строк подключения модулей 10 220, разных модулей 197 (из них 7 — это `Draw` частиц в FXParticleSystem: `DefaultDraw`, `RenderObjectDraw`, `StreakDraw`, `LightningDraw`, `ButterflyDraw`, `QuadDraw`, они к объектам не относятся).
- **Модули, которые уже есть в ZH:** 67 разных, 4683 подключения (≈ 51% от 9252 подключений без частиц). **Модулей, которых нет в ZH:** 123 разных (без частиц), 4569 подключений, из них 2348 приходится на `W3DScriptedModelDraw` (рендер, область Лида и Форматов).
- Сверка с фабриками ZH: `GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp` (`ModuleFactory::init`, 204 `addModule`) + `Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp` (Draw/ClientUpdate). **Важно:** `ModuleFactory::findModuleTemplate` на неизвестном имени делает `DEBUG_CRASH("Module name '%s' not found")` и возвращает `nullptr`. Значит, с первого дня нужна регистрация заглушек для всех 123 имён, иначе объекты BFME1 не загрузятся. **Решено** (§2.2 п. 1): явный реестр в `engine/bfme/`, таблица имён генерируется из datacheck.
- Сверка с OpenSAGE: таблица разбора `Logic/Object/BehaviorModule.cs` (`BehaviorParseTable`, 335 имён) + `Logic/Object/Body/BodyModule.cs` + Draw/ClientUpdate. Атрибут `[AddedIn(SageGame.Bfme)]` стоит у 97 модулей без аналога в ZH, но **поведение (класс с `CreateModule` и `Update`) есть только у ~12 из них**, остальное только разбор полей. Атрибуты AddedIn в OpenSAGE местами неточны (например, `MetaImpactNugget` помечен Bfme2, но 63 раза встречается в INI BFME1).
- Блоки верхнего уровня BFME1, которых нет в ZH `Core/GameEngine/Source/Common/INI/INI.cpp` (`theTypeTable`), с частотой в ini.big: `ExperienceLevel` 412, `ModifierList` 290, `LargeGroupAudioMap` 117, `EmotionNugget` 15, `PlayerAIType` 15, `FactionVictoryData` 14, `LivingWorld*` (кампания), `ChildObject`, `CrateData` и др. Блоки, которые есть в ZH, но с новыми полями BFME: `Object` 1795, `Weapon` 295, `SpecialPower` 105, `Upgrade` 149, `Science` 45, `Rank` 85, `Locomotor` 80, `PlayerTemplate` 7.
- **Оружие BFME1 построено на «нагетах»** (в ZH такого нет вообще): `DamageNugget` 258, `ProjectileNugget` 88, `MetaImpactNugget` 63, `BuffNugget` 23 (это нагет FXList, а не оружия: `FX/FXNuggets/BuffNugget.cs`, клиентский эффект), `HordeAttackNugget` 12, `SpawnAndFadeNugget` 3, `AttributeModifierNugget` 2, `GrabNugget` 2, `SpecialModelConditionNugget` 2, `DamageFieldNugget` 1.
- Поля объектов: `BuildCost` 358, `BuildTime` 351, `CrushableLevel` 291, `CommandPoints` 191, `CrusherLevel` 122, `EmotionRange` 69, `IsTrainable` 51, `CrushRevengeWeapon` 32.
- Скрипт подсчёта и результат лежат вне репозитория: `/workspace/gp-scratch/bigcount.py`, `/workspace/gp-scratch/freq.tsv` (там только имена модулей и числа, без контента EA).

---

## 1. Таблица пробелов по системам

Статусы: **ZH** — есть в ZH, берём как есть (иногда с новыми полями); **≈ZH** — похожее есть, переиспользуем; **0** — пишем с нуля. Частота — число подключений модуля в ini.big.

### 1.1 Ядро, без которого BFME1-объект не живёт

| # | Система / модули BFME1 (частота) | Статус | ZH: где | OpenSAGE: где и насколько готово | Объём | Риски |
|---|---|---|---|---|---|---|
| 1 | **Заглушки и регистрация всех модулей BFME1**: 123 имени, отчёт о неподдержанных | 0 | `ModuleFactory::init`, `findModuleTemplate` (DEBUG_CRASH на неизвестном), `ModuleFactory::newModuleDataFromINI` | имена и поля: `Logic/Object/BehaviorModule.cs`, `Body/BodyModule.cs` | M (5–6 дн. с генератором таблицы из datacheck и флагом `BFME_SOFT_MODULES`) | **Решено** (§2.2 п. 1, §2.4): явный реестр всех 123 имён, `BfmeModuleRegistry::registerAll(ModuleFactory&)` в конце `ModuleFactory::init()`; схемы полей у Форматов (`formats/`), поведение у меня (`engine/bfme/`). В CI неизвестное имя — ошибка |
| 2 | **AttributeModifier / ModifierList** (290 блоков; `AttributeModifierAuraUpdate` 181, `AttributeModifierUpgrade` 40, `AttributeModifierNugget` 2, `AttributeModifierPoolUpdate` 1, а также `ExperienceLevel.AttributeModifiers`, `HordeContain.AttributeModifiers`). На этом держатся лидерство героев, бонусы орд, баннеры, ауры, бафы сил | ≈ZH | только бонусы оружия: `WeaponBonusUpdate.cpp`, `WeaponBonusUpgrade.cpp`, `WeaponBonusSet` в `Weapon.cpp`; бонус «орды» ZH: `HordeUpdate.cpp` | `Logic/ModifierList.cs` (разбор: категории, типы модификаторов, длительность), модули только данные | **L** (8–12 дн.) | ключевой системный слой: множители урона, брони, скорости, обзора, опыта. Порядок применения и округление влияют на CRC (§4). **Решено** (§2.2 п. 4): владелец — Геймплей, логика в `engine/bfme/`, хуки `// BFME` в `Weapon.cpp`/`Damage.cpp` на ревью Лида |
| 3 | **Оружие на нагетах**: `DamageNugget`, `ProjectileNugget`, `MetaImpactNugget` (отбрасывание), `HordeAttackNugget`; поля `MeleeWeapon`, `HitStoredTarget`, `CanFireWhileMoving`, `FinishAttackOnceStarted` и др. (31 поле BFME в `WeaponTemplate.cs`) | ≈ZH | `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Weapon.cpp`, `WeaponSet.cpp`, `Core/GameEngine/Source/GameLogic/System/Damage.cpp`, `Object/Armor.cpp`, снаряды `DumbProjectileBehavior` | `Logic/Object/Weapon/WeaponEffects/*.cs` (678 строк): `Execute` только у `DamageNugget`, `ProjectileNugget`; `MetaImpactNugget` только разбор | **L** (10–15 дн.) | ближний бой, отбрасывание (физика, float), кривая `BezierProjectileBehavior` (92). Без этого нет боя Э2. **Решено** (§2.2 п. 4): владелец — Геймплей, минимальные хуки `// BFME` |
| 4 | **Опыт и уровни** (`ExperienceLevel` 412 блоков: до 10 уровней у героев, 5 у юнитов, `RequiredExperience`, `ExperienceAward`, `Upgrades`, `AttributeModifiers`, `LevelUpFx`; `LevelUpUpgrade` 34, `ExperienceLevelCreate` 9, `ShareExperienceBehavior` 1; фермы получают опыт от дохода, см. `GiveNoXP`) | ≈ZH | `GameLogic/Object/ExperienceTracker.cpp`; уровни жёстко заданы enum `VeterancyLevel` (REGULAR..HEROIC, 4 шт.) в `Core/GameEngine/Include/Common/GameCommon.h`; `VeterancyGainCreate.cpp`, `ExperienceScalarUpgrade.cpp` | `Logic/ExperienceLevel.cs` (разбор), `Logic/Object/ExperienceTracker.cs`, `Create/ExperienceLevelCreate.cs` (15 строк) | M (5–7 дн.) | **Решено** (§2.2 п. 2): enum `VeterancyLevel` не трогаем; отдельный счётчик BFME (опыт и уровень 1–10, `xfer` и CRC), одностороннее отображение в `VeterancyLevel` для старых эффектов ZH |
| 5 | **Локомоторы BFME** (22 новых поля: `FormationPriority`, `ChargeAvailable`, `ChargeSpeed`, `BackingUpSpeed`, `SlowTurnRadius`/`FastTurnRadius`, `WaitForFormation` и др.) | ≈ZH | `GameLogic/Object/Locomotor.cpp` (244 упоминания `Real`) | `Logic/Object/LocomotorTemplate.cs` (только разбор) | M (4–6 дн.) | движение строем и разворот орды зависят от этих полей |
| 6 | **Командные очки (CommandPoints)**: поле объекта (191), `CommandPointBonus` (здания поднимают лимит), лимиты в `gamedata.ini` (`GoodCommandPoints`, `GoodCommandPointLimit`, `...Bonus`, `...AI`, `...MP2` и т.д.) | 0 | лимита юнитов в ZH нет; ближе всего проверки в `Common/System/BuildAssistant.cpp`, `Common/RTS/ProductionPrerequisite.cpp`, `MaxSimultaneousOfType` в `Core/GameEngine/Source/Common/Thing/ThingTemplate.cpp`, а схема «здание даёт ресурс игроку» как у энергии `Common/RTS/Energy.cpp` | поля `CommandPoints`, `CommandPointBonus` в `Logic/Object/ObjectDefinition.cs`; **логики лимита в Player.cs нет** | S–M (2–3 дн.) | 300/600 — базовый слой; числа 2.22 берём из `_patch222*.big` после datacheck (§0) |
| 7 | **Объекты орды vs члены** (выделение, команды, урон), коллизии `HordeMemberCollide` (32) | 0 | — | выделение орды: `Logic/Player.cs` (`ParentHorde`) | входит в №8 | |

### 1.2 Орды и построения

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 8 | **HordeContain** (63): `RankInfo` (позиции рядов), `InitialPayload`, `RanksToReleaseWhenAttacking`, `ComboHorde`, `SplitHorde`, `BannerCarrier*`, `UseSlowHordeMovement`, `MeleeAttackLeashDistance`, `BackUp*`; плюс `HordeAIUpdate` (48), `W3DHordeModelDraw` (37), `HorseHordeContain` (10), `AODHordeContain` (3), `HordeTransportContain` (4), `HordeGarrisonContain` (4), `HordeSiegeEngineContain` (1), `SlaughterHordeContain` (1), `HordeWorkerAIUpdate` (5) | ≈ZH | ближайший аналог — «толпа» GLA: `GameLogic/Object/Contain/MobNexusContain.cpp` + `Object/Update/MobMemberSlavedUpdate.cpp` + `Object/Behavior/SpawnBehavior.cpp` (члены держатся у «нексуса»); групповое движение `GameLogic/AI/AIGroup.cpp`; контейнеры `Object/Contain/OpenContain.cpp`, `TransportContain.cpp` | `Logic/Object/Contain/HordeContain.cs` (444 строки): разбор полный; поведение базовое: смещения формации из `RankInfo`, `Unpack` (создание членов), `SetTargetPoints` (каждому члену своя точка с поворотом), регистрация из `ProductionUpdate`. `Update()` пуст (`TODO(Port)`). `HordeAIUpdate` и прочие только разбор | **XL** (20–30 дн.: базовая версия Э2 ≈ 10–12, бой рядами, отступление, комбо/сплит, баннер, конница ≈ 10–15) | главный риск геймплея: pathfinding ZH ведёт отдельные юниты, а орда BFME должна двигаться как один объект с членами-«хвостами». Нагрузка на CPU (Mali, см. ADR-0002): 15–30 членов на батальон. Вид и плавность (W3DHordeModelDraw) на стороне рендера. **Решено** (§2.2 п. 3): прототип на настоящих объектах, решение по замеру (тик логики ≤ 10 мс p95 на Pixel 8 Pro, 4 игрока); не влезаем — виртуальные члены |
| 9 | **Бонусы орды / большой группы** (`LargeGroupBonusUpdate` 3, `LargeGroupAudioUpdate` 138 — только звук, `RousingSpeechUpdate` 3, ауры героев через №2) | ≈ZH | `HordeUpdate.cpp` (`HordeUpdateModuleData`: `m_kindof`, `m_minCount`, `m_minDist`, `m_rubOffRadius`, флаг; бонус через WeaponBonus) | только разбор | S (2 дн.) поверх №2 | `LargeGroupAudioUpdate` отдать клиенту (звук, не логика) |
| 10 | **Формации (FormationAssistant)** | 0 | — | блок `FormationAssistant` помечен Bfme2 | — | в BFME1, похоже, не используется (в ini.big блоков нет) |

### 1.3 Экономика, застройка, крепости

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 11 | **Крепость/лагерь** `CastleBehavior` (46): распаковка лагеря из файла `bases\<Camp>\<Camp>.bse` (формат map с `CastleTemplates`), захват нейтрального лагеря по `ScanDistance`, `CastleToUnpackForFaction`, `FilterValidOwnedEntries`, `PreBuiltList`; `CastleMemberBehavior` (187), `CastleUpgrade` (3), `KeepObjectDie` (ZH) | 0 | ближайшее: заготовки базы ИИ/скриптов (`AIPlayer.cpp` build lists), `RebuildHoleBehavior.cpp` | `Logic/Object/Behaviors/CastleBehavior.cs`: рабочая распаковка `.bse` и смена владельца по ближайшему юниту (с `TODO`: не проверяются все соседи) | **L** (8–12 дн.) | `.bse` берут Форматы (скорее всего DataChunk, как `.map`, вместе с E1-25); поведение у меня. Стартовые лагеря 1v1 появляются именно отсюда: без этого нет старта матча |
| 12 | **Слоты застройки** (KindOf `BASE_FOUNDATION`, `BASE_SITE`, `NEED_BASE_FOUNDATION`; `FoundationAIUpdate` 17, `GettingBuiltBehavior` 120 — здание строится само, без рабочего; `BuildingBehavior` 67; радиальное меню через CommandSet) | ≈ZH | постановка здания: `Common/System/BuildAssistant.cpp`; строительство рабочим `Object/Update/AIUpdate/DozerAIUpdate.cpp`, `WorkerAIUpdate`; очереди `ProductionUpdate.cpp` | `Update/AIUpdate/FoundationAIUpdate.cs` (прячет слот, если на нём стоит здание; 49 строк), `ObjectKinds.cs`; `GettingBuiltBehavior` только разбор | M–L (6–9 дн.) | заменить «бульдозер ставит здание» на «слот превращается в здание»; продажа зданий/юнитов (2.22) уточнить по данным |
| 13 | **Ресурсы: фермы, лесопилки, печи** (`AutoDepositUpdate` 17 с полями BFME `Upgrade`, `UpgradeBonusPercent`, `UpgradeMustBePresent`, `GiveNoXP`; уменьшение дохода по числу построек `PlayerTemplate.ResourceModifierObjectFilter/ResourceModifierValues`; сборщиков нет) | ZH (+поля) | `GameLogic/Object/Update/AutoDepositUpdate.cpp` (`DepositTiming`, `DepositAmount`, `InitialCaptureBonus`, `ActualMoney`, `UpgradedBoost`); деньги `Common/RTS/Money.cpp` | `Update/AutoDepositUpdate.cs`: реализован, плюс опыт за доход (`AddExperiencePoints`), множитель `GameObject.ProductionModifier` | S–M (3 дн.) | откуда в BFME1 берётся зависимость дохода от свободной земли вокруг фермы, **не нашёл** ни в ZH, ни в OpenSAGE; Форматы пришлют место в данных (файл:строка) |
| 14 | **Найм** (ProductionUpdate 97, `QueueProductionExitUpdate` 51; найм орды по `HordeContain.InitialPayload`; `ProductionSpeedBonus` 1; `ReplenishUnitsBehavior` 2 — пополнение у здания) | ZH (+) | `ProductionUpdate.cpp`, `QueueProductionExitUpdate`, `DefaultProductionExitUpdate`, `SpawnPointProductionExitUpdate` | `Update/ProductionUpdate.cs` (612 строк, `ParentHorde`, `SpawnPayload` для орд) | M (3–5 дн.) | пополнение батальона у казармы (2.22) — проверить, какой модуль |
| 15 | **Апгрейды и кузница** (`SubObjectsUpgrade` 145, `StatusBitsUpgrade` 82, `WeaponSetUpgrade` 59, `GeometryUpgrade` 50, `ArmorUpgrade` 32, `CommandSetUpgrade` 34, `GiveUpgradeUpdate` 35, `CostModifierUpgrade` 14, `TooltipUpgrade` 9, `AudioLoopUpgrade` 6, `BaseUpgrade` 2, `PlayerUpgradeSpecialPower` 3; огненные стрелы = `WeaponSetUpgrade`/бонус оружия) | ZH в основном | `GameLogic/Object/Upgrade/*` (`ArmorUpgrade`, `WeaponSetUpgrade`, `WeaponBonusUpgrade`, `CommandSetUpgrade`, `SubObjectsUpgrade`, `StatusBitsUpgrade`, `CostModifierUpgrade`) | реализованы ZH-модули; BFME-модули только разбор | M (4–6 дн.) | апгрейды объекта vs игрока (кузница даёт игроку, юнит применяет): механика ZH подходит |
| 16 | **Победа/поражение** (`VictorySystemData`, `FactionVictoryData` 14) | ≈ZH | `GameLogic/System/GameLogic.cpp` + `ScriptEngine.cpp` (условия скирмиша) | разбор | S (2 дн.) | BFME1: поражение при потере всех зданий/юнитов (уточнить по `VictorySystemData`) |

### 1.4 Герои и силы

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 17 | **Герои: найм, лимит 1, возрождение** (`HeroDie` 8, `RespawnUpdate` 24, `RespawnBody` 25, `ImmortalBody`/`HighlanderBody` (ZH); `BuildableHeroesMP` в PlayerTemplate; кнопки «воскресить» в крепости) | ≈ZH | `ImmortalBody`, `HighlanderBody`, `UndeadBody` (ZH); лимит `MaxSimultaneousOfType` (`ThingTemplate.cpp`); возрождения в ZH **нет** | `RespawnUpdate.cs`, `HeroDie.cs` только разбор | M (5–7 дн.) | хранить «мёртвого героя» с уровнем и опытом; стоимость воскрешения |
| 18 | **Способности героев** (`SpecialAbilityUpdate` ZH 19, `WeaponFireSpecialAbilityUpdate` 17, `ToggleMountedSpecialAbilityUpdate` 9, `ModelConditionSpecialAbilityUpdate` 9, `HeroModeSpecialAbilityUpdate` 5, `ToggleHiddenSpecialAbilityUpdate` 5, `LevelGrantSpecialPower` 6, `GrabPassengerSpecialPower` 6, `WeaponChangeSpecialPowerModule` 2, `DominateEnemySpecialPower` 1, `DeflectSpecialPower` 1 и т.д.; общий `SpecialPowerModule` 112; `AutoAbilityBehavior` 15) | ≈ZH | `GameLogic/Object/SpecialPower/SpecialPowerModule.cpp`, `OCLSpecialPower`, `Object/Update/SpecialAbilityUpdate.cpp`, шаблоны `Common/RTS/SpecialPower.cpp` (перезарядки) | `SpecialPower/SpecialPower.cs` (`SpecialPowerModule`, 170 строк); остальные только разбор | **L** (10–15 дн.) на 1v1-набор (по 5–8 способностей у ~4 героев на фракцию) | у каждого героя свои способности, разблокировка по уровню героя (через `ExperienceLevel.Upgrades` и условия кнопок; точный механизм проверить по данным) |
| 19 | **Силы (Spellbook) и очки сил** (`Science` 45 блоков, `SciencePurchasePointCost`; `PlayerTemplate.SpellBook`, `PurchaseScienceCommandSet`; очки копятся за опыт/убийства через `Rank` 85) | ≈ZH | система генерала ZH: `Common/RTS/Science.cpp`, `Common/RTS/Player.cpp` (skill points, science purchase points), `Core/GameEngine/Source/GameLogic/System/RankInfo.cpp` | `Logic/Science.cs`, `Logic/Rank.cs` (начисление очков по рангу), `Player.SciencePurchasePoints` | M (5 дн.) для механики; UI дерева — Дизайнер | дерево сил BFME1 = ветка наук ZH, это хорошее совпадение |
| 20 | **Сами силы** (BFME1: `ArrowStormUpdate`, `CloudBreakSpecialPower`, `ElvenWoodSpecialPower`, `TaintSpecialPower`, `DarknessSpecialPower`, `FreezingRainSpecialPower`, `DevastateSpecialPower`, `PlayerHealSpecialPower`, `RainOfFireUpdate`, `PartTheHeavensUpdate`, `RepairSpecialPower`, `ScavengerSpecialPower`, `ManTheWallsSpecialPower`, `FellBeastSwoopPower` — по 1–2 раза; призыв войск — `OCLSpecialPower` (ZH, 22)) | ≈ZH / 0 | `OCLSpecialPower.cpp`, `FireWeaponPower` (ZH) | все перечисленные — только разбор | **L** (10–14 дн.), по 0,5–1,5 дня на силу | многие силы — это ModifierList + область + длительность, поэтому сначала №2 |

### 1.5 Бой, осада, стены

| # | Система / модули (частота) | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 21 | **Конница и раздавливание** (`CrusherLevel` 122, `CrushableLevel` 291, `CrushRevengeWeapon` 32; `SquishCollide` 182 (ZH); `HorseHordeContain` 10; `AODCrushCollide` 4; `DetachableRiderUpdate`/`Body` 2) | ZH (+) | `Object/Collide/SquishCollide.cpp`, `Object/Die/CrushDie.cpp`, давление по `CrusherLevel/CrushableLevel` в `Object/Object.cpp`, `Object/Update/PhysicsUpdate.cpp`, `Object/Update/AIUpdate.cpp` (танки давят пехоту) | `Collide/` (SquishCollide 15 строк); BFME-модули только разбор | M (5–7 дн.) | в BFME давят «на скорости» (атака с разгона), отбрасывание через `MetaImpactNugget`; эффект сильно зависит от физики на float |
| 22 | **Стрелки, огненные стрелы, залпы** | ZH | `Weapon.cpp` (разброс, `DumbProjectileBehavior`), `WeaponSetUpgrade` | `BezierProjectileBehavior.cs` (124 строки, 6 TODO) | S–M (3 дн.) поверх №3 | |
| 23 | **Осада: тараны, катапульты, лестницы, осадные башни** (`SiegeDockingBehavior` 15, `SiegeEngineContain` 4, `SiegeAIUpdate` 2, `SiegeDeploySpecialPower` 2, `SiegeDeployHordeSpecialPower` 9) | ≈ZH | артиллерия = оружие с дальним снарядом (`Weapon.cpp`), башни/лестницы ≈ `TransportContain.cpp` + выход на стену | только разбор | **L** (8–12 дн.) | связано со стенами (№24) |
| 24 | **Стены и ворота крепости** (`GateOpenAndCloseBehavior` 11, `GateProxyBehavior` 2, `WallUpgradeUpdate` 2, `ManTheWallsSpecialPower` 1; хождение по стенам) | 0 | KindOf `WALK_ON_TOP_OF_WALL` есть (`GeneralsMD/Code/GameEngine/Include/Common/KindOf.h`), учитывается в `Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp` и `GameLogic.cpp` **[насколько это покрывает стены BFME, не проверял]** | `Behaviors/GateOpenAndCloseBehavior.cs` (137 строк, базово: открытие для своих) | **L** (8–10 дн.) | в скирмише BFME1 стены есть у Хельмовой Пади/Минас Тирита и крепостей на картах; для первых 1v1 можно отложить |
| 25 | **Гарнизон** (`GarrisonContain` 1, `HordeGarrisonContain` 4, `EvacuateDamage` 7; башни и укрепления) | ZH | `Object/Contain/GarrisonContain.cpp` | `Contain/GarrisonContain` (91 строка) | S (2–3 дн.) | орда в здании = все члены как один пассажир |
| 26 | **Страх, оглушение, эмоции** (`EmotionTrackerUpdate` 76, `EmotionNugget` 15 блоков, `EmotionRange` 69; 2.22: страх и оглушение до 3-го уровня) | 0 | ближе всего состояние паники в AI (`GameLogic/AI/AIStates.cpp`, `Include/GameLogic/AIStateMachine.h`), `PoisonedBehavior` | `Update/EmotionTrackerUpdate.cs` только разбор; `EmotionNugget.cs` | M (5–7 дн.) | влияет на исход боя (террор назгулов, тролли) |
| 27 | **Смерть, обломки, трупы** (`SlowDeathBehavior` 661, `StructureCollapseUpdate` 241, `DestroyDie` 211, `TransitionDamageFX` 139, `FXListDie`, `CreateObjectDie`, `RefundDie` 14, `RubbleRiseUpdate` 11, `ClearanceTestingSlowDeathBehavior` 14) | ZH в основном | `Object/Behavior/SlowDeathBehavior` и др. (ZH) | реализованы ZH-модули | S–M (3 дн.) для BFME-модулей | |
| 28 | **Прочее поведение юнитов** (`HitReactionBehavior` 62, `DualWeaponBehavior` 26, `AimWeaponBehavior` 7, `AutoPickUpUpdate` 12, `RunOffMapBehavior` 11, `DynamicPortalBehaviour` 10, `BannerCarrierUpdate` 8, `SpecialEnemySenseUpdate` 3, `PorcupineFormationBodyModule` 4 (пикинёры), `SymbioticStructuresBody` 3, `DelayedDeathBody` 11, `AnimalAIUpdate` 19, `OathbreakersFadeAwayBehavior` 7, `PassiveAreaEffectBehavior` 4) | 0 | — | только разбор (у `BannerCarrierUpdate`, `PassiveAreaEffectBehavior` по 20–40 строк) | M–L (по 0,5–2 дня на модуль, суммарно 8–12) | `DualWeaponBehavior` (переключение лук/меч) и `HitReactionBehavior` заметны в бою, остальное можно позже |

### 1.6 AI скирмиша

| # | Система | Статус | ZH: где | OpenSAGE | Объём | Риски |
|---|---|---|---|---|---|---|
| 29 | **AI скирмиша** (`PlayerAIType` 15 блоков, `AIData` в `aidata.ini`; скрипты карт) | ≈ZH | `GameLogic/AI/AISkirmishPlayer.cpp`, `AIPlayer.cpp` (build lists, команды, атаки), `Squad.cpp`, скриптовый движок `GameLogic/ScriptEngine/ScriptEngine.cpp` | `Logic/AI/SkirmishAIPlayer.cs`, `AIPlayer.cs` (~154 строки), `PlayerAIType.cs`; почти пусто | **XL** (15–25 дн.) | AI ZH строит бульдозерами по списку, а BFME1 строит на слотах крепости, нанимает орды, тратит очки сил. План: Э3 — свой простой скриптовый AI (порядок слотов + волны), затем данные BFME1 |

### 1.7 Нелогическое, но мешающее загрузке (не моя область, для полноты)

- Draw/клиент: `W3DScriptedModelDraw` 2348, `W3DFloorDraw` 84, `W3DHordeModelDraw` 37, `W3DLightDraw` 25, `W3DBuffDraw` 17, `W3DStreakDraw` 15, `W3DQuadrupedDraw` 3, `AnimationSoundClientBehavior` 130, `*SoundSelectorClientBehavior`. Это Лид/Форматы (рендер). Для логики нужно лишь, чтобы они не роняли загрузку и не влияли на CRC.
- `DelayedLuaEventUpdate` 1, `CritterEmitterUpdate` 1, `CivilianSpawnUpdate/Collide` 1–2, `FadeAndDieOrnamentUpdate` 1: заглушки.
- `OneRingPenaltyUpdate` 2 (Кольцо) и `LivingWorld*`: в скирмише 1v1 не нужны.

### 1.8 Итог по объёму

| Блок | Объём |
|---|---|
| Ядро (№1–7) | ≈ 35–50 чел.-дней |
| Орды (№8–10) | ≈ 22–32 |
| Экономика и застройка (№11–16) | ≈ 26–38 |
| Герои и силы (№17–20) | ≈ 30–41 |
| Бой, осада, стены (№21–28) | ≈ 40–55 |
| AI (№29) | ≈ 15–25 |
| **Всего геймплей BFME1** | **≈ 170–240 чел.-дней [оценка]** |

Это больше, чем PLAN.md закладывает на Э2+Э3 (10–16 недель) для одного агента. Поэтому §3 режет объём: только то, что нужно для 1v1 на 2.22, с «похожим» поведением, и с Э2 работают двое: второго исполнителя подключат к старту Э2, если Э1 пройдёт точку «идём» (Оркестратор, §2.3). Разбиение на два потока — §7.

### 1.9 Где модули живут по фракциям (ini.big, для выбора пары 1v1)

- `HordeContain`: 35 в `object\goodfaction\hordes\…`, 26 в `object\hordes\evilfactionhordes.ini`.
- `AutoDepositUpdate` (пассивный доход): 10 в `goodfactionbuildings.ini`, 4 в `evilfactionbuildings.ini`, 3 у нейтральных построек. У Зла часть экономики идёт через рабочих: `WorkerAIUpdate` (ZH-модуль) 3 в `evilfactionunits.ini`; у Добра `HordeWorkerAIUpdate` 5 в `goodfaction\hordes`.
- `CastleBehavior` 45 из 46 — в `object\civilian\civilianbuildings.ini`: это крепости и лагеря на картах, которые распаковываются под фракцию владельца.
- `SpecialPowerModule`: 68 у юнитов Добра, 21 у юнитов Зла (герои Добра богаче способностями).
- `RespawnUpdate` (возрождение героев): 14 у Добра, 6 у Зла. `GateOpenAndCloseBehavior` и `SiegeDockingBehavior` — в основном постройки Добра (ворота и стены крепостей).

---

## 2. Решения

### 2.1 Мои исходные решения (не противоречат ADR и NET-DESIGN)

1. **Логика 30 Гц** (ADR-0002 §7: `LOGICFRAMES_PER_SECOND = 30`, без `libmain60`). Все длительности INI BFME1 (в мс) переводим в кадры через `ConvertDurationFromMsecsToFrames` (`Core/GameEngine/Include/Common/GameCommon.h`) с `ceil`, как в ZH. На вопрос NET-DESIGN §3.0 («30 или 5 Гц») мой ответ как владельца геймплея: **30 Гц** — я не нашёл в данных ничего, что требует 5 Гц, а 30 Гц дают меньшую задержку ввода. Отдельного решения Лида по этому вопросу ещё нет (§6 Лиду №1), но ADR-0002 §7 и черновик ADR-0003 уже исходят из 30 Гц.
2. **Код BFME в `engine/bfme/`** (ADR-0001 §3), регистрация в фабриках движка через одну точку расширения в `ModuleFactory::init` с пометкой `// BFME:`. Каждый модуль BFME — отдельный класс `Bfme<Имя>` с собственным `ModuleData` и `xfer`/`crc`, чтобы он попадал в CRC и сейвы.
3. **Поведение «похоже на BFME1», а не побайтно** (PLAN §5). Эталоны поведения: данные INI 2.22, OpenSAGE как справочник разбора, наблюдение за игрой на ПК Андрея (только с его согласия). Open-BFME-1 не используем.
4. **Сначала данные, потом поведение.** Каждый модуль сначала парсится полностью (заглушка без логики), потом получает минимальное поведение, и только затем доводится.

### 2.2 Решения Лида движка (02.10.2026), касающиеся геймплея

Пункты 2, 3, 7 и 8 решения Лида (детерминированная математика, флаги компилятора, flush-to-zero, запрет `long` и `lroundf`) вынесены в **ADR-0003-determinism.md** (статус «Предложено»). Здесь — пункты 1, 4, 5, 6.

1. **Модули BFME: явный реестр** (п. 1 Лида; закрывает §6 Лиду №4 и Форматам №4).
   - В `engine/bfme/` — явный реестр всех 123 имён модулей, которых нет в ZH. Таблицу имён (имя, тип модуля: Behavior/Body/Draw/ClientUpdate, интерфейсы) генерируем из вывода datacheck Форматов, а не пишем руками.
   - Каждая заглушка без ошибок разбирает свой INI-блок по схеме Форматов: неизвестные поля пропускает и считает (в лог: модуль, поле, число), в логике ничего не делает. `xfer`/`crc` версионированы с первого дня.
   - Подключение одной точкой: `BfmeModuleRegistry::registerAll(ModuleFactory&)` вызывается в конце `ModuleFactory::init()` (`GeneralsMD/Code/GameEngine/Source/Common/Thing/ModuleFactory.cpp`). Это единственная правка апстрима для реестра, с пометкой `// BFME` и строкой в `docs/UPSTREAM-PATCHES.md`.
   - Страховка: мягкий режим `findModuleTemplate` под флагом `BFME_SOFT_MODULES`. В dev включён (неизвестное имя → заглушка, в лог имя и частота), в CI и релизе выключен: **в CI неизвестное имя модуля — ошибка сборки данных**.
   - Найдено при проверке: `ModuleFactory::addModuleInternal` (`ModuleFactory.cpp:689`) молча перезаписывает шаблон с тем же именем. Поэтому `registerAll` проверяет, что имени ещё нет в таблице (иначе заглушка BFME затрёт модуль ZH). А `W3DModuleFactory::init()` (`Core/GameEngineDevice/Source/W3DDevice/Common/Thing/W3DModuleFactory.cpp:55–59`) вызывает `ModuleFactory::init()` первым, так что настоящие Draw-модули BFME (`W3DScriptedModelDraw` и др.), зарегистрированные позже на стороне W3D, заменят наши заглушки. Это допустимо, только если так задумано (открытый вопрос, §6).
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

### 2.3 Ответы Оркестратора (02.10.2026)

- Сроки в PLAN.md Оркестратор пересматривает сам; оценки здесь — вход для него.
- **Второй исполнитель на геймплей** подключается к старту Э2, если Э1 пройдёт точку «идём» (закрывает §6 Лиду №7 условно). Разбиение работы — §7.
- Порядок Э2/Э3 из §3 — базовый.
- **Версия данных:** на ПК Андрея 2.22; `_patch222*.big` перекрывают базовый `ini.big`, на боксе их пока нет. 300/600 — базовые значения (§0). Сверка баланса только по слою с патчем, после datacheck и с учётом порядка `.big` (`docs/formats/bfme1-big-order.md`, E1-22). Закрывает §6 Форматам №1.

### 2.4 Ответы Форматов (02.10.2026)

- `.bse` берут Форматы (скорее всего DataChunk, как `.map`, вместе с E1-25). Закрывает §6 Форматам №3.
- Схемы полей модулей — у Форматов в `formats/`, поведение — у Геймплея в `engine/bfme/`. Закрывает §6 Форматам №4.
- `ChildObject` наследует модули с заменой и удалением по тегу; частоты считаются по итоговому объекту (мои частоты в §0 этого не учитывают). Числа парсятся без зависимости от локали: `std::from_chars`, без `strtof`. Закрывает §6 Форматам №5 и снимает пометку в §4.1 п. 6.
- Продажу, стойки, пополнение батальонов и падение дохода ферм Форматы пришлют с указанием файл:строка (§6 Форматам №6–7 ждут ответа).

---

## 3. Порядок реализации Э2 и Э3

**Предварительно.** Порядок построен на моём подсчёте частот по `ini.big` (§0, §1.9). Когда «Форматы и данные» выдадут отчёт `datacheck` с частотой модулей (критерий Э1 №4), уточню порядок по нему, особенно с учётом объектов, реально участвующих в 1v1 на картах 2.22, наследования `ChildObject` (§2.4) и слоя `_patch222*.big` (§2.3). Оркестратор подтвердил этот порядок как базовый (§2.3). Как он делится на два потока — §7. На 02.10.2026 17:35 МСК отчёта datacheck в `docs/` и `incoming/` нет.

**Пара фракций для первого 1v1:** **Гондор против Мордора** (Добро против Зла: разные лимиты CP, у Добра фермы через `AutoDepositUpdate`, у Зла орки дешёвые и большие орды). Rohan и Isengard идут вторыми, когда работают модули, общие для фракции. Пару подтверждает Оркестратор/Андрей.

### 3.1 Э2 «Юниты и бой» (цель PLAN: два батальона разных фракций бьются до конца, 10 мин без падений, реплей совпадает по CRC)

| Шаг | Что | Пункты §1 | Объём | Готово, когда |
|---|---|---|---|---|
| Э2.1 | Явный реестр 123 модулей (`BfmeModuleRegistry`, таблица из datacheck, §2.2 п. 1): заглушки разбирают блок, неизвестные поля пропускают и считают; `xfer`/`crc` пустые, но версионированные; `BFME_SOFT_MODULES` только в dev | 1 | M | все 2734 объекта ini.big создаются без падения; в CI неизвестное имя — ошибка |
| Э2.2 | Детерминизм по ADR-0003 (**начинается в Э1**, решение Лида п. 2): своя GameMath и `SAGE_USE_DETERMINISTIC_MATH=ON`, перевод `Trig.cpp`/`wwmath.h`/`PhysicsUpdate.cpp:737`, хелперы `int32_t`, проверка FPCR/MXCSR в начале тика, CI-грепы; реплей-тест QA с первого модуля | §4 | M (6–9 дн.; было S, пока считал, что GameMath в форке готова) | E1-45 + QA: CRC x86_64 = arm64 на реплее; CI ловит `GameClientRandomValue`/`rand()`/`sinf`/`lroundf`/`long` в логике |
| Э2.3 | Оружие на нагетах: `DamageNugget`, `ProjectileNugget`, ближний бой, `BezierProjectileBehavior`; броня BFME через ZH `Armor.cpp` | 3, 22 | L | одиночный мечник бьёт одиночного орка, лучник стреляет дугой |
| Э2.4 | Опыт и уровни `ExperienceLevel` (без героев): свой счётчик 1–10 + отображение в `VeterancyLevel` (§2.2 п. 2) | 4 | M | юнит набирает уровни, получает бонусы из списка; уровень в CRC и сейве |
| Э2.5 | ModifierList, базовая версия: урон, броня, скорость, по длительности и навсегда | 2 | L (первая половина) | бонус уровня и ауры работают и видны в CRC-дампе |
| Э2.6 | Орда, базовая версия: `HordeContain` (ряды из `RankInfo`, выпуск рядов в бой, смерть членов, распад), `HordeAIUpdate` (приказы орде → членам), `HordeMemberCollide`, локомоторы BFME (минимум полей) | 5, 7, 8 | XL (базовая часть ≈ 10–12 дн.) | сначала прототип на настоящих объектах и замер с QA (≤ 10 мс p95, Pixel 8 Pro, 4 игрока, §2.2 п. 3) — точка решения; затем два батальона идут строем, сходятся, бьются до конца |
| Э2.7 | Конница: раздавливание по `CrusherLevel/CrushableLevel` (ZH) + `MetaImpactNugget` (отбрасывание), `HorseHordeContain` | 21 | M | рыцари проходят сквозь орков, урон и отбрасывание повторяются в реплее |
| Э2.8 | Смерть и обломки BFME-модули, `HitReactionBehavior`, `DualWeaponBehavior` | 27, 28 | M | без «застывших» трупов и пропавших юнитов |

Итого Э2 у меня ≈ 40–55 чел.-дней. В план Э2 (4–6 недель) это укладывается только при параллели: Э2.1 и Э2.2 в Э1 и в первую неделю Э2, дальше два потока (§7).

### 3.2 Э3 «Экономика и строительство» — минимум для 1v1 Гондор vs Мордор на карте 2.22

Порядок «чтобы можно было сыграть матч», затем «чтобы было похоже»:

| Шаг | Что | Пункты §1 | Объём |
|---|---|---|---|
| Э3.1 | Крепость: `CastleBehavior` + `.bse` (вместе с Форматами), `CastleMemberBehavior`, `KeepObjectDie`; стартовый лагерь на точке старта | 11 | L |
| Э3.2 | Слоты застройки: `BASE_FOUNDATION/BASE_SITE`, `FoundationAIUpdate`, `GettingBuiltBehavior`, радиальное меню через CommandSet (UI — Дизайнер) | 12 | M–L |
| Э3.3 | Ресурсы: `AutoDepositUpdate` + поля BFME, уменьшение по числу построек (`ResourceModifier*`), рабочие Зла через `WorkerAIUpdate` | 13 | S–M |
| Э3.4 | Найм орд (`ProductionUpdate` + `InitialPayload`), командные очки и лимит | 6, 14 | M |
| Э3.5 | Победа/поражение (`VictorySystemData`) | 16 | S |
| Э3.6 | Простой AI: свой скриптовый (на ZH `AISkirmishPlayer`/`AIPlayer`): занять слоты по списку, строить ферму/казарму/кузницу, собирать волну из N батальонов, атаковать ближайшую цель | 29 (часть) | L |
| — | **Точка «первый матч»: Андрей может сыграть Гондор vs Мордор от старта до победы** | | |
| Э3.7 | Герои: найм, лимит 1, уровни, возрождение (`RespawnUpdate`, `HeroDie`), 2–3 героя на фракцию с 3–4 способностями | 17, 18 | M + L |
| Э3.8 | Силы: очки сил (ветка наук ZH), дерево (Spellbook), 4–6 первых сил каждой стороны, затем остальные | 19, 20 | M + L |
| Э3.9 | Апгрейды и кузница (огненные стрелы, броня, баннеры), `LevelUpUpgrade` | 15 | M |
| Э3.10 | Эмоции: страх и оглушение (по правилам 2.22 — до 3-го уровня) | 26 | M |
| Э3.11 | Гарнизон, ворота и стены, осада | 23, 24, 25 | L + L |
| Э3.12 | AI на данных BFME1 (`PlayerAIType`, `aidata.ini`), применение сил и героев | 29 | L–XL |
| Э3.13 | Rohan и Isengard: их уникальные модули (по datacheck) | — | M–L |

Итого Э3 ≈ 90–130 чел.-дней. Минимум до точки «первый матч» (Э3.1–Э3.6) ≈ 35–50 чел.-дней.

**Что сознательно откладываю за Э3:** Кольцо (`OneRingPenaltyUpdate`), Living World, `DynamicPortalBehaviour`, `CivilianSpawn*`, `CritterEmitterUpdate`, `DelayedLuaEventUpdate`, редкие силы (по 1 использованию).

---

## 4. Детерминизм: правила для всего геймплейного кода

Контекст. Lockstep ZH обменивается только командами, рассинхрон ловится по CRC (`GameLogic.cpp` `getCRC(CRC_RECALC)`, интервал по NET-DESIGN §3.2 — N=30 кадров в release, N=1 в QA). Все игроки на одной сборке arm64 (ADR-0002 §2 п. 1: Android только `arm64-v8a`), Linux x86_64 — для CI и десктопной отладки. Значит, главная цель — **одинаковый результат на всех arm64-телефонах и в CI-прогоне реплеев**, а совпадение x86_64↔arm64 нужно для CI на ПК/эмуляторе и для отладки. Детерминизм ARM по NET-DESIGN §3.2 — зона Лида и QA, сеть только детектирует. Ниже мои правила для `engine/bfme/` и предложения к общей сборке.

**Обновление 02.10.2026.** Лид принял решения по математике, флагам, flush-to-zero и `long` (пункты 2, 3, 7, 8). Обязательная редакция правил теперь в **ADR-0003-determinism.md** (статус «Предложено»): при расхождении с этим разделом действует ADR. Главное: правила обязательны для всего кода логики, в том числе апстримного, а не только для `engine/bfme/`. Во всех целях и на всех платформах — `-ffp-contract=off -fno-strict-aliasing -fwrapv`. `SAGE_USE_DETERMINISTIC_MATH=ON` с Э1, GameMath только в логике. FPCR/MXCSR проверяются в начале каждого тика. `long` и `lroundf` в логике запрещены.

### 4.1 Плавающая точка

**Решение: строгий IEEE-754 single (float), без fixed-point.** Вся логика ZH на `Real` = float: позиции `Coord3D`, здоровье, урон, скорость. Только в `PhysicsUpdate.cpp` 143 упоминания `Real`, в `Core/GameEngine/Source/GameLogic/AI/AIPathfind.cpp` 145, в `Locomotor.cpp` 244, в `Weapon.cpp` 90. Перевести движок на fixed-point — это XL-переписывание без гарантии успеха. Для нового кода BFME fixed-point тоже не берём: он смешивался бы с float движка на каждой границе. Исключение — **счётчики** (деньги, командные очки, опыт, очки сил, кадры, таймеры): только целые.

Правила:
1. **Флаги компилятора для всего симуляционного кода** (уже в MYSOREZ, `src/GeneralsZH-Android-Port/cmake/compilers.cmake`, ветка не-MSVC): `-ffp-contract=off` (запрет слияния `a*b+c` в FMA), а также `-fno-strict-aliasing -fwrapv -fno-delete-null-pointer-checks` (там же, с пояснением: движок читает float через `*(unsigned*)&f` и полагается на переполнение Int). В TSH (`src/GeneralsGameCode/cmake/compilers.cmake`) этих флагов **нет**, поэтому при cherry-pick из TSH их нельзя потерять (ADR-0001 §5: из TSH только `cherry-pick -x`, изменения детерминизма идут через QA). Мой модуль `engine/bfme/` наследует их, а CI проверяет флаги в `compile_commands.json`.
   - Почему это важно на arm64: clang по умолчанию (`-ffp-contract=on`) сливает `a*b+c` внутри выражения в `fmadd` на AArch64, где FMA есть всегда, а на x86_64 без `-mfma` — нет. Результат отличается в последнем бите, и CRC расходится между CI (x86_64) и телефоном, а при разной версии NDK — и между сборками.
   - Запрещено: `-ffast-math`, `-Ofast`, `-funsafe-math-optimizations`, `-ffinite-math-only`, `-freciprocal-math`, `-fassociative-math` (поиск по `CMakeLists.txt`/`*.cmake` MYSOREZ ничего такого не нашёл). Векторизация (`-O2`, `-O3`) допустима, если не включает переассоциацию сумм: clang без fast-math этого не делает.
   - x86_64: только SSE2 (по умолчанию). Никакого x87 (`-mfpmath=387`) и `-march=native`. В ZH есть x87-путь `#if defined(_MSC_VER) && defined(_M_IX86)` (`fld/fsin/fsqrt/fistp` в `Core/Libraries/Source/WWVegas/WWMath/wwmath.h`, `fistp` в `Core/Libraries/Include/Lib/BaseFunctions.h`), на наших сборках он не компилируется.
   - Denormals: не включаем flush-to-zero (`FPCR.FZ` на arm64, `MXCSR.FTZ/DAZ` на x86), режим округления только round-to-nearest. **Решено** (Лид п. 7, ADR-0003 §2.3): регистры у каждого потока свои, опасен только поток логики; SDL3, звук и драйвер проверяет Android-платформа; в начале каждого тика (`GameLogic::update()`, `GameLogic.cpp:3679`) Геймплей проверяет FPCR/MXCSR: в debug — assert, в release — принудительная установка со счётчиком.
2. **Трансцендентные функции только свои, одинаковые для всех платформ.** libm разный: bionic (Android), glibc (CI), UCRT (Windows). В ZH логика зовёт libm напрямую:
   - `GeneralsMD/Code/GameEngine/Source/Common/System/Trig.cpp`: `Sin`→`sinf`, `Cos`→`cosf`, `ACos`→`acosf`, `ASin`→`asinf` (по MZ DEV_BLOG в пересказе NET-DESIGN §2: `sinf` расходится на 1,236% углов);
   - `Core/Libraries/Source/WWVegas/WWMath/wwmath.h`: на не-x86-MSVC `WWMath::Sin/Cos`→`sinf/cosf`, `Atan`/`Atan2`→`atan`/`atan2` (double), `Acos/Asin`→`acos/asin`; таблицы `_FastSinTable` и пр. заполняются при `WWMath::Init()`;
   - `GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/PhysicsUpdate.cpp:737` — `atan2(xvec.Z, xy)` напрямую.
   - **Исправление после проверки:** готовой GameMath в MYSOREZ нет. `cmake/gamemath.cmake` (опция `SAGE_USE_DETERMINISTIC_MATH`, по умолчанию OFF, строка 23) тянет `Core/GameMath` из TSH на коммите `838f9d0f`, но такого каталога там нет (`git ls-tree`). Ветки `#ifdef USE_DETERMINISTIC_MATH` в `wwmath.h` форка — заглушки `// TODO: return GameMath::…` с откатом на CRT. **Решено** (Лид п. 2, ADR-0003 §2.2): ON с Э1 во всех целях; GameMath пишет и подключает Геймплей; `Trig.cpp`, `wwmath.h`, `PhysicsUpdate.cpp:737` переводятся на неё только в логике, рендер и клиент остаются на libm. Для `engine/bfme/` — только `bfme::math::{sin,cos,atan2,acos,asin,pow,exp,log}` поверх той же GameMath. Прямой вызов libm в `engine/bfme/` ловит линтер в CI.
   - `sqrtf`, `+ - * /`, `floorf/ceilf/truncf`, `fabsf`, `fminf/fmaxf` по IEEE точны (корректное округление), их можно звать напрямую.
3. **Преобразования float→int.** `fast_float2long_round` в `Core/Libraries/Include/Lib/BaseFunctions.h` использует `lroundf` и возвращает `long`: 32 бита на Windows и 64 бита на arm64/x86_64 Linux. На это MZ уже натыкался (NET-DESIGN §2). Под ним `REAL_TO_INT_FLOOR/CEIL` (используются в pathfinding, `Core/GameEngine/Include/GameLogic/AIPathfind.h:493`). **Решено** (Лид п. 8, ADR-0003 §2.4): `lroundf`, `lrintf` и `long`/`unsigned long` в логике и сериализации запрещены; вместо них хелперы с `int32_t` в GameMath (определённое поведение на NaN/±inf/переполнении) и CI-греп по GameLogic. Никаких C-кастов float→int за пределами диапазона (это UB).
4. **Никакого `double` в логике BFME** без явной необходимости и без явного приведения обратно к float в той же точке. Смешанные выражения float/double дают разные результаты при разной оптимизации.
5. **Порядок операций с float не зависит от порядка контейнеров** (см. 4.3): сумма модификаторов `ModifierList` считается в фиксированном порядке (по имени/ID модификатора), а не по порядку прихода аур.
6. **Значения из INI парсим один раз и одинаково:** проценты (`50%`) → `float` через одну функцию, длительности → целые кадры с `ceil`. Парсер не зависит от локали: **решено Форматами** — `std::from_chars`, без `strtof` (на Android локаль приложения может быть `ru_RU` с запятой).

### 4.2 Случайные числа

- В ZH три независимых генератора с общим алгоритмом (`Core/GameEngine/Source/Common/RandomValue.cpp`, `randomValue()` на 6 словах `UnsignedInt` с переносом):
  - `theGameLogicSeed` → `GetGameLogicRandomValue/…Real` (макросы `GameLogicRandomValue(lo, hi)`, `GameLogicRandomValueReal(lo, hi)` в `Core/GameEngine/Include/GameLogic/LogicRandomValue.h`). **Единственный допустимый источник случайности для логики.** Сид общий для матча: `InitRandom(seed)` из настроек игры (`SkirmishGameOptionsMenu.cpp:444` `TheSkirmishGameInfo->getSeed()`, сеть `GameSpyGameInfo.cpp:562` `InitGameLogicRandom(...getSeed())`, реплей `Common/Recorder.cpp:1257` `InitRandom(m_gameInfo.getSeed())`). CRC сида — `GetGameLogicRandomSeedCRC()`, он входит в CRC логики.
  - `theGameClientSeed` → `GameClientRandomValue` (`Core/GameEngine/Include/GameClient/ClientRandomValue.h`): только визуал. Его можно вызывать сколько угодно раз, у каждого игрока по-разному, поэтому **результат никогда не должен влиять на состояние логики**.
  - `theGameAudioSeed` → звук.
  - TSH добавил `GameLogicRandomValueUnchanged/RealUnchanged`: значение из логического сида **без сдвига** состояния, для «желательно одинаково, но не критично» (скриптовый звук). В логике BFME не используем.
  - `InitRandom()` без аргумента сидит от `time(nullptr)`: в матче его вызывать нельзя.
- В логических файлах ZH есть 17 вызовов `GameClientRandomValue` (18 строк `rg` вместе с комментарием) (например, `Object/Update/AutoDepositUpdate.cpp:190` — позиция текста денег, `Object/Body/ActiveBody.cpp:1026` — кость для эффекта с комментарием «Moved back to GameClientRandomValue because of desync problems», `StealthUpdate.cpp:136`, `StructureCollapseUpdate`, `StructureToppleUpdate`, `EMPUpdate`). Это допустимо, только пока результат идёт в клиент. Первичная классификация — в ADR-0003 §2.5 п. 7: 14 вызовов визуальные; на проверку QA остаются три: `SpectreGunshipUpdate.cpp:661–662` (точка попадания, в BFME1 модуль не используется) и `StealthUpdate.cpp:136`. У `StructureToppleUpdate` клиентский RNG задаёт кадр следующей «вспышки» FX, и это значение пишется в `xfer` (сейвы у игроков различаются, но в CRC не входит).
- Правила для `engine/bfme/`:
  1. Случайность в логике — только `GameLogicRandomValue*`. Запрещены `rand()`, `std::random_device`, `std::mt19937` без сида из матча, `GameClientRandomValue` в коде, который пишет состояние объекта/игрока. Линтер в CI.
  2. Число вызовов логического RNG не должно зависеть от клиента: нельзя звать его из кода, который исполняется только при видимости объекта на экране, только у локального игрока, только при включённом звуке и т.п. (классика рассинхрона в SAGE).
  3. Разброс членов орды (`HordeContain.RandomOffset`), разброс урона, шанс в `HitReactionBehavior`, задержки `BackUpMin/MaxDelayTime` — всё через логический RNG и только в update логики.
  4. Для отладки рассинхрона включать `DEBUG_RANDOM_LOGIC` (лог каждого вызова с файлом/строкой в `RandomValue.cpp`) в QA-сборке.

### 4.3 Порядок, адреса, время

1. **Никаких обходов hash-контейнеров в логике** (`std::unordered_map/set`, `hash_map`): порядок зависит от реализации STL и от адресов. В логике ZH я их не нашёл (`rg` по `GameLogic/`). Для BFME: `std::map`/`std::vector` с сортировкой по стабильному ключу (`ObjectID`, `NameKeyType`, индекс в INI).
2. **Никакого порядка по адресам указателей.** Ключи `std::map<T*, …>` и сортировки по `T*` запрещены, если по ним итерируются с влиянием на состояние. В ZH есть `AttackPriorityMap = std::map<const ThingTemplate*, Int>` (`GeneralsMD/Code/GameEngine/Include/GameLogic/ScriptEngine.h:102`): в логике он используется только для поиска (`ScriptEngine.cpp:183`), это безопасно. Но по нему идёт обход в отладочном дампе (`:200`) и в `xfer` (`:265`, `:284`), поэтому сейвы и дамп `XferDeepCRC` у двух машин могут различаться без настоящего рассинхрона (ADR-0003 §2.5 п. 2). Такой же шаблон в BFME допустим только для `find`.
3. **Порядок обхода объектов** — порядок списка объектов `GameLogic` (детерминированный, по созданию). Для орд: члены в порядке рядов из `RankInfo`, а не в порядке регистрации из `ProductionUpdate`.
4. **Запросы «ближайший/все в радиусе»** (`PartitionManager.cpp`) при равных расстояниях дают ничью: разрешать по `ObjectID`.
5. **Никакого времени стены в логике:** только номер кадра `TheGameLogic->getFrame()`. В `GameLogic.cpp` есть `timeGetTime()` (строки 1866, 1962, 4613–4654), но это загрузка и таймауты прогресса, не симуляция. В BFME запрещены `time()`, `clock()`, `std::chrono`, `timeGetTime` в коде логики.
6. **Никакого состояния из клиента в логике:** видимость камеры, LOD, FPS, настройки графики, «мобильный слой» UI. Мобильный слой INI (короткий матч) — часть данных матча, его хеш должен входить в проверку при старте (NET-DESIGN §3.2 «хэш данных»).
7. **Ленивая инициализация** (кэши, таблицы) — при загрузке, а не при первом вызове в матче, если от неё зависит результат.
8. **Каждый модуль BFME реализует `crc()` и `xfer()`** со всеми полями состояния (особенно таймеры, накопленные модификаторы, позиции в формации). Иначе рассинхрон найдётся поздно, а XferDeepCRC (NET-DESIGN §3.2) не покажет, где.

### 4.4 Проверка (вместе с QA)

- Реплей-тесты с Э2.1: на каждый новый модуль BFME — короткий реплей (2–3 мин) с CRC каждые N=1 кадр, прогон на x86_64 (CI) и arm64 (эмулятор или устройство), сравнение потоков CRC. Это критерий Э2 в PLAN.md.
- Тест «двойной прогон» на одном устройстве с разным порядком загрузки ассетов и разным разрешением/LOD: CRC должны совпасть.
- Линтер `engine/bfme/`: запрет `sinf|cosf|tanf|atan2|acos|asin|powf|expf|logf|rand\(|GameClientRandomValue|unordered_|time\(|chrono` в логических файлах (с явным списком исключений).

### 4.5 Известные проблемные места ZH (для QA и Лида)

| Что | Где | Почему риск |
|---|---|---|
| libm в тригонометрии логики | `GeneralsMD/Code/GameEngine/Source/Common/System/Trig.cpp` (`sinf/cosf/acosf/asinf`); `Core/Libraries/Source/WWVegas/WWMath/wwmath.h` (`sinf/cosf/atan/atan2/acos/asin`) | bionic ≠ glibc ≠ UCRT; готовой GameMath в MYSOREZ нет, флаг по умолчанию OFF (ADR-0003 §1) |
| прямой `atan2` в физике | `GameLogic/Object/Update/PhysicsUpdate.cpp:737` | то же, плюс double |
| `lroundf` → `long` | `Core/Libraries/Include/Lib/BaseFunctions.h` (`fast_float2long_round`, `REAL_TO_INT_FLOOR/CEIL`) | ширина `long` и поведение на NaN различаются по платформам (MZ: NaN в REAL_TO_INT_FLOOR) |
| чтение float через `*(int*)&f` | `wwmath.h` (`Fabs`, `Float_To_Int_Floor/Chop`, `Is_Valid_Float`), `BaseFunctions.h` (`fast_float_ceil/floor/trunc`) | strict aliasing; закрыто флагом `-fno-strict-aliasing` в MYSOREZ |
| FMA | любой float-код без `-ffp-contract=off` | в TSH флага нет; при слияниях с TSH следить |
| клиентский RNG в логических файлах | 17 вызовов (18 строк `rg`), например `ActiveBody.cpp:1026`, `AutoDepositUpdate.cpp:190`, `StealthUpdate.cpp:136` | безопасно, только пока результат не попадает в состояние; классификация — ADR-0003 §2.5 п. 7 |
| float повсюду в физике и pathfinding | `PhysicsUpdate.cpp`, `AIPathfind.cpp`, `Locomotor.cpp` | детерминирован при правилах 4.1; на ретейле работал (x87/SSE одна сборка), на arm64 проверяют реплеи |

---

## 5. Главные риски геймплея

1. **Орды на pathfinding ZH** (§1 №8). Pathfinding ZH рассчитан на отдельные юниты и малые группы (`AIGroup.cpp`). Орда BFME1 — объект-контейнер с членами по слотам формации. Нужен ранний прототип (Э2.6, первые 2–3 недели Э2) и замер с QA: тик логики ≤ 10 мс p95 на Pixel 8 Pro в тяжёлой сцене на 4 игроков (§2.2 п. 3). Не влезаем — виртуальные члены (+8–12 дн.).
2. **ModifierList — системный слой** (№2), на нём держатся уровни, ауры, силы, баннеры, кузница. Ошибка в дизайне всплывёт везде, поэтому дизайн ревьюит Лид до кода.
3. **Объём** ≈ 170–240 чел.-дней против 10–16 недель Э2+Э3 в PLAN.md. Второй исполнитель будет только при «идём» Э1 (§2.3). Без него прототип Э3 сдвигается. Мера: §3 режет до минимума 1v1, редкие модули — заглушки, работа делится на два потока (§7).
4. **Слой патча 2.22 ещё не на боксе.** 300/600 в `ini.big` — базовые значения; 2.22 лежит в `_patch222*.big`, которые перекрывают базовый слой (§2.3). Пока их нет, частоты и баланс — по базе; сверка только после копирования патча и datacheck с порядком `.big` (E1-22).
5. **Механики, не найденные в коде** ни ZH, ни OpenSAGE: зависимость дохода фермы от свободной площади, продажа юнитов и стойки из 2.22, пополнение батальонов у здания. Их придётся восстанавливать по данным и наблюдению.
6. ~~Предел enum `VeterancyLevel`~~ — **снят**: отдельный счётчик BFME (§2.2 п. 2).
7. **GameMath надо писать самим** (ADR-0003 §1): решение «ON с Э1» добавляет в Э1 6–9 дн. работы Геймплея [оценка] и вопрос лицензии/источника кода (fdlibm, musl). Если она задержится, E1-45 проверит только флаги.

---

## 6. Открытые вопросы

Статус на 02.10.2026, 18:00 МСК. Закрытые вопросы оставлены для истории, ответ указан рядом.

**Лиду движка:**
1. **Открыт.** Подтверждаем ли логику 30 Гц для BFME1 (ADR-0002 §7) как окончательную? С моей стороны возражений нет (§2.1), это закрывает вопрос NET-DESIGN §3.0. Отдельного ответа ещё нет; ADR-0002 §7 и ADR-0003 исходят из 30 Гц.
2. ✅ **Решён** (Лид п. 2 → ADR-0003 §2.2). Включаем ли `SAGE_USE_DETERMINISTIC_MATH=ON`? — Да, с Э1 во всех целях; на GameMath переводим только логику. *Новое:* GameMath в форке нет, пишем свою (вопросы 9–10 ниже).
3. ✅ **Решён** (Лид п. 4 → §2.2 п. 2). Как расширяем уровни? — Enum не трогаем, отдельный счётчик BFME 1–10 с `xfer`/CRC, одностороннее отображение в `VeterancyLevel`.
4. ✅ **Решён** (Лид п. 1 → §2.2 п. 1). Точка расширения фабрики и `DEBUG_CRASH`? — Явный реестр, `BfmeModuleRegistry::registerAll(ModuleFactory&)` в конце `ModuleFactory::init()`; мягкий режим только под `BFME_SOFT_MODULES` в dev. Про `W3DModuleFactory` — вопрос 11 ниже.
5. ✅ **Решён** (Лид п. 5 → §2.2 п. 3). Орда? — Прототип на настоящих объектах, решение по замеру (≤ 10 мс p95, Pixel 8 Pro, 4 игрока), иначе виртуальные члены.
6. ✅ **Решён** (Лид п. 6 → §2.2 п. 4). Кто владеет `ModifierList` и нагетами? — Геймплей, хуки `// BFME` в `Weapon.cpp`/`Damage.cpp` на ревью Лида.
7. ✅ **Решён условно** (Оркестратор → §2.3). Второй исполнитель? — К старту Э2, если Э1 пройдёт точку «идём». Разбиение — §7.
8. ✅ **Решён** (Лид п. 7 → ADR-0003 §2.3). FPCR/MXCSR? — SDL3, звук и драйвер проверяет Android-платформа; проверку в начале каждого тика делает Геймплей.
9. **Новый.** Источник и лицензия кода GameMath (fdlibm, musl или ждём TSH) и где она живёт: `engine/bfme/` или `Core/Libraries` (ADR-0003 §5 п. 1–2).
10. **Новый.** E1-45 в E1-BACKLOG сформулирован как «GameMath на fdlibm в форке»; её там нет. Уточнить, что E1-45 проверяет флаги (OFF) и нашу GameMath, когда она будет готова.
11. **Новый.** Draw/ClientUpdate-модули BFME (`W3DScriptedModelDraw` 2348 и др.): регистрировать заглушки в нашем реестре в `ModuleFactory::init()` и давать W3D-стороне их перезаписать (`addModuleInternal` перезаписывает молча, §2.2 п. 1) или держать отдельный список для `W3DModuleFactory::init()`? И кто владеет их поведением (рендер)?
12. **Новый.** Хуки в `ProductionUpdate.cpp` (найм орд, CP) и `Player.cpp` (CP, очки сил) нужны потоку B (§7): та же процедура, что и для `Weapon.cpp`/`Damage.cpp` (минимальный хук `// BFME`, ревью Лида)?

**Форматам и данным:**
1. ✅ **Решён** (Оркестратор → §0, §2.3). Какая версия в `ini.big`? — Базовый слой; 300/600 — базовые значения; 2.22 в `_patch222*.big` (на бокс ещё не скопированы), порядок — `docs/formats/bfme1-big-order.md` (E1-22).
2. **Открыт.** Когда будет datacheck с частотой модулей, нужна разбивка не только по INI, но и **по объектам, доступным фракциям в скирмише** (CommandSet крепости/зданий → какие юниты и герои реально нанимаются), и по картам 1v1 2.22 (`CastleBehavior`-объекты на картах), по итоговым объектам после `ChildObject` и со слоем `_patch222*.big`. По ней я уточню §3 и сгенерирую таблицу реестра (§2.2 п. 1).
3. ✅ **Решён** (§2.4). Кто разбирает `.bse`? — Форматы, вместе с E1-25.
4. ✅ **Решён** (§2.4). Схемы полей модулей? — У Форматов в `formats/`, поведение у Геймплея.
5. ✅ **Решён** (§2.4). Наследование и локаль? — `ChildObject` с заменой и удалением по тегу, частоты по итоговому объекту; числа через `from_chars`.
6. **Ждёт ответа** (Форматы пришлют файл:строка). Продажа юнитов, стойки, пополнение батальонов в данных 2.22.
7. **Ждёт ответа** (Форматы пришлют файл:строка). Откуда берётся падение дохода фермы от соседних построек.
8. **Новый.** Когда `_patch222*.big` окажутся на боксе (нужно для сверки баланса и для таблицы реестра по итоговым объектам)? Это вопрос к Оркестратору и Андрею.

**QA:**
1. **Новый.** Сценарий замера орды (§2.2 п. 3): тяжёлая сцена на 4 игроков — сколько батальонов, какие фракции, длительность, что считаем «тиком» (только `GameLogic::update()` или вместе с pathfinding в фоне). Нужен до прототипа Э2.6.
2. **Новый.** Общий тестовый сценарий 1v1 для двух потоков (§7.5): карта и фиксированный сид.

---

## 7. Два параллельных потока Э2/Э3

**Условия.** Второй исполнитель подключается к старту Э2, если Э1 пройдёт точку «идём» (§2.3). До этого я делаю общую основу: реестр модулей (Э2.1) и детерминизм по ADR-0003 (Э2.2); по решению Лида обе части стартуют уже в Э1. Оценки — в чел.-днях **[оценка]**. Недели считаются от старта Э2 (Н1 — первая неделя Э2), календарные даты назначит Оркестратор при пересмотре PLAN.md.

### 7.1 Разбиение

Предложено: поток A — орды, бой, нагеты, ModifierList, опыт и уровни, герои и силы; поток B — экономика, крепости и слоты, найм, CP, стены и осада, AI.

**Предлагаю изменённый вариант** с тем же смыслом: A — «юнит в бою», B — «игрок, база, прогрессия, AI». Переношу из A в B три вещи:

1. **Опыт и уровни (№4) → B.** Опыт — общий счётчик. Пишут в него оба потока: убийства идут из A, доход ферм (`AutoDepositUpdate`, `GiveNoXP`) — из B. Читают его найм и возрождение героев, `LevelUpUpgrade`, очки сил (`Rank`), то есть в основном B. Кроме того, у B появляется настоящая работа в Э2, пока крепость ждёт `.bse` от Форматов.
2. **Найм, лимит и возрождение героев (№17), очки сил и Spellbook (№19) → B.** Это `ProductionUpdate`, `Player`, кнопки крепости и `Science`, то есть тот же код, что найм и CP. **Способности героев (№18) и эффекты сил (№20) остаются в A**: они строятся на ModifierList, нагетах и областях действия.
3. **Смерть и обломки (№27) → B.** Это в основном здания: `StructureCollapseUpdate` 241, `RubbleRiseUpdate`, `RefundDie`.

| Вариант | Поток A | Поток B | Работа B в Э2 |
|---|---|---|---|
| исходный | ≈ 103–145 чел.-дн. | ≈ 61–90 | только задел на Э3 (крепость), цель Э2 целиком в A |
| **предлагаемый** | **≈ 79–115** | **≈ 80–114** | уровни, смерть и обломки, CP, крепость |

Исполнители: **я беру поток A**. Орды и замер — главный риск, а бой ZH и OpenSAGE я уже разобрал. **Второй исполнитель берёт поток B.** Общая основа (реестр, ADR-0003) сделана мной до Н1 и дальше поддерживается мной. Таблица реестра генерируется из datacheck, поэтому каждый поток правит в ней только строки своих модулей.

### 7.2 Поток A: «юнит в бою» (Геймплей)

| ID | Задача | №§1 | Шаг §3 | Оценка | Зависит от |
|---|---|---|---|---|---|
| A1 | ModifierList: API и базовая версия (урон, броня, скорость; на время и навсегда; фиксированный порядок применения) | 2 | Э2.5 | 4–6 | заголовок с Н1 (§7.5) |
| A2 | Хуки `// BFME` в `Weapon.cpp`/`Damage.cpp`, ревью Лида | 3 | Э2.3 | 1–2 | ревью Лида |
| A3a | Минимальный ближний бой: `DamageNugget`, `MeleeWeapon` | 3 | Э2.3 | 3–4 | A2 |
| A4 | **Прототип орды на настоящих объектах + замер с QA** (≤ 10 мс p95, Pixel 8 Pro, 4 игрока) → **решение** | 8 | Э2.6 | 6–8 | A3a, сценарий QA (§6 QA №1) |
| A5 | Орда базовая: ряды `RankInfo`, выпуск рядов, смерть членов, распад, `HordeAIUpdate`, `HordeMemberCollide`, минимум локомоторов | 5, 7, 8 | Э2.6 | 6–8 (+8–12 при виртуальных членах) | A4 |
| A3b | Остальные нагеты: `ProjectileNugget`, `HordeAttackNugget` | 3 | Э2.3 | 5–8 | A3a |
| A6 | Стрелки, `BezierProjectileBehavior` | 22 | Э2.3 | 3 | A3b |
| A7 | Конница, раздавливание, `MetaImpactNugget` | 21 | Э2.7 | 5–7 | A5 |
| A8 | `DualWeaponBehavior`, `HitReactionBehavior` | 28 (часть) | Э2.8 | 3–5 | A3b |
| | *Итого в Э2* | | | *36–51* | |
| A9 | ModifierList полностью: ауры `AttributeModifierAuraUpdate` (181), `AttributeModifierUpgrade` (для кузницы B), пулы | 2 | Э3.9 | 4–6 | A1 |
| A10 | Бонусы орды, баннер, `LargeGroupBonusUpdate` | 8, 9 | Э3.9–3.10 | 3–5 | A5, A9 |
| A11 | Орда: комбо и сплит, отступление, орда в контейнерах (гарнизон, осадные машины) | 8 | Э3.11 | 6–10 | A5 |
| A12 | Страх и оглушение (`EmotionTrackerUpdate`, `EmotionNugget`) | 26 | Э3.10 | 5–7 | A5 |
| A13 | Способности героев (набор для 1v1) | 18 | Э3.7 | 10–15 | A1, B1 (уровни) |
| A14 | Эффекты сил | 20 | Э3.8 | 10–14 | A9, B12 |
| A15 | Остальное поведение юнитов | 28 | после «первого матча» | 5–7 | — |
| | *Итого в Э3* | | | *43–64* | |

**Критический путь A (Э2):** заглушки интерфейсов (Н1) → A2 → A3a → **A4: замер и решение (конец Н3 — Н4)** → A5 → реплей боя с совпадением CRC (QA) = цель Э2. Это ≈ 17–23 дн., то есть Н4–Н5. Хвост Э2 (A1, A3b, A6–A8, ≈ 20–29 дн.) идёт параллельно критическому пути B к «первому матчу» и его не задерживает. В Э3 у A критический путь A9 → A13 → A14: герои и силы, от них зависит AI B17.

### 7.3 Поток B: «игрок, база, прогрессия, AI» (второй исполнитель)

| ID | Задача | №§1 | Шаг §3 | Оценка | Зависит от |
|---|---|---|---|---|---|
| B1 | Прогрессия: счётчик опыта и уровня 1–10, `xfer`/CRC, отображение в `VeterancyLevel`, `ExperienceLevel`, `LevelUpUpgrade`, `ExperienceLevelCreate` | 4 | Э2.4 | 5–7 | API ModifierList (заглушка) |
| B2 | Опыт из событий: убийство (`Object::scoreTheKill`, приёмник орды `setExperienceSink`), доход фермы | 4, 13 | Э2.4 | 1–2 | Horde API (A4), события |
| B3 | Смерть и обломки, модули BFME | 27 | Э2.8 | 3 | — |
| B4 | Командные очки и лимит (числа 2.22 — из слоя патча) | 6 | Э3.4 | 2–3 | `_patch222*.big` для чисел |
| B5 | Найм: `ProductionUpdate` + `InitialPayload` через Horde API, очередь, пополнение (по данным Форматов) | 14 | Э3.4 | 3–5 | A5 |
| B6 | Крепость: `CastleBehavior` + `.bse`, `CastleMemberBehavior`, `KeepObjectDie` | 11 | Э3.1 | 8–12 | Форматы: `.bse` (вместе с E1-25) |
| | *Итого в Э2 (B6 и B4–B5 — задел на Э3)* | | | *22–32* | |
| B7 | Слоты застройки, `FoundationAIUpdate`, `GettingBuiltBehavior`, CommandSet радиального меню (UI — Дизайнер) | 12 | Э3.2 | 6–9 | B6 |
| B8 | Ресурсы: `AutoDepositUpdate` с полями BFME, падение дохода | 13 | Э3.3 | 3 | Форматы: файл:строка |
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
| | *Итого в Э3* | | | *58–82* | |

**Критический путь B (до «первого матча»):** `.bse` от Форматов → B6 → B7 → B8 → B4 + B5 (нужна орда A5) → B9 → B10 = **точка «первый матч»**. Это ≈ 32–44 дн. от начала B6. Если `.bse` готов к Н1, B начинает с B6, а B1 делает параллельно с ожиданием ревью. Если нет — сначала B1, B3, B4, а B6 стартует, как только Форматы отдадут `.bse`. Вся задержка `.bse` ложится на «первый матч» один в один.

### 7.4 Точки стыковки

Правила для всех интерфейсов: вызовы синхронные, внутри тика логики, в порядке кода. Никаких очередей и подписок, упорядоченных по адресам (§4.3). Сигнатуры меняются только по согласию обоих потоков и записываются в журнал изменений (§7.5 п. 6).

| # | Интерфейс | Владелец | Кому нужен | Заглушка | Рабочая версия нужна к |
|---|---|---|---|---|---|
| 1 | **ModifierList** (`bfme::modifiers`: наложить и снять по источнику, множитель по категории, фиксированный порядок) | A | B: бонусы уровня (B1, `ExperienceLevel.AttributeModifiers`), кузница (B13, `AttributeModifierUpgrade`) | Н1: множитель всегда 1.0 | база A1 — к **Н3** (B1); ауры и апгрейды A9 — к **Э3.9** (B13) |
| 2 | **Horde API** (`bfme::horde`: создать орду из `InitialPayload`, член → орда, обход членов в порядке рядов, «член погиб», «орда распалась») | A | B: опыт орды (B2), найм (B5), AI (B10), гарнизон и осада (B14, B16) | Н1: орда — один объект без членов | после решения по замеру, к **Н4**; найм B5 — к **Э3.4** (≈ Н6–Н7). При переходе на виртуальных членов сигнатуры не меняются, меняется только реализация. Ради этого API и фиксируем в Н1 |
| 3 | **События логики** (`bfme::events`: `onDamage`, `onKill(killer, victim)`, `onLevelUp(obj, from, to)`, `onIncome(player, amount, source)`) | каркас — B; источники: `onDamage`/`onKill` — A (хуки `Damage.cpp`, `scoreTheKill`), `onLevelUp`/`onIncome` — B | A: способности по уровню (A13); B: опыт (B2) | Н1: пустые вызовы | `onKill` — к **Н2–Н3** (B2); `onLevelUp` — к **Э3.7** (A13) |
| 4 | **Прогрессия** (`bfme::progression`: уровень, опыт, `addXP`, отображение в `VeterancyLevel`) | B | A: способности героев (A13), бонусы по уровню через ModifierList | Н1: уровень всегда 1 | B1 — к **Н3**, герои — к **Э3.7** |
| 5 | **CommandSet и найм** (смена CommandSet, радиальное меню слотов, кнопки крепости, очередь `ProductionUpdate`, проверка CP перед наймом, кнопки героев и воскрешения) | B | A: кнопки способностей героев (A13); стойки и построения орды, если они найдутся в данных 2.22 (§6 Форматам №6) | CommandSet ZH как есть | B7 — к **Э3.2**; кнопки способностей — к **Э3.7** |
| 6 | **Силы и способности**: исполнение через стандартный путь ZH (`SpecialPowerModule` + `MSG_DO_SPECIAL_POWER*`, `Core/GameEngine/Include/Common/MessageStream.h:547`) | A (эффекты) | B: AI (B17), Spellbook (B12 открывает силу через науку) | путь ZH уже есть; сила без эффекта = заглушка | первые силы — к **Э3.8**, все нужные AI — к **Э3.12**. AI зовёт силы только командой, как игрок, а не прямым вызовом эффекта: так реплей и сеть видят то же, что игрок |
| 7 | **Очки сил и науки** (очки покупки наук `Player`, `Rank`) | B | A: доступ к эффектам сил (A14) | механика ZH | **Э3.8** |
| 8 | **Урон по зданиям и стенам** (нагеты по постройкам, ворота, осадное оружие) | A (нагеты, `Damage.cpp`) | B: стены, ворота, осада (B15, B16) | — | A3b — к **Н4**; для осады — к **Э3.11** |
| 9 | **Pathfinding и стены** (`AIPathfind.cpp`, `WALK_ON_TOP_OF_WALL`, ворота) | A (движение орд); правки по заявке B | B15 | — | **Э3.11** |
| 10 | **Детерминизм** (ADR-0003: GameMath, хелперы `int32_t`, CI-грепы, FPCR/MXCSR) | я (Э1) | оба | — | до **Н1** |
| 11 | **Реестр модулей** (таблица из datacheck) | я (Э1); дальше каждый поток правит строки своих модулей | оба | — | до **Н1** |
| 12 | **Хуки в апстримных файлах** — у каждого файла один автор: A — `Weapon.cpp`, `Damage.cpp`, `AIPathfind.cpp`, `GameLogic.cpp` (FPCR); B — `ProductionUpdate.cpp`, `Player.cpp` (CP, очки сил), `AutoDepositUpdate.cpp`; реестр — `ModuleFactory.cpp` (я). Все хуки — на ревью Лида (§6 Лиду №12) | — | — | — | по мере задач |

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
- **Э3 до «первого матча»:** критический путь идёт через поток B (B6 → B7 → B8 → B4/B5 → B9 → B10), ≈ 32–44 дн. от начала B6. Он зависит от Форматов (`.bse`) и от A5 (найм орд).
- **После «первого матча»:** A13/A14 (герои, силы) и B15–B17 (стены, осада, AI); B17 ждёт A13 и A14.
- **Календарь:** у каждого потока ≈ 80–115 чел.-дней, то есть 16–23 недели против 10–16 недель Э2+Э3 в PLAN.md. Что можно перенести за Э3 при пересмотре плана: A11, A15, B15–B16 (для первых 1v1 хватит карт без стен), часть B17.
- **Риски стыковки:**
  - замер орды провален → виртуальные члены: +8–12 дн. в A, меняется реализация Horde API, но не сигнатуры;
  - задержка `.bse` → B6 стоит, B переключается на B1, B3, B4, B11;
  - очередь хуков на ревью у Лида — общая для обоих потоков;
  - слоя `_patch222*.big` нет на боксе → числа CP, цен и урона временные.
