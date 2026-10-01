# Інтеграція CarPlay у панель приладів Audi Virtual Cockpit (AltScreen + Route Guidance)

[English](README.md) | **Українська**

Єдиний комплексний набір патчів CarPlay для систем Audi MHI2Q з цифровою панеллю приладів Audi Virtual Cockpit.  
Об'єднує **[MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen)** (трансляція відеопотоку мапи CarPlay на приладку) та **[mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi)** (3D-стрілки навігації turn-by-turn та рендерер маневрів) в єдину кодову базу зі зручним спільним інсталятором для SD-карт.

**Підтверджено та перевірено на:** Audi Q5 (FY) 2019 · `MHI2Q_ER_AUG22_P5092` · MU Software `1329`.

**Застереження:** Використовуйте на власний ризик. Ці патчі змінюють системні бінарні файли та конфігурації мультимедійного блоку. Завжди робіть повну резервну копію оригінальних файлів перед внесенням будь-яких змін. Автори не несуть відповідальності за пошкодження обладнання чи втрату гарантії.

## 🖼️ Галерея

**AltScreen: Відеопотік CarPlay (Google Maps) на панелі Virtual Cockpit**

<p align="center">
  <img src="assets/gallery/vc_altscreen_classic.jpg" width="45%" />
  <img src="assets/gallery/vc_altscreen_sport.jpg" width="45%" /><br />
  <sub>Відеопотік мапи CarPlay із накладанням підказок навігації на Audi Virtual Cockpit (перевірено на Audi Q5 FY, MU1329)</sub>
</p>

**Керування масштабом (зум) з коліщатка на кермі**

<p align="center">
  <img src="assets/gallery/zoom_demo.gif" width="45%" /><br />
  <sub>Зміна масштабу карти CarPlay безпосередньо лівим коліщатком керма</sub>
</p>

**Virtual Cockpit: підказки навігації від 3D-рендерера маневрів**

<p align="center">
  <img src="assets/gallery/maneuver_demo.gif" width="90%" /><br />
  <sub>Робота рендерера маневрів при веденні за тестовим маршрутом</sub>
</p>

<p align="center">
  <img src="assets/gallery/vc_day_nav.jpeg" height="200" />
  <img src="assets/gallery/vc_night_nav.jpeg" height="200" />
</p>
<p align="center">
  <img src="assets/gallery/vc_full_map.jpeg" height="200" />
  <img src="assets/gallery/vc_lane_guidance.jpeg" height="200" />
</p>

**Штатний парктронік PDC не перекриває CarPlay** · **Обкладинки треків на приладці**

<p align="center">
  <img src="assets/gallery/pdc_over_carplay.jpeg" width="45%" />
  <img src="assets/gallery/cover_art.jpeg" width="45%" />
</p>

**Проекційний дисплей (Head-Up Display)**

<p align="center">
  <img src="assets/gallery/IMG_0623.jpeg" width="30%" />
  <img src="assets/gallery/IMG_6302.jpeg" width="30%" />
  <img src="assets/gallery/IMG_0599.jpeg" width="30%" />
</p>

## 📍 Зміст

