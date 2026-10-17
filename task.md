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
- [x] Keymap (палитра кейкодов нарисована целиком + поиск, автоподгонка, PgUp/PgDn, Del, Esc), Layout
- [x] Macros (строки действий, текстовый редактор, запись: evdev-хелпер на Linux, хук на Windows)
- [x] Tap Dance, Combos, Key Overrides (исправлены подписи бит 4/5), Alt Repeat Key — список + живой редактор
- [x] QMK Settings, Matrix Tester, Firmware (vibl, восстановление раскладки), Lighting (VialRGB/QMK), About
- [x] Разблокировка (асинхронная), .vil через контроллер
- [ ] «Load dummy JSON» (клавиатура-пустышка для отладки) — не перенесено

## 6. Страницы Armed
- [x] Lighting (живое превью, темы, полоса), Theme editor, Device, Screen settings, Animation
- [x] HUD editor: LAYOUT (холст, список, свойства, выравнивание, undo, пресеты = Python, импорт/экспорт, отправка с дебаунсом 120 мс и диффом на потоке устройства) и AGENT
- [x] HUD agent: в приложении (фоновые задания сессии, пауза на время прошивки) и `--agent` без окна; автозапуск Linux/Windows

## Оптимизации на потом
- [ ] Конвейерные запросы при загрузке по USB для qmk-armed (сейчас 1 с, как в Python: упирается в USB)

## Сверх плана
- [x] Темы: KEYMAP LAYER у слоя (ALL KEYS / CUSTOM / FN1–FN3, прошивка сама следит за ремапом), SHOW по активному слою (qmk-armed)

## 7. Тесты и релиз
- [x] Порт тестов: keyboard, GUI на виртуальной клавиатуре, холст HUD, макросы, HUD layout (15 наборов)
- [x] Замеры: docs/benchmarks.md
- [x] Упаковка: AppImage и .deb (Qt внутри, udev), установщик и zip для Windows, dmg для macOS, релиз CI по тегам v2.* — Linux проверен локально, Windows/macOS только в CI после пуша
- [x] README и скриншоты
- [ ] 2.0.0: тег vial-gui-armed, qmk-armed 1.9.0, io-type84-pro-qmk — ждёт подтверждения
- [ ] Удаление Python-приложения — ждёт подтверждения
