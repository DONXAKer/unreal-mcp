---
name: reference-style-guide
description: Где лежит арт-стиль-гайд WarCard и его ключевые опорные значения (палитра, шрифты, размеры) для визуального ревью
metadata:
  type: reference
---

Эталон визуального стиля: `D:\WarCard\client\Documentation\ArtDirection_StyleGuide.md`.
Стиль — «Military Gritty» (тёмный, металл, камуфляж; референсы WoT/War Thunder/Arma 3).

Опорные значения для проверки кадров (могут меняться — сверяться с гайдом перед выводами):
- Палитра §1 (обновл. 2026-06-02, тёплый gunmetal-olive, НЕ синий): фон `MainBackground #15150f` / `DeepBackground #0d0d0b` / панели `PanelBackground #21211a`; акцент/золото `#f39c12`; фракции Red `#c0392b` / Blue `#2980b9`. Раньше гайд был сине-фиолетовый — теперь намеренно тёплые нейтрали.
- Шрифты §2: Oswald (заголовки/кнопки ALL CAPS), Roboto Condensed (body), Roboto Mono (числа HP/урон/таймер), Black Ops One (логотип WARCARD, золото 48px).
- Размеры §11: ячейка поля 56×56, карта действия 120×160, карточка юнита 140×200.
- Обязательные блоки экранов — §4 (Login 4.1, MainMenu 4.2, Draft 4.4, Deployment 4.5, Battle/ActionCards 4.7, GameResult 4.9).
- Читаемость §15: WCAG AA; Caption не ниже `TextSecondary`; фракцию дублировать вторым каналом (метка RED/BLUE) — не только цветом.

Признак «прототип, а не игра»: ровный плоский фон без паттерна/vignette, плейсхолдерные цвета (сиреневый/розовый вместо палитры §1), наложенный/слипшийся текст.
См. [[reference-playtest-run-layout]].
