# Известные проблемы драйверов Mali и PowerVR (GLES 3 и Vulkan) для рендера SAGE DX8

- Автор: «Android-платформа», 02.10.2026. Статус: черновик к ADR-0002.
- Контекст: рендер по умолчанию из форка MYSOREZ — свой D3D8→GLES 3.0 (`Core/Libraries/Source/d3d8gles`, генерирует GLSL из fixed-function состояния D3D8). Опционально GLES через ANGLE и D3D8→DXVK 2.6→Vulkan. gl4es не используется.
- Ссылки на форк: `B = https://github.com/MYSOREZ/GeneralsZH-Android-Port/blob/8ef2349`.
- Списки багов эмуляторов, на которые ссылаюсь: Dolphin `Source/Core/VideoCommon/DriverDetails.{h,cpp}` (коммит `24c80c5`, https://github.com/dolphin-emu/dolphin/blob/24c80c5a32d9429bcd8e7b0b90d7b61763782009/Source/Core/VideoCommon/DriverDetails.h); PPSSPP `Common/GPU/thin3d.h`, `OpenGL/thin3d_gl.cpp`, `OpenGL/GLFeatures.cpp`, `Vulkan/thin3d_vulkan.cpp` (коммит `a2f4ce2`, https://github.com/hrydgard/ppsspp/tree/a2f4ce214f224c6c26b3319409ec50205ff8253d/Common/GPU).
- Правило: только то, что нашёл в источниках. Где источника нет, так и написано. Версии драйверов Pixel в этих списках обычно не указаны («-1» в Dolphin означает «все версии»), поэтому каждый пункт **проверяем на Pixel 8 Pro**, а не принимаем на веру.

## Сводка: топ-5 для нас

| # | Проблема | GPU | Обход |
|---|---|---|---|
| 1 | Нет BC/DXT, а текстуры SAGE в DXT1/3/5 | Mali, PowerVR, Xclipse, стоковые Adreno | декод в RGBA8 (сейчас), позже перекодировка в ETC2/ASTC |
| 2 | `mediump` = FP16 во фрагментном шейдере, UV «дрожат» | Mali, PowerVR | `highp` для UV и текстурной математики |
| 3 | Стопоры при обновлении динамических буферов | Mali (и Adreno) | кольцо из 3 буферов + fence, обновления пачкой |
| 4 | Тайловый рендер: инкрементальные проходы, лишняя запись глубины | все мобильные, особенно Mali | 1 bind на FBO за кадр, полный glClear, glInvalidateFramebuffer |
| 5 | Vulkan на PowerVR (Pixel 10): артефакты, слабая производительность, форматы | PowerVR DXT/BXM | только GLES по умолчанию, Vulkan по списку драйверов ≥ 25.1 |

---

## 1. Сжатие текстур: BC/DXT против ETC2/ASTC

- **Симптом:** без декодирования текстуры чёрные или магента. В форке `[d3d8gles] MAGENTA:` означает неподдержанный формат (README «Known issues»). У fadi-labib на Xclipse фон шелл-карты был чёрным.
- **Затронуто:** Mali и Xclipse (`textureCompressionBC = FALSE`), PowerVR (по комментарию форка), стоковые Adreno. Форк: «Most Android GPUs (Mali, Adreno, PowerVR) don't expose GL_EXT_texture_compression_s3tc».
- **Обходы:**
  - (а) Программный декод BC1–3 в RGBA8 при загрузке, как в форке (`gles_pipeline.cpp#L1235`, проверка расширения `#L436`). Работает, но память под текстуры растёт ×4 (DXT3/5) или ×8 (DXT1), а на телефоне память общая. Учесть в бюджете ОЗУ.
  - (б) Перекодировка один раз (при импорте или на ПК) в формат GPU. У fadi-labib: DXT→RGBA8 с переупаковкой одного `.big`, 3496 текстур, 1014 МБ. Отдельные файлы вместо архива дали загрузку ~5 мин вместо 7 с из-за FUSE. Для d3d8gles лучше ETC2 (входит в ядро GLES 3.0: `GL_NUM_COMPRESSED_TEXTURE_FORMATS` не меньше 10, это ETC2/EAC) или ASTC (`GL_KHR_texture_compression_astc_ldr`, проверять расширение). Ограничение: у ETC2/ASTC нет токена D3D8, поэтому формат пройдёт только через d3d8gles, не через DXVK.
  - (в) В Vulkan-пути: JIT-декодирование в слое (как Vortek/bcn_layer), дорого по CPU.
  - Альфа DXT1 (punch-through) после перекодировки проверить глазами (листва).
- **Источники:** https://github.com/fadi-labib/Generals-Android/issues/9 ; https://leegao.github.io/winlator-internals/2025/06/02/Vortek2.html ; B/Core/Libraries/Source/d3d8gles/src/gles_pipeline.cpp#L1133-L1240 ; B/README.md (Known issues) ; https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glGet.xhtml

## 2. Точность в шейдерах (precision)

- **Симптом:** дрожащие или ступенчатые UV на больших и повторяющихся текстурах (террейн, вода, дороги), полосы, ошибки при больших координатах.
- **Затронуто:** Mali и PowerVR. `mediump` здесь настоящий FP16 (диапазон ±65504, 10 бит мантиссы). На PowerVR Rogue/Volcanic `lowp` = `mediump` = FP16. Старые PowerVR SGX: PPSSPP помечает «bad/terrible precision» (`GLFeatures.cpp`, `BUG_PVR_SHADER_PRECISION_BAD`). Для Rogue автор сам пишет «not sure», то есть флаг выставлен эвристически.
- **В форке:** VS `precision highp float` (#L843), FS `precision mediump float` (#L962), так что UV-varyings во FS получаются mediump.
- **Обход:** во FS явно `highp` для `vUV*` и любой текстурной математики, `mediump` для цвета и тумана. Позиции и матрицы только `highp` (Imagination). Arm: mediump для UV годится лишь примерно до 512×512 без сильного повторения. Масштабирование и сдвиги UV делать в VS. Не смешивать точности внутри одного выражения (Imagination: цена упаковки и распаковки).
- **Источники:** https://docs.imgtec.com/performance-guides/graphics-recommendations/html/topics/demystifying-precision.html ; Arm GPU Best Practices Developer Guide (https://developer.arm.com/documentation/101897/latest/) ; PPSSPP `GLFeatures.cpp` (a2f4ce2).

## 3. Динамические буферы: glMapBufferRange, orphaning, glBufferSubData

- **Симптом:** время кадра растёт пропорционально числу 2D/UI-вызовов и частиц (стоп CPU на GPU). Либо «мусорные» треугольники при неверном orphaning.
- **Затронуто:**
  - Dolphin `BUG_BROKEN_UNSYNC_MAPPING` (ARM, все версии): «The Mali behavior is even worse: They just ignore the unsychronized flag and stall the GPU», обход — отдельный буфер на вызов через BufferData.
  - `BUG_BROKEN_BUFFER_STREAM` (ARM и IMGTEC, GL): стопоры и OOM на `glBufferSubData`/`glMapBufferRange`. Описание относится к Adreno a3xx/Mali-T6xx, но запись в таблице без ограничения версии.
  - PPSSPP: `ANY_MAP_BUFFER_RANGE_SLOW` выставлен всем не-NVIDIA, «because it murders performance on Mali».
  - Arm (официально): при конфликте драйвер делает ghosting (копию ресурса) или ждёт GPU. `GL_MAP_UNSYNCHRONIZED_BIT` можно использовать только для области, которую GPU уже не читает. Рекомендовано N-буферизация (обычно 3 копии на Android) с проверкой через fence. На старых драйверах Mali избегать `GL_MAP_INVALIDATE_*_BIT`.
- **В форке:** NOOVERWRITE переводится в `glMapBufferRange(..., GL_MAP_WRITE_BIT | GL_MAP_UNSYNCHRONIZED_BIT)` (#L2260, #L2321). DISCARD переводится в полный `glBufferData` с данными: orphaning с `nullptr` дал «stray grids and long diagonal lines», потому что SortingRenderer ссылается на старые диапазоны кольца. Замеры форка: ~1 мс на каждый `Render2DClass::Render()` до исправления.
- **Обход для нас:** кольцо из 3 VBO/IBO на кадр с `glFenceSync`/`glClientWaitSync(…, 0)`; все обновления до отрисовок; статику держать отдельно от летучих данных. Замер на Pixel 8 Pro: есть ли стопоры при UNSYNCHRONIZED на текущем драйвере Mali (r5x).
- **Источники:** https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-performance-6-efficiently-updating-dynamic-resources ; Dolphin DriverDetails.h/.cpp (24c80c5) ; PPSSPP thin3d_gl.cpp (a2f4ce2) ; B/Core/Libraries/Source/d3d8gles/src/gles_pipeline.cpp#L2205-L2330

## 4. Тайловый рендер, glClear и glInvalidateFramebuffer

- **Симптом:** нет артефактов, но лишний трафик памяти, нагрев и падение FPS. Arm оценивает инкрементальный рендер в +400% трафика к framebuffer.
- **Причины:** повторный bind того же FBO за кадр; частичный clear (с scissor); нет `glInvalidateFramebuffer` для глубины и трафарета до unbind (вызов в начале следующего кадра «too late»); чтение framebuffer посреди кадра.
- **Затронуто:** Mali (официально), по словам Arm то же у большинства мобильных GPU. PPSSPP и Vulkan Guide считают тайловыми ARM, IMGTEC и Qualcomm.
- **Обход:** каждый FBO (сцена в пониженном разрешении, render-to-texture движка: тени, отражения воды **[в BFME1 не проверено]**) привязывать один раз и рендерить целиком; в начале полный `glClear` всех вложений; перед unbind `glInvalidateFramebuffer(GL_FRAMEBUFFER, 2, {GL_DEPTH_ATTACHMENT, GL_STENCIL_ATTACHMENT})`; не делать `glReadPixels`/`glCopyTexImage` посреди кадра. **Багов драйверов** с самим `glInvalidateFramebuffer` в источниках не нашёл, речь о правильном использовании.
- **Источники:** https://developer.arm.com/community/arm-community-blogs/b/mobile-graphics-and-gaming-blog/posts/mali-performance-2-how-to-correctly-handle-framebuffers ; https://docs.vulkan.org/guide/latest/tile_based_rendering_best_practices.html

## 5. Vulkan на PowerVR (Tensor G5 / Pixel 10)

- **Симптомы и факты:**
  - Через DXVK магента, искажённая геометрия и испорченные меню на PowerVR BXM и Samsung Xclipse, «nothing above the driver can fix» (форк, ANDROID_PORT.md §2, README).
  - Pixel 10 вышел с драйвером 1.602.400 (v24.3). В Android 16 QPR3 Beta 1 он обновлён до 1.634.2906 (v25.1, Vulkan 1.4). Vulkan всё равно «performs very poorly on this GPU compared to OpenGL» (Android Authority).
  - Flutter/Impeller: часть версий драйвера Pixel 10 отказывала в Vulkan-контексте по версии движка; производитель GPU называет безопасным драйвер ≥ 25.1.
  - Eden: на драйвере 1.634.2906 неподдержанные форматы Vulkan, графические ошибки и падение по памяти.
  - Dolphin (IMGTEC, Vulkan): `BUG_BROKEN_CLEAR_LOADOP_RENDERPASS`, обход — явная очистка вместо LOAD_OP_CLEAR.
  - PPSSPP (IMGTEC, Vulkan): `PVR_BAD_16BIT_TEXFORMATS` («just ban»), `verySlowShaderCompiler = true`. Для DX8 это важно: движок использует R5G6B5, A4R4G4B4 и A1R5G5B5.
  - Dolphin на Pixel 10 и в GL: падения при сглаживании, в Vulkan пропадающие текстуры (Android Authority).
- **Обход:** на PowerVR по умолчанию только GLES; Vulkan скрыт или по списку при `driverVersion ≥ 25.1`, и только по явному выбору; 16-битные форматы расширять до RGBA8 (в Vulkan-пути обязательно, в GLES проверить); MSAA на PowerVR по умолчанию выключено; прогрев шейдеров (кэш программ `glProgramBinary` / pipeline cache).
- **Источники:** B/docs/port/ANDROID_PORT.md §2 ; https://www.androidauthority.com/pixel-10-gpu-update-android-16-qpr3-beta-1-3626234/ ; https://www.androidauthority.com/android-16-qpr3-beta-1-game-test-3626393/ ; https://www.androidauthority.com/pixel-10-game-emulation-test-3605149/ ; https://github.com/flutter/flutter/issues/179812 ; https://github.com/eden-emulator/Issue-Reports/issues/398 ; Dolphin DriverDetails.h (24c80c5) ; PPSSPP thin3d_vulkan.cpp (a2f4ce2).

---

## 6. Прочие известные проблемы (менее приоритетные)

| Проблема | API | GPU | Симптом / суть | Обход | Источник |
|---|---|---|---|---|---|
| Нет `shaderClipDistance` | Vulkan | Mali | создание конвейера с `gl_ClipDistance` падает | убирать ClipDistance из SPIR-V (Vortek; возможны артефакты) или отсекать по-другому | Vortek2 (ссылка выше) |
| `NO_DEPTH_CANNOT_DISCARD_STENCIL_MALI` | Vulkan | Mali (до драйвера ~40) | depth write при depth test NEVER роняет драйвер, сломан discard трафарета | не комбинировать, по PPSSPP | PPSSPP thin3d_vulkan.cpp |
| `MALI_CONSTANT_LOAD_BUG` | Vulkan | Mali, драйвер 32–39 | ошибки загрузки констант (PPSSPP #15661) | обход PPSSPP | там же |
| `EMPTY_RENDERPASS_BROKEN_MALI` | Vulkan | один драйвер Mali (0xaa9c4b29) | пустой render pass ломает рендер | не создавать пустые проходы | там же |
| `BUG_PRIMITIVE_RESTART` | Vulkan | ARM | primitive restart сломан | не использовать | Dolphin DriverDetails |
| `BUG_BROKEN_VECTOR_BITWISE_AND` | GL и Vulkan | ARM | `&` между int-векторами даёт константу | покомпонентно | Dolphin DriverDetails |
| `BUG_SLOW_CACHED_READBACK_MEMORY` | Vulkan | ARM | чтение из кэшируемой памяти медленное | coherent-память для readback | Dolphin DriverDetails |
| `BUG_BROKEN_VSYNC` | GL | ARM (Galaxy S6, Android 5.1.1) | `eglSwapInterval(0)` замораживает экран | не ставить интервал 0 (у нас Swappy, интервал ≥ 1) | Dolphin DriverDetails |
| `PVR_GENMIPMAP_HEIGHT_GREATER` | GL | PowerVR | `glGenerateMipmap` при высоте больше ширины | генерировать мипы на CPU или при импорте | PPSSPP thin3d_gl.cpp |
| `BUG_BROKEN_BITWISE_OP_NEGATION` | GL | IMGTEC (до 108.4693462) | отрицание побитовых операций | обход Dolphin | Dolphin DriverDetails |
| Чтение R32F/R32UI через image binding | GLES 3.2 | Pixel 10 | возвращает 0 без GL_NEAREST | нас не касается (GLES 3.0, без image load/store) | https://forums.imgtec.com/t/pixel-10-opengl-bug-cannot-read-from-r32f-r32ui-textures-via-image-binding/4235 |
| Вертикальный переворот кадра при pre-rotation | GLES через ANGLE | Mali-G76 (Redmi Note 8 Pro) | весь кадр перевёрнут при повороте поверхности на 180° | `ANGLE_FEATURE_OVERRIDES_DISABLED=enablePreRotateSurfaces` (так в форке) | B/GeneralsMD/Code/Main/SDL3Main.cpp#L1095-L1124 |
| Полоса по краю экрана / разрывы | GLES (системный и ANGLE) | разные | EGL-конфиг без альфы; vsync молча не включился | RGBA8888-конфиг, логировать результат SwapInterval (так в форке) | SDL3Main.cpp#L1176-L1195, gles_pipeline.cpp#L380-L400 |

## 7. sRGB, лимиты uniform и varyings

- **sRGB:** D3D8-рендер SAGE работает в «гамма-пространстве» без sRGB-текстур и framebuffer. Держим линейный RGBA8 (как D3D8), `EGL_GL_COLORSPACE_SRGB` не запрашиваем и sRGB-форматы при перекодировке не используем, иначе изменится яркость. **Конкретных багов драйверов Mali/PowerVR с sRGB в источниках не нашёл.**
- **Лимиты GLES 3.0 (минимумы спецификации):** `MAX_VERTEX_UNIFORM_VECTORS` ≥ 256, `MAX_FRAGMENT_UNIFORM_VECTORS` ≥ 224, `MAX_VARYING_VECTORS` ≥ 15, `MAX_VERTEX_ATTRIBS` ≥ 16, `MAX_TEXTURE_IMAGE_UNITS` ≥ 16, `MAX_TEXTURE_SIZE` ≥ 2048, `MAX_UNIFORM_BLOCK_SIZE` ≥ 16384 (https://registry.khronos.org/OpenGL-Refpages/es3.0/html/glGet.xhtml). Генератор d3d8gles использует 2 текстурные стадии и несколько varyings (vCol, vSpec, vUV0, vUV1, vFogDepth), то есть далеко от лимитов. Если BFME1 потребует больше стадий или вершинных шейдеров (vs.1.1), сверить с этими минимумами **[объём констант vs.1.1 по документации D3D8 не сверял]**. Imagination: большие массивы uniform уходят из регистров в память, лишние varyings повышают давление на регистры.

## 8. Что проверить на Pixel 8 Pro в Э1 (для QA)
1. Лог `[d3d8gles] GLES3 context ready … (s3tc=?)`, строки GL_RENDERER/GL_VERSION, драйвер Mali.
2. Террейн на крупном масштабе: дрожание UV (п. 2), сравнить mediump и highp.
3. Время кадра в сценах с UI и частицами (п. 3), `gx_perf.txt` из форка.
4. Пропускная способность памяти и нагрев (п. 4): Android GPU Inspector / Perfetto.
5. Режим 16 КБ страниц (параметры разработчика): запуск без page-size compat.

---

## Решение Лида 02.10.2026

Закреплено в `docs/adr/ADR-0002-target-devices.md` §3, §5, §8:
- №1 Текстуры: в Э1 CPU-декод DXT в RGBA8 и замер PSS (E1-41). Перекодировку в ETC2 при импорте включаем по триггеру: PSS выше бюджета или загрузка карты > 30 с. Кодировщик и кэш делают «Форматы и данные» (E1-27), загрузку в d3d8gles делает Android-платформа (E1-18). Внутренний FOURCC только в d3d8gles, CPU-декод остаётся запасным путём, ASTC позже. Альфу DXT1 проверяет скриншот-тест QA.
- №2 Точность: во FS `highp` для UV и текстурной математики, `mediump` для цвета и тумана (E1-16).
- №3 и №4: кольцо буферов с fence; один bind FBO за кадр, полный clear, `glInvalidateFramebuffer`. Обязательно для FBO сцены в Э4, замеры на Pixel 8 Pro в Э1.
- №5 PowerVR: только GLES по умолчанию, Vulkan скрыт или доступен по списку с драйвера ≥ 25.1; MSAA на PowerVR выключено.
- №7 sRGB: линейный RGBA8, `EGL_GL_COLORSPACE_SRGB` не запрашиваем.
- §8 входит в матрицу QA (ADR-0002 §10) и в задачу E1-41.
