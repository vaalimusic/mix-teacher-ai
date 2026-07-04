# Mix Teacher AI

**Автор:** Артур Валиев

Mix Teacher AI - VST3-плагин для анализа дорожек и обучения сведению. Плагин ставится как обычный insert-эффект в DAW, показывает уровни, динамику, спектр, частотные зоны и даёт простые подсказки: что может быть не так, почему это мешает миксу и какой безопасный первый шаг попробовать.

Проект сделан на **C++ / JUCE / CMake** и ориентирован на Windows 10/11 и macOS с VST3-хостами.

## Возможности

- VST3 insert-effect для Windows и macOS.
- Анализ дорожки в реальном времени.
- Выбор источника: Auto, Vocal, Drums, Drums Bus, Kick, Snare, Bass, Guitar, Piano, Synth, FX, Master.
- Role mode: Track или Mix Hub.
- Peak, RMS, approximate LUFS short-term, crest factor, headroom, clipping count.
- Improved analysis: active-window detection, better LUFS approximation, 4x interpolated true peak estimate, sibilance spike score, transient density, stereo correlation/width and mono fold-down loss.
- Waveform, spectrum, RMS dynamics, частотные зоны и снимок спектра в момент проблемы.
- Rule-based подсказки с confidence, evidence и listen check.
- Русский язык по умолчанию, есть переключение на English.
- Выбор FFT buffer size: 1024/2048/4096/8192.
- Центральная ручка GOODIZER: на 0% звук не меняется, при повороте добавляет мягкую сатурацию/presence/soft-clip.
- Reset, Freeze, Copy JSON.
- JSON-отчёт для будущей AI/Ollama-интеграции.

## Философия

Плагин не пытается автоматически заменить инженера. Он помогает учиться:

```text
увидел проблему -> понял причину -> попробовал безопасный шаг -> послушал в миксе
```

Все советы задуманы как стартовая точка, а не финальное правило.

## Сборка

Требования:

- CMake 3.22+
- Visual Studio 2022 или Visual Studio Build Tools с C++ workload
- либо Visual Studio Insiders с C++ workload
- для macOS: Xcode Command Line Tools
- интернет при первой конфигурации, потому что JUCE подтягивается через CMake FetchContent

### Visual Studio Insiders

В проекте есть готовый скрипт:

```powershell
.\scripts\build_vs_insiders.bat
```

После сборки VST3 появляется здесь:

```text
build-release\MixTeacher_artefacts\Release\VST3\Mix Teacher.vst3
```

### Visual Studio 2022

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### macOS

```bash
./scripts/build_macos.sh
```

После сборки VST3 появляется здесь:

```text
build-macos/MixTeacher_artefacts/Release/VST3/Mix Teacher.vst3
```

## Установка VST3

Windows: скопируйте собранный плагин:

```text
build-release\MixTeacher_artefacts\Release\VST3\Mix Teacher.vst3
```

в стандартную папку VST3:

```text
C:\Program Files\Common Files\VST3
```

macOS: скопируйте собранный плагин:

```text
build-macos/MixTeacher_artefacts/Release/VST3/Mix Teacher.vst3
```

в стандартную папку VST3:

```text
/Library/Audio/Plug-Ins/VST3
```

После этого выполните rescan плагинов в DAW.

## Статус

Текущая версия: **MVP v0.2**

Готово:

- realtime-анализ;
- базовые и расширенные метрики;
- улучшенный active signal detection;
- улучшенный true peak estimate;
- stereo width/correlation и mono compatibility warning;
- spike-based sibilance detection;
- улучшенный transient detection для drums;
- tonal balance в Mix Hub;
- rule-based подсказки;
- drum-oriented анализ;
- регулируемый FFT buffer size для более спокойного спектра;
- JSON export;
- русский/английский интерфейс;
- GOODIZER как экспериментальный творческий эффект.
- Mix Hub MVP: несколько экземпляров на дорожках + один экземпляр на master для общей сводки.

Планируется:

- AI/Ollama-интеграция;
- deep analyze mode;
- сравнение before/after;
- улучшенная LUFS-метрика;
- более точный анализ сибилянтов и transient detection.

## Mix Hub

Mix Hub работает внутри того же VST3-плагина:

```text
1. На отдельных дорожках поставьте Mix Teacher AI и оставьте Role = Track.
2. Выберите Source: Vocal, Kick, Bass, Drums Bus и т.д.
3. На master bus поставьте ещё один Mix Teacher AI.
4. На master-инстансе выберите Role = Mix Hub.
```

Hub собирает данные с активных Track-инстансов и показывает:

- список дорожек;
- визуальную карту частотных конфликтов между дорожками;
- RMS/Peak по каждой дорожке;
- low/mid/high активность;
- предупреждения по headroom/clipping;
- частотные конфликты по диапазонам 40 Hz - 16 kHz;
- примерный конфликт kick/bass;
- вокал слишком впереди или теряется;
- вокал маскируется другими дорожками в 2-5 kHz;
- хэты/верх барабанов могут мешать вокалу;
- перегруженный низ от нескольких дорожек;
- общий диагноз картины: муть, резкий верх, доминирующий низ, отсутствие фокуса.

Подсказки Mix Hub стараются давать безопасный первый шаг: high-pass на второстепенных дорожках, dynamic EQ, лёгкий cut 1-3 dB, sidechain или выбор главного источника в спорном диапазоне.
Карта конфликтов показывает дорожки как узлы, линии между ними как пересечения, а нижняя шкала подсвечивает проблемные частотные зоны.

Ограничение MVP: обмен работает между экземплярами плагина внутри одного процесса DAW. Если хост запускает каждый плагин в отдельной sandbox-среде, Mix Hub может не увидеть Track-инстансы.

## Важно

GOODIZER меняет звук только если ручка больше 0%. Если нужен чистый анализатор без обработки, оставьте GOODIZER на 0%.

Build-папки (`build`, `build-release`) не нужно загружать в GitHub. Они уже исключены через `.gitignore`.
