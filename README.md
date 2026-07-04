# Mix Teacher AI

**Автор:** Артур Валиев

Mix Teacher AI - VST3-плагин для анализа дорожек и обучения сведению. Плагин ставится как обычный insert-эффект в DAW, показывает уровни, динамику, спектр, частотные зоны и даёт простые подсказки: что может быть не так, почему это мешает миксу и какой безопасный первый шаг попробовать.

Проект сделан на **C++ / JUCE / CMake** и ориентирован на Windows 10/11 и macOS с VST3-хостами.

## Возможности

- VST3 insert-effect для Windows и macOS.
- Анализ дорожки в реальном времени.
- Выбор источника: Auto, Vocal, Drums, Drums Bus, Kick, Snare, Bass, Guitar, Piano, Synth, FX, Master.
- Peak, RMS, approximate LUFS short-term, crest factor, headroom, clipping count.
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
- rule-based подсказки;
- drum-oriented анализ;
- регулируемый FFT buffer size для более спокойного спектра;
- JSON export;
- русский/английский интерфейс;
- GOODIZER как экспериментальный творческий эффект.

Планируется:

- AI/Ollama-интеграция;
- deep analyze mode;
- сравнение before/after;
- Mix Hub для нескольких дорожек;
- улучшенная LUFS-метрика;
- более точный анализ сибилянтов и transient detection.

## Важно

GOODIZER меняет звук только если ручка больше 0%. Если нужен чистый анализатор без обработки, оставьте GOODIZER на 0%.

Build-папки (`build`, `build-release`) не нужно загружать в GitHub. Они уже исключены через `.gitignore`.
