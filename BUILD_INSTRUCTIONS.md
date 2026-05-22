# Ondes Martinot VST3 — Инструкция по сборке (macOS)

## Что понадобится

| Инструмент | Версия | Установка |
|---|---|---|
| **Xcode** | 14+ | App Store |
| **Xcode Command Line Tools** | — | `xcode-select --install` |
| **CMake** | 3.22+ | `brew install cmake` |
| **Git** | любая | встроен в macOS / `brew install git` |

> Homebrew (если не установлен): https://brew.sh

---

## Шаги сборки

### 1. Клонируй проект или скопируй папку

```bash
cd ~/Documents
# Если пришло архивом — просто распакуй. Если это git-репо:
# git clone <url> OndesMartinot
cd OndesMartinot
```

### 2. Настрой CMake (скачает JUCE автоматически)

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
```

> JUCE скачивается один раз (~600 МБ). После — кэшируется.
> На медленном интернете может занять 3–10 минут.

### 3. Собери плагин

```bash
cmake --build build --config Release -j$(sysctl -n hw.logicalcpu)
```

### 4. Готово

Флаг `COPY_PLUGIN_AFTER_BUILD TRUE` в CMakeLists.txt автоматически копирует плагин в:

```
~/Library/Audio/Plug-Ins/VST3/Ondes Martinot.vst3
```

Открой Ableton / Logic / Reaper — плагин появится в списке синтезаторов.

---

## Если CMake не нашёл Xcode

```bash
sudo xcode-select --switch /Applications/Xcode.app
```

## Если macOS блокирует плагин ("не верифицирован")

```bash
# Снять карантин:
xattr -r -d com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/Ondes\ Martinot.vst3

# Или в Finder: правая кнопка → Открыть → Открыть (один раз)
```

---

## Добавить AU (Audio Unit) формат

В `CMakeLists.txt` замени строку `FORMATS`:

```cmake
FORMATS  VST3 AU Standalone
```

AU требует Apple Developer ID для Distribution, но для локального использования работает без подписи.

---

## Структура проекта

```
OndesMartinot/
├── CMakeLists.txt
├── BUILD_INSTRUCTIONS.md
└── Source/
    ├── PluginProcessor.h    ← аудио движок, MIDI логика
    ├── PluginProcessor.cpp
    ├── PluginEditor.h       ← GUI
    └── PluginEditor.cpp
```

---

## Как пользоваться плагином

### Параметры

| Параметр | Описание |
|---|---|
| **HOLD** | ON — нота живёт после отпускания клавиши, пока колесо не опущено. OFF — «смычок»: нота только пока клавиша зажата. |
| **CC MIN** | CC1-значение ниже которого = полная тишина (по умолчанию 20) |
| **CC MAX** | CC1-значение выше которого = полная громкость (по умолчанию 120) |
| **GLIDE** | Максимальное время глайда (сек). Скорость глайда = velocity следующей ноты: velocity 127 = мгновенно, velocity 1 = максимально медленно. |
| **VOLUME** | Общая громкость |

### Механика звукоизвлечения

1. Зажми клавишу на MIDI-клавиатуре.
2. Медленно или резко поднимай колесо модуляции (CC1) — это и есть атака.
3. Держи колесо на нужной позиции — это sustain.
4. Опускай колесо — это release.
5. Aftertouch (давление на клавишу) добавляет вибрато.

### Глайд

Нажимай следующую ноту с нужным velocity:
- **velocity 127** — мгновенный переход (pizzicato-стиль)
- **velocity 64** — средний глайд
- **velocity 1** — медленный глайд (portamento)

---

## Что дальше (идеи для развития)

- [ ] Выбор формы волны (пила, квадрат, мягкий клип)
- [ ] Настраиваемый CC-номер (не только CC1)
- [ ] Фильтр с модуляцией от колеса
- [ ] Реверб / хор для имитации тембра оригинала
- [ ] Полифония (несколько голосов à la «кольцо» Волн Мартено)
