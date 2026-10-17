# Vial Armed 2.0 — checklist

## 1. Каркас
- [x] `app/CMakeLists.txt`: C++20, Qt 6.5+, hidapi, liblzma (системные, иначе FetchContent)
- [x] Версия из git-тега (`app/cmake/Version.cmake`)
- [x] `main.cpp`: окно / `--agent` / `--linux-recorder` (режимы пока заглушки)
- [x] Ресурсы: иконка, шрифты, `qmk_settings.json`, svg-стрелки (из `src/main`)
- [x] Qt Test: smoke-тест
- [x] CI: сборка + тесты на Linux / Windows / macOS (`.github/workflows/app.yml`) — Windows/macOS не проверены до пуша

## 2. Транспорт и протоколы
- [x] `RawHid` + `Link` (ретраи USB/донгла 1:1) + `Session` (поток, приоритеты, склейка по ключу)
- [x] Обнаружение устройств + hotplug (udev на Linux, опрос 2 с на Windows/macOS)
- [x] `vial::` протокол
- [x] `armed::` протокол + общий с Python кэш списков
- [x] Загрузчик Vial (`vibl::`)
- [x] Тесты: link, session, vial, armed (пакеты HUD побайтно = Python); `hwprobe` на железе

## 3. Модели данных
- [x] Генератор таблиц кейкодов из Python + реестр (паритет: все 65536 кодов, v5/v6)
- [x] KLE-парсер, определение клавиатуры, модель клавиатуры, .vil (на железе = Python)
- [x] Парсер выражений Any keycode
- [x] Макросы, динамические записи, QMK Settings
- [x] HUD: layout, renderer, golden-тесты (попиксельно; 0.08 мс против 13 мс в Python)
- [x] Темы, рендер тем, превью эффектов и световой полосы (паритет)
- [x] ScreenMedia (поток = Python; 152 кадра за 26 мс против ~3.5 с)

## 4. Оболочка UI
- [x] Главное окно, тема Armed (контраст AA, фокус), навигация (Ctrl+1…9), подстраницы, статус USB/2.4G, замок, трей, пустые состояния, тосты, меню, .vil загрузка/сохранение, VIA-определения
- [x] Отмена обмена при закрытии/выходе (выход мгновенный); старт ~0.19 с против ~0.65 с

## 5. Страницы Vial
- [ ] Keymap, Layout, Macros, Tap Dance, Combos, Key Overrides, Alt Repeat, QMK Settings, Matrix, Firmware, About

## 6. Страницы Armed
- [ ] Lighting, Themes, Device, Screen, HUD editor, Animation, HUD agent

## Оптимизации на потом
- [ ] Конвейерные запросы при загрузке по USB для qmk-armed (сейчас 1 с, как в Python: упирается в USB)

## 7. Тесты и релиз
- [ ] Порт тестов, замеры, упаковка, README, 2.0.0, удаление Python