- [Галерея](#️-галерея)
- [Можливості](#-можливості)
- [Структура репозиторію](#️-структура-репозиторію)
- [Збірка](#-збірка)
- [Встановлення](#-встановлення)
- [Логування](#-логування)
- [Документація](#-документація)
- [Зворотний зв'язок](#-зворотний-звязок)
- [Посилання та подяки](#-посилання-та-подяки)

## ✨ Можливості

Жодних зайвих перемикачів: підключіть iPhone — CarPlay запуститься як зазвичай, а функції на панелі приладів підтягнуться автоматично.

- **AltScreen: Відеопотік CarPlay у Virtual Cockpit.** Виводить повноцінне відео другого екрана CarPlay (мапа Apple Maps, Google Maps) безпосередньо у Virtual Cockpit через displayable 3.
- **Плавне відео на приладці, 30 кадрів/с.** Оригінальний AltScreen показував ~15 кадрів/с: він копіював лише кожен другий декодований кадр, а сервіс дзеркалювання перевіряв нові кадри раз на 20 мс із жорстким періодом 33 мс. Збірка SD патчить обидва бінарники, і приладка показує 28–30 із 30 кадрів/с iPhone (перевірено в авто, `MIRROR_PRESENT_FPS` у STATUS).
- **Природні пропорції (без викривлення).** Сервіс дзеркалювання перезібрано з виправленою геометрією 1:1 та кадруванням знизу (clean bottom crop), тому відеопотік CarPlay не розтягується і не сплющується.
- **Чистий вигляд OEM (без водяних знаків та логотип Audi).** Сторонні рекламні водяні знаки видалено (`watermark.rgba` прозорий), а на екрані завантаження відображається фірмовий логотип Audi (`logo.rgba`).
- **Підказки turn-by-turn + 3D-рендерер маневрів.** 3D-стрілки поворотів у реальному часі, покажчик смуг руху, дистанція до маневру, залишок шляху та очікуваний час прибуття. Коли відеопотік AltScreen активний, модуль `ScreenModule` автоматично обирає **Display Context 81** (`{98, 101, 102, 3}`), накладаючи шар маневрів поверх живого відео. Підтримується навігаційними додатками, які надсилають дані CarPlay route guidance: Apple Maps, Google Maps, AMap (Waze ці дані не надсилає) ([деталі](docs/rgd/rgd-activation.md#-which-navigation-apps-send-route-guidance)).
- **Безшовний перехід (Context 80 / 74).** Якщо відеопотік AltScreen не готовий або вимкнений, система автоматично перемикається на **Context 80** (3D-стрілки поверх штатної навігаційної мапи Audi) або стан спокою **Context 74** ([деталі](docs/cluster/display-contexts.md)).
- **Назви вулиць на приладці.** Текстовий рядок вказує назву з'їзду або наступної дороги з плавною прокруткою. Натискання кнопки **OK** (ліве коліщатко на кермі) перемикає текст на час прибуття, і повертається назад через 20 с ([деталі](docs/rgd/vc-route-text.md)).
- **Проекційний дисплей (HUD).** Ті самі іконки маневрів, стрілки смуг та дистанція транслюються на проекційний екран на лобовому склі.
- **Зум з керма для мапи CarPlay та штатної мапи.** Коли на Virtual Cockpit активне відео CarPlay, кожне клацання лівого коліщатка надсилає в iPhone заводську команду AirPlay `changeMapZoomLevel` для приладки (штатна поведінка CarPlay). Обертання «від себе» віддаляє, «до себе» — наближає. Штатна мапа під відео також продовжує зумитися ([деталі](docs/input/steering-wheel.md)).
- **Вибір схеми розміщення мапи (центрування мітки авто).** Через те, що iOS резервує місце на приладці під власну картку маневру, мітка авто може зміщуватися. Меню MMI-Cockpit-Carplay в GEM містить перемикач **Cluster map layout** з 4 опціями: *AltScreen default*, *maneuver card on top*, *maneuver card on the right* та *no ETA* (застосовується після перепідключення телефону).
- **Sport-лейаут і зсув мапи.** У малому виді VC площина відео CarPlay отримує штатний зсув лейауту, як і рідна мапа: на Sport (один великий циферблат) вона зсувається на 476 px вліво, тож мапа й мітка авто видно ліворуч, а не за циферблатом. Пункт GEM **Cluster map shift** зсуває картинку ще лівіше для додатків, що ігнорують safe area приладки (Amap, Baidu); *Sport test* відтворює зсув Sport на Classic. Застосовується одразу.
- **Обкладинки альбомів на приладці.** Обкладинка поточного треку відображається на екрані медіа у Virtual Cockpit.
- **Вікно парктроніка не приховує CarPlay.** Коли вмикається штатний передній парктронік PDC, екран CarPlay залишається активним і не згортається ([деталі](docs/hmi/pdc-small-stage.md)).

## 🗂️ Структура репозиторію

| Шлях | Призначення |
| --- | --- |
| `hook/` | Вихідний код нативного перехоплювача `libcarplay_hook.so` |
| `java_patch/` | Вихідний код Java-патчів |
| `java_resources/` | Ресурси, що пакуються в jar (ширина гліфів / таблиця Unicode `vc-text.bin`) |
| `maneuver_render/` | GLES-рендерер маневрів (C та C++11 рушій `scene/`) |
| `common/` | Спільний код рендерера: робота з QNX Screen, кеш бінарників GL, мітки часу |
| `deploy/smartphone_integrator/` | Скрипти запуску та конфігурація дочірніх процесів для головного пристрою |
| `install_MoreIncredibleBash/`, `uninstall_MoreIncredibleBash/`, `logging_MoreIncredibleBash/` | Користувацькі скрипти M.I.B. для встановлення / видалення / збору логів |
| `altscreen/` | Дерево SD-карти AltScreen (відео CarPlay на приладці) з інтегрованим інсталятором RGI |
| `deploy/altscreen/` | Конфігурація процесу carplay для AltScreen (`CARPLAY_PRELOAD_EXTRA`) |
| `tools/` | Бінарні патчі AltScreen на 30 кадрів/с (`patch_altscreen_fps.py`) та аналізатор дампів H.264 приладки |
| `scripts/` | Точки входу для збірки в Docker (Java / hook / renderer / SD-карта) та хостові тести |
| `tests/` | Хостові тести (C, Java, Python) |
| `toolchain/qnx65-abi/` | Заголовки ABI QNX Screen для крос-компіляції |
| `docs/` | База знань у Markdown (підтримує перегляд в Obsidian) — детальні нотатки (відкрийте [`docs/INDEX.md`](docs/INDEX.md)) |
| `assets/` | Скріншоти та графічні матеріали |
| `build/` | Зібрані артефакти для встановлення |

## 🔧 Збірка

Для компіляції нативного коду потрібен образ крос-тулчейна QNX 6.5 ARMv7 від
[luka-dev/qnx65-armv7-toolchain](https://github.com/luka-dev/qnx65-armv7-toolchain):

```sh
git clone https://github.com/luka-dev/qnx65-armv7-toolchain
cd qnx65-armv7-toolchain
./host-scripts/qnx-run.sh build        # qnx65-armv7-toolchain:latest (GCC 8.5)
```

Після цього з кореня цього репозиторію:

```sh
./scripts/build_java.sh        # → build/carplay_hook.jar
./scripts/build_hook.sh        # → build/libcarplay_hook.so
./scripts/build_renderers.sh   # → build/maneuver_render
```

Усі три компоненти збираються у Docker без потреби встановлення тулчейнів на хост.

### Збірка повної SD-карти AltScreen (Відео на приладці + підказки навігації)

Один скрипт збирає всі бінарники та формує готову структуру картки:

```sh
STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh                  # -> build/sd/
SD=/Volumes/SD32 STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh   # ... і запис одразу на SD-карту
```

Скрипт автоматично вираховує розмір і контрольну суму JAR, прописує їх у перевірочні скрипти AltScreen і перераховує `SHA256SUMS-SD.txt`. Каталог із резервними копіями `MMI-Cockpit-Carplay/` на вашій карті залишається недоторканим.

Також збірка патчить `libcarplay_altscreen.so` і сервіс дзеркалювання AltScreen для відео 30 кадрів/с (`tools/patch_altscreen_fps.py`; кожен патч спершу перевіряє, що бінарник саме той, під який написаний). `ALTSCREEN_FULL_FPS=0` залишає їх без змін. Пункт GEM **Dump cluster video to SD (diagnostic)** зберігає останні ~4 МБ H.264-потоку приладки в `MMI-Cockpit-Carplay/logs/h264/`; `python3 tools/h264_ring_analyze.py <файл>` показує, що саме надіслав iPhone.

#### Головні переваги над оригінальним AltScreen:
- **30 кадрів/с замість 15:** копіюється кожен декодований кадр, а сервіс дзеркалювання перевіряє нові кадри кожні 4 мс (бінарні патчі накладає `build_sd.sh`).
- **Керування зумом карти з керма:** Кожне клацання коліщатка транслюється в команду AirPlay `changeMapZoomLevel` (`CRSUIClusterZoomAction`).
- **Меню вибору схеми розміщення (центрування мітки авто):** У GEM доступні 4 варіанти (`AltScreen default`, `maneuver card on top`, `maneuver card on the right`, `no ETA`) для оптимального центрування курсора на мапі.
- **Виправлена геометрія та пропорції 1:1:** Замість розтягування зображення застосовано кадрування знизу, завдяки чому мапа має природні пропорції.
- **Видалено водяні знаки:** Сторонні водяні знаки прибрано (`watermark.rgba` повністю прозорий).
- **Фірмовий логотип Audi:** Замість стороннього логотипу під час старту виводиться фірмовий логотип Audi (`logo.rgba`).
- **Спільна робота з RGI:** Скрипт `rgi_companion.sh` реєструє `CARPLAY_PRELOAD_EXTRA` для чистого поєднання обох бібліотек.

### Тести

Хостові тести (без магнітоли):

```sh
./scripts/run_tests.sh            # C + shell: парсер RGD, шина, обкладинки, кеш шейдерів, супервізор
./scripts/test_route_info.sh      # міст Java route-guidance / BAP до штатних інтерфейсів
./scripts/test_java_transports.sh # Java-шина, сокети рендерера
./scripts/test_maneuver_native.sh # рушій рендерера + смуги (macOS, ASan/UBSan)
```

## 🚀 Встановлення

**Сумісність та перевірене обладнання:**
- **Підтверджено та протестовано на:**
  - **Автомобіль:** Audi Q5 (FY) 2019
  - **Версія прошивки / Train:** `MHI2Q_ER_AUG22_P5092`
  - **Версія MU Software:** `1329`
- **Підтримувані блоки:** Будь-які блоки Audi MHI2Q (розроблено та протестовано на MU1316 та MU1329).
  Вимоги:
  - Повністю цифрова панель приладів (**Audi Virtual Cockpit**); аналогові приладки не підтримуються.
  - Бажано оновити блок до останньої доступної версії прошивки перед встановленням.

---

### Варіант 1: Повна SD-карта AltScreen + Route Guidance (Рекомендовано)

Встановлює одночасно **AltScreen** (відеопотік CarPlay на панелі приладів) та **Route Guidance Integration (RGI)** (3D-стрілки маневрів, смуги руху, текст вулиць, проекція на HUD) через вбудований MIB2 Toolbox.

1. **Зберіть образ на SD-карті:**
   ```sh
   STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh
   SD=/Volumes/SD32 STOCK_JAR=MU1329-base.jar ./scripts/build_sd.sh
   ```
2. **Вставте SD-карту** у слот 1 (SD1) магнітоли MMI.
3. **Відкрийте інженерне меню GEM:**
   - Перейдіть у **Toolbox -> Update Toolbox** для оновлення скриптів.
   - Перейдіть у **MMI-Cockpit-Carplay -> INSTALL**. Скрипт-компаньйон (`rgi_companion.sh`) автоматично встановить нативні бінарники RGI у `/mnt/app/root/hooks/`, розмістить JAR `carplay_hook-basevideo3.jar` та оновить конфігурації `smartphone_integrator.json` (з `CARPLAY_PRELOAD_EXTRA`) і `dio_manager.json`.
4. **Перезавантажте MMI** у штатному режимі.
5. **Відкрийте GEM -> MMI-Cockpit-Carplay -> START** для активації сервісу дзеркалювання.
6. **Знову перезавантажте MMI.**

**Перевірка роботи в авто:**
- **Зум з керма:** Запустіть CarPlay з навігацією на приладці та покрутіть ліве коліщатко на кермі («від себе» — віддалення, «до себе» — наближення).
- **Положення мітки авто:** Увімкніть ведення по маршруту в CarPlay, зайдіть у GEM -> **MMI-Cockpit-Carplay -> Cluster map layout** та перевірте 4 доступні варіанти, перепідключаючи телефон після вибору кожного, щоб обрати найкраще центрування мітки для вашого стилю приладки (Classic / Sport).
- **STATUS:** Пункт **MMI-Cockpit-Carplay -> STATUS** у GEM виводить повний звіт про стан системи: `DIO_PRELOAD_ALTSCREEN`, `DIO_PRELOAD_RGI`, `RGI_*` та активний контекст дисплея з файлу `/tmp/carplay_cluster.ctx`.
- **RESTORE ORIGINAL:** Пункт **RESTORE ORIGINAL** у GEM повністю видаляє модифікації та відновлює заводський стан із бекапу.

---

### Варіант 2: Лише навігаційні підказки (без відеопотоку AltScreen)

Для користувачів, яким потрібні лише 3D-стрілки навігації та покажчик смуг поверх штатної мапи Audi без трансляції відео екрана CarPlay.

Реліз складається з 8 файлів та 2 правок конфігурації (жоден штатний бінарник не замінюється):

| Шлях на магнітолі | Файли |
| --- | --- |
| `/mnt/app/root/hooks/` | `libcarplay_hook.so`, `maneuver_render`, `flag_atlas.rgba`, `carplay_startup.sh`, `carplay_monitor.sh`, `carplay_processes.sh`, `carplay_cleanup.sh` |
| `/mnt/app/eso/hmi/lsd/jars/` | `carplay_hook.jar` |
| `/mnt/system/etc/eso/production/smartphone_integrator.json` | `children.carplay` замінюється на [`carplay_child.json`](deploy/smartphone_integrator/carplay_child.json) |
| `/mnt/system/etc/eso/production/dio_manager.json` | `MessagesSentByAccessory` += `0x5200`, `0x5203`; `MessagesReceivedFromDevice` += `0x5201`, `0x5202`, `0x5204` |

- **Через M.I.B. (рекомендовано для окремого RGI):** Скопіюйте `install_MoreIncredibleBash/` на SD-карту M.I.B. і покладіть файли релізу в `mod/carplay/`, після чого виконайте **GEM -> M.I.B. -> Advanced Settings -> Run Custom Script**. Для видалення запустіть `uninstall_MoreIncredibleBash/`.
- **Вручну:** Через термінал root (SSH або Telnet). Див. детальну інструкцію у [`docs/deploy/install.md`](docs/deploy/install.md).

**Перезавантаження:** Відключіть CarPlay, виконайте `sync`, зачекайте кілька секунд і перезавантажте систему комбінацією кнопок. Перевірте логи в `/tmp`.

## 📝 Логування

Усі логи записуються в `/tmp` на магнітолі:

| Файл | Джерело |
| --- | --- |
| `/tmp/carplay_hook.log` | нативний хук (всередині `dio_manager`) |
| `/tmp/carplay_java.log` | Java-патч (з ротацією, `.1` = попередній) |
| `/tmp/maneuver_render.log` | рендерер маневрів на приладці |
| `/tmp/carplay_wrapper.log` | скрипт запуску, злиття прелоадів (`CARPLAY_PRELOAD_EXTRA`) та монітор |
| `/tmp/carplay_cluster.ctx` | активний контекст дисплея (`81` = відео + маневри, `80` = маневри на штатній мапі, `74` = спокій) |

За замовчуванням записуються лише попередження та помилки. Щоб увімкнути повний журнал (`INFO`), створіть маркерний файл:

```sh
touch /mnt/app/carplay_verbose        # зберігається після перезавантаження
```

Хук та Java зчитують маркер під час кожного підключення телефону — перезавантажувати пристрій не потрібно. Логи очищаються при перезавантаженні, тому скопіюйте їх перед перезапуском.

**Немає доступу до SSH? Використовуйте M.I.B.** Скопіюйте `logging_MoreIncredibleBash/` на карту і запустіть як інсталятор. Кожен запуск зберігає логи в `<card>/carplay_logs/NNN/` та створює маркер `/tmp/carplay_verbose`.

## 📚 Документація

Папка `docs/` містить детальну базу знань у форматі Markdown (зручно переглядати в Obsidian), де кожне твердження підтверджено кодом чи реверс-інжинірингом. Почніть з [`docs/INDEX.md`](docs/INDEX.md).

## 🤝 Зворотний зв'язок

Будемо вдячні за ваші відгуки, звіти про тестування на різних авто, виправлення помилок та покращення іконок маневрів.

## 🔗 Посилання та подяки

- [yuedizhibo/MHI2Q-CarPlay-AltScreen](https://github.com/yuedizhibo/MHI2Q-CarPlay-AltScreen) — проект трансляції відео CarPlay на приладку Virtual Cockpit, універсальний завантажувач та інсталятор MIB2 Toolbox (автори yuedizhibo та Lanye-z).
- [luka-dev/mib2q-carplay-rgi](https://github.com/luka-dev/mib2q-carplay-rgi) — інтеграція підказок навігації turn-by-turn, 3D-рендерер маневрів та протокол BAP.
- https://github.com/ludwig-v/wireless-carplay-dongle-reverse-engineering
- https://github.com/EthanArbuckle/iPhone18-3_26.1_23B85_Restore
- https://github.com/adi961/mib2-android-auto-vc
- [@fifthBro](https://t.me/fifthBro)
