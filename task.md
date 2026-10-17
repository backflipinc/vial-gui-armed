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
- [ ] Генератор таблиц кейкодов из Python
- [ ] KLE-парсер, раскладки
- [ ] Парсер выражений Any keycode
- [ ] HUD: layout, renderer, golden-тесты
- [ ] Темы, превью эффектов
- [ ] ScreenMedia

## 4. Оболочка UI
- [ ] Главное окно, тема Armed, навигация, статус связи, трей, пустой экран

## 5. Страницы Vial
- [ ] Keymap, Layout, Macros, Tap Dance, Combos, Key Overrides, Alt Repeat, QMK Settings, Matrix, Firmware, About

## 6. Страницы Armed
- [ ] Lighting, Themes, Device, Screen, HUD editor, Animation, HUD agent

## 7. Тесты и релиз
- [ ] Порт тестов, замеры, упаковка, README, 2.0.0, удаление Python
