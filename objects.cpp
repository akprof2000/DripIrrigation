// objects.cpp 🌱💧 Определения глобальных объектов и переменных проекта
#include "objects.h"
#include "valves.h"  // 🚰 countValveOpen(), valveIsDraining() — для контроля засора фильтра
#include "log.h"

// ⏱️ Момент запуска насоса в millis() (для защиты по таймауту и гейта замера потока).
//    Намеренно millis(), а НЕ unix-время: защита насоса не должна зависеть от
//    синхронизации NTP/RTC (иначе при несинхронных часах таймаут не сработает).
//    0 = насос не в пусковом режиме.
unsigned long pumpStart = 0;

// ⚙️ Глобальная конфигурация системы полива (хранится на SD-карте)
Config myConfig;

// 💾 Менеджер файловой конфигурации: путь /configuration.dat, тип 'B' (бинарный)
FileData data(&SD, "/configuration.dat", 'B', &myConfig, sizeof(myConfig));

// 🔐 Кодовое слово для первичной регистрации администратора (генерируется в портале)
char tstr[32] = "";

// 🔄 Флаг запроса на перезагрузку ESP (устанавливается через Telegram)
bool res = false;

// 🤖 Экземпляр Telegram бота (FastBot2)
FastBot2 bot;

// 🆘 Флаг ошибки соединения с серверами Telegram (3 или 4 из tick())
bool botHasError = false;

// 💧 Менеджер 8-канальных датчиков влажности почвы
HumiditySensors hs;

// 📡 Флаг потери WiFi соединения
bool dropped = false;

// 🌧️ Флаг обнаружения дождя
bool rainNow = false;

// 🌙 Флаг ночного времени
bool nightNow = false;

// 🚰 Предыдущие состояния клапанов (для отслеживания изменений и отправки уведомлений)
byte oldMode[NUM_CHANNELS] = { 0 };  // 🔢 все каналы в состояние VState::Init

// 📡 Указатель на функцию отправки статуса в Telegram
void (*p_sendTelegramFunction)(String text);

// ============================================================
// 💧 Переменные для замера расхода воды через датчик потока
// ============================================================
// 🔧 Калибровочный коэффициент: импульсов на 1 литр
// Для YF-S201 ≈ 450 импульсов/литр, для YF-S402 ≈ 980 импульсов/литр
#define FLOW_PULSES_PER_LITER 450.0

// 🔄 Счётчик импульсов датчика потока (volatile — обновляется в прерывании)
volatile unsigned long flowPulseCount = 0;


// ============================================================
// ⚡ ISR: Обработчик прерывания от датчика потока воды
// ============================================================
// Вызывается при каждом импульсе на пине FLOW_SENSOR (27)
void IRAM_ATTR onFlowPulse() {
  flowPulseCount+=1;  // 🔄 Увеличиваем счётчик импульсов
}

// ============================================================
// 🔌 Инициализация датчика потока воды
// ============================================================
void flowInit() {
  pinMode(FLOW_SENSOR, INPUT_PULLUP);  // 🔌 Настраиваем пин как вход с подтяжкой
  
  flowPulseCount = myConfig.pulses;
  // ⚡ Подключаем прерывание по спадающему фронту (FALLING) — датчик замыкает на GND
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR), onFlowPulse, FALLING);
  LOG_I("Датчик потока воды инициализирован (пин %d)", FLOW_SENSOR);
}

// ============================================================
// 🔄 Сброс счётчика текущей сессии полива
// ============================================================
// Вызывается при открытии первого клапана — начинаем новый замер
void flowResetSession() {
  flowPulseCount = 0;  // 📝 Сохраняем стартовое значение
  myConfig.pulses = 0;
  myConfig.flowSessionLiters = 0.0;                 // 💧 Обнуляем расход сессии
  LOG_I("Счётчик расхода сброшен — новая сессия полива");
  data.updateNow();
}

// ============================================================
// 💧 Получить расход воды за текущую сессию полива
// ============================================================
float flowGetSessionLiters() {
  // 📟 Считаем разницу импульсов с момента сброса и переводим в литры
  myConfig.pulses = flowPulseCount;
  return myConfig.pulses / FLOW_PULSES_PER_LITER;
}

// ============================================================
// 📊 Получить общий накопленный расход воды
// ============================================================
float flowGetTotalLiters() {
  return myConfig.flowTotalLiters;
}

void flowGetSessionLitersTick()
{
    myConfig.flowSessionLiters = flowGetSessionLiters();  // обновляет myConfig.pulses = flowPulseCount
    // 💾 Пишем конфиг на SD только если реально текла вода (счётчик импульсов изменился).
    // Это покрывает и полив через клапаны, и пролив дренажа (spillage гоняет воду
    // без открытия канальных клапанов), но не плодит лишние перезаписи карты,
    // когда воды нет (все клапаны закрыты, насос выключен).
    static unsigned long lastSavedPulses = 0;
    if (myConfig.pulses != lastSavedPulses) {
      lastSavedPulses = myConfig.pulses;
      data.update();
    }
}

void clearDataFlow()
{
  myConfig.flowSessionLiters = 0;
  myConfig.flowTotalLiters = 0;
  myConfig.pulses = 0;
  flowPulseCount = 0;
  data.updateNow();
}


// ============================================================
// ➕ Перенести расход текущей сессии в общий счётчик
// ============================================================
// Вызывается при закрытии всех клапанов — фиксируем расход сессии
void flowAddToTotal() {
  myConfig.flowSessionLiters = flowGetSessionLiters();  // 💧 Фиксируем литры за сессию
  myConfig.flowTotalLiters += myConfig.flowSessionLiters;        // ➕ Добавляем к общему расходу
  LOG_I("Расход за сессию: %.3f л; всего: %.3f л",
        myConfig.flowSessionLiters, myConfig.flowTotalLiters);
}

// ============================================================
// 🧽 Контроль засора фильтра по скорости потока
// ============================================================
// Модель: ожидаемый_поток = эталон_на_клапан × число_открытых + (дренаж ? эталон_дренажа : 0).
// Эталоны (cleanFlowPerValve — скорость одного клапана; cleanFlowDrain — добавка дренажа)
// набираются авто-максимумом на чистом фильтре и сбрасываются кнопкой «фильтр прочищен».
// Засор = измеренный поток < clogThresholdPercent% от ожидаемого.
static uint8_t       fmCount = 0;             // число клапанов в текущем окне (0 = не мониторим)
static bool          fmDraining = false;      // шёл ли пролив дренажа в текущем окне
static bool          fmBoostTainted = false;  // в окне работал пусковой насос → оценку фильтра пропускаем
static bool          fmMeasuring = false;     // фаза измерения (после стабилизации потока)
static unsigned long fmPhaseStartMs = 0;      // старт фазы выхода на режим (для потолка ожидания)
static unsigned long fmMeasureStartMs = 0;    // старт окна измерения
static unsigned long fmMeasureStartPulses = 0;
// ⏳ Адаптивное ожидание стабилизации потока (наполнение пустой трубы после
// открытия клапана даёт кратковременный завышенный поток, см. timing.h)
static unsigned long fmSubStartMs = 0;        // старт текущего подынтервала
static unsigned long fmSubStartPulses = 0;    // импульсы на старте подынтервала
static float         fmPrevSubRate = -1.0;    // скорость предыдущего подынтервала (-1 = ещё не было)
static uint8_t       fmStableCount = 0;       // подряд стабильных (не падающих) подынтервалов
static uint8_t       fmBadWindows = 0;        // подряд «плохих» окон (дебаунс)
static bool          fmAlertActive = false;   // активна ли тревога засора
static unsigned long fmLastAlertMs = 0;       // для rate-limit тревоги
static float         fmRate = 0.0;            // последняя измеренная скорость, л/мин
static bool          fmDirty = false;         // эталон изменился — нужно сохранить
// 📈 Разовый выброс скорости (пузырь воздуха, наводка на датчике и т.п.) не должен
// сразу становиться новым эталоном — кандидат подтверждается минимумом за
// FM_SPIKE_CONFIRM_MS, допуская откаты не больше FM_SPIKE_TOLERANCE_L (см. timing.h).
static float         fmCandidatePerValve = -1.0;  // минимум за время всплеска-кандидата (-1 = нет кандидата)
static unsigned long fmCandidateStartMs = 0;       // когда кандидат появился

float fmLastRate() { return fmRate; }
bool  fmIsClogged() { return fmAlertActive; }

// 📊 Ожидаемый «чистый» поток для openCount открытых клапанов (без дренажа), л/мин
float fmBaselineFor(uint8_t openCount) {
  return myConfig.cleanFlowPerValve * openCount;
}

bool flowMonitorNeedUpdate() {
  bool d = fmDirty;
  fmDirty = false;
  return d;
}

// 🧽 «Фильтр прочищен»: сбрасываем эталоны (пере-калибровка) и снимаем тревогу
void flowMonitorRecalibrate() {
  myConfig.cleanFlowPerValve = 0.0;
  myConfig.cleanFlowDrain = 0.0;
  fmAlertActive = false;
  fmBadWindows = 0;
  fmDirty = true;
  data.updateNow();
  LOG_I("Эталон фильтра сброшен — калибровка на чистом фильтре");
}

// 📊 Обработка измерения: rate — суммарная скорость (л/мин), cnt — открытых клапанов,
//    draining — шёл ли при этом пролив дренажа.
static void fmProcessSample(uint8_t cnt, bool draining, float rate) {
  fmRate = rate;
  float perValve = myConfig.cleanFlowPerValve;
  unsigned long now = millis();

  // 📈 Авто-обучение эталона «на клапан» (чистый фильтр = самый быстрый поток).
  // Разовый выброс (пузырь воздуха, наводка на датчике потока) не должен сразу
  // становиться новым эталоном: повышенное значение живёт как «кандидат»,
  // за время всплеска копится МИНИМУМ из всех окон, и только продержавшись
  // FM_SPIKE_CONFIRM_MS кандидат фиксируется эталоном. Колебания в пределах
  // FM_SPIKE_TOLERANCE_L от минимума отсчёт не сбрасывают; более резкий откат
  // перезапускает кандидата с текущего значения. Пример: эталон 2, поток
  // 5 → 6 → 4 — через 5 минут эталоном станет 4 (минимум), а не 6.
  // Возврат к старому эталону и ниже отменяет кандидата, поэтому кандидат
  // всегда ВЫШЕ старого эталона: авто-обучение может только поднимать эталон
  // (понизить — только вручную, кнопкой «фильтр прочищен»); иначе постепенное
  // снижение потока (настоящий засор) шаг за шагом утянуло бы эталон вниз
  // и замаскировало сам засор.
  if (!draining) {
    float measuredPerValve = rate / cnt;  // скорость на один открытый клапан
    if (measuredPerValve <= perValve) {
      fmCandidatePerValve = -1.0;  // поток не выше эталона — всплеск не подтвердился
      // и продолжаем к проверке засора ниже
    } else {
      bool rollback = (fmCandidatePerValve >= 0.0)
                        && (measuredPerValve < fmCandidatePerValve - FM_SPIKE_TOLERANCE_L);
      if (fmCandidatePerValve < 0.0 || rollback) {
        fmCandidatePerValve = measuredPerValve;  // старт (или перезапуск) кандидата
        fmCandidateStartMs = now;
        LOG_D("Кандидат на эталон: %.2f л/мин (подтверждение через %lu мс)",
              measuredPerValve, (unsigned long)FM_SPIKE_CONFIRM_MS);
      } else if (measuredPerValve < fmCandidatePerValve) {
        fmCandidatePerValve = measuredPerValve;  // 📉 копим минимум за время всплеска
      }
      if (now - fmCandidateStartMs >= FM_SPIKE_CONFIRM_MS) {
        myConfig.cleanFlowPerValve = fmCandidatePerValve;
        fmDirty = true;
        fmBadWindows = 0;
        fmCandidatePerValve = -1.0;
        LOG_D("Эталон на клапан подтверждён: %.2f л/мин", myConfig.cleanFlowPerValve);
      }
      return;  // поток выше эталона — засора точно нет, проверка не нужна
    }
  } else if (perValve > 0.0) {  // дренаж: учим «добавку» сверх потока клапанов
    float drainExtra = rate - perValve * cnt;
    if (drainExtra < 0.0) drainExtra = 0.0;
    if (drainExtra > myConfig.cleanFlowDrain) {
      myConfig.cleanFlowDrain = drainExtra;
      fmDirty = true;
      fmBadWindows = 0;
      LOG_D("Новый эталон дренажа: +%.2f л/мин", drainExtra);
      return;
    }
  }

  // 🚦 Проверка засора — только когда эталоны для текущей конфигурации известны
  if (perValve <= 0.0) return;                              // эталон на клапан ещё не набран
  if (draining && myConfig.cleanFlowDrain <= 0.0) return;   // эталон дренажа ещё не набран

  float expected = perValve * cnt + (draining ? myConfig.cleanFlowDrain : 0.0);
  if (expected <= 0.0) return;
  float ratioPct = rate / expected * 100.0;
  LOG_D("Поток: %.2f / норма %.2f л/мин (%d%%), кл.=%d дренаж=%d",
        rate, expected, (int)ratioPct, cnt, draining ? 1 : 0);

  if (ratioPct < myConfig.clogThresholdPercent) {
    if (fmBadWindows < 255) fmBadWindows++;
    LOG_W("Низкий поток: %.2f л/мин (%d%% от нормы), окно %d/%d",
          rate, (int)ratioPct, fmBadWindows, FM_BAD_WINDOWS);
    if (fmBadWindows >= FM_BAD_WINDOWS) {
      if (!fmAlertActive || (now - fmLastAlertMs > FM_ALERT_REPEAT_MS)) {
        fmAlertActive = true;
        fmLastAlertMs = now;
        LOG_W("ЗАСОР ФИЛЬТРА подтверждён: %.2f л/мин при норме %.2f", rate, expected);
        sendTelegramStatus("🧽 Похоже, засорился фильтр! Поток " + String(rate, 2)
          + " л/мин при норме " + String(expected, 2) + " л/мин ("
          + String((int)ratioPct) + "% от нормы). Прочистите фильтр.");
      }
    }
  } else {
    fmBadWindows = 0;
    if (fmAlertActive) {
      fmAlertActive = false;
      LOG_I("Поток фильтра восстановился: %.2f л/мин", rate);
      sendTelegramStatus("✅ Поток воды восстановился — фильтр в норме.");
    }
  }
}

// 📟 Скорость потока (л/мин) по числу импульсов за интервал времени
static float fmRateOf(unsigned long pulses, unsigned long ms) {
  if (ms == 0) return 0.0;
  return pulses / FLOW_PULSES_PER_LITER * 60000.0 / ms;
}

// 🚀 Старт окна замера с текущего момента
static void fmStartMeasureWindow(unsigned long now, bool boostActive) {
  fmMeasuring = true;
  fmMeasureStartMs = now;
  fmMeasureStartPulses = flowPulseCount;
  fmBoostTainted = boostActive;  // окно стартует «чисто», если буст уже выключен
}

// ⏱️ Тик контроля фильтра — вызывать в каждом loop()
void flowMonitorTick() {
  if (!myConfig.flowMonitorEnabled) { fmCount = 0; fmMeasuring = false; return; }

  uint8_t cnt = countValveOpen();
  bool draining = valveIsDraining();
  // 💪 Пусковой нагнетательный насос активен (держится PUMP_TIMEOUT после открытия
  //    клапана). Поток в это время искусственно завышен.
  bool boostActive = (pumpStart != 0);
  // ✅ Качественное окно для ЗАМЕРА (отображения текущего потока): открыт хотя бы
  //    один клапан и не идёт заливка бака. Пролив дренажа учитывается отдельным эталоном.
  bool qualified = (cnt > 0) && !fillActive;
  if (!qualified) { fmCount = 0; fmMeasuring = false; return; }

  unsigned long now = millis();

  // 🔄 Сменилась конфигурация (число клапанов или режим дренажа) — рестарт окна.
  //    Открытие/закрытие клапана заново наполняет трубу — сбрасываем и адаптивное
  //    ожидание стабилизации потока (см. ниже).
  if (cnt != fmCount || draining != fmDraining) {
    fmCount = cnt;
    fmDraining = draining;
    fmMeasuring = false;
    fmPhaseStartMs = now;
    fmBoostTainted = boostActive;
    fmSubStartMs = now;
    fmSubStartPulses = flowPulseCount;
    fmPrevSubRate = -1.0;
    fmStableCount = 0;
    fmCandidatePerValve = -1.0;  // 📈 смена конфигурации клапанов — кандидат эталона больше не актуален
    return;
  }

  // ⏳ Фаза выхода на режим: при открытии клапана пустая труба наполняется —
  // поток кратковременно завышен (гидроудар/заполнение воздуха водой), потом
  // падает до реального. Вместо фиксированной паузы ждём, пока скорость
  // перестанет заметно падать между соседними подынтервалами FM_SUBSAMPLE_MS
  // (FM_STABLE_SAMPLES_NEEDED раз подряд), и только тогда стартуем окно замера —
  // от «устаканившейся» точки, отбрасывая сам всплеск. FM_SETTLE_CAP_MS —
  // потолок ожидания, чтобы не зависнуть, если стабилизация не наступает.
  if (!fmMeasuring) {
    if (boostActive) fmBoostTainted = true;  // буст во время ожидания «пачкает» окно

    if (now - fmPhaseStartMs >= FM_SETTLE_CAP_MS) {
      LOG_W("Поток не стабилизировался за %lu мс — начинаем замер принудительно", (unsigned long)FM_SETTLE_CAP_MS);
      fmStartMeasureWindow(now, boostActive);
      return;
    }

    if (now - fmSubStartMs < FM_SUBSAMPLE_MS) return;  // подынтервал ещё не закончился

    float subRate = fmRateOf(flowPulseCount - fmSubStartPulses, now - fmSubStartMs);
    fmRate = subRate;  // 📟 /status показывает актуальную скорость даже во время ожидания стабилизации

    // 🛡️ На дальних ветках спад плавный (доли % за подынтервал) — детектор
    // «перестал падать» может ложно сработать, пока труба ещё наполняется.
    // Поэтому раньше FM_SETTLE_MIN_MS «стабильность» вообще не засчитываем —
    // только копим тренд (fmPrevSubRate), чтобы сразу после минимума уже было
    // с чем сравнить.
    bool pastMinimum = (now - fmPhaseStartMs) >= FM_SETTLE_MIN_MS;
    if (pastMinimum && fmPrevSubRate >= 0.0) {
      bool stillFalling = subRate < fmPrevSubRate * (1.0 - FM_SETTLE_STABLE_PCT / 100.0);
      fmStableCount = stillFalling ? 0 : (fmStableCount + 1);
      LOG_D("Стабилизация потока: %.2f -> %.2f л/мин, стабильно %d/%d",
            fmPrevSubRate, subRate, fmStableCount, FM_STABLE_SAMPLES_NEEDED);
    } else {
      fmStableCount = 0;  // до истечения минимума «стабильность» не копим
    }
    fmPrevSubRate = subRate;
    fmSubStartMs = now;
    fmSubStartPulses = flowPulseCount;

    if (pastMinimum && fmStableCount >= FM_STABLE_SAMPLES_NEEDED) {
      LOG_D("Поток стабилизировался (%.2f л/мин) — начинаем окно замера", subRate);
      fmStartMeasureWindow(now, boostActive);
    }
    return;
  }

  // 💪 Буст в любой момент окна замера → оценку фильтра по этому окну пропустим
  if (boostActive) fmBoostTainted = true;

  // 📟 Фаза измерения — ждём окно FM_WINDOW_MS
  if (now - fmMeasureStartMs < FM_WINDOW_MS) return;

  unsigned long dPulses = flowPulseCount - fmMeasureStartPulses;
  unsigned long dt = now - fmMeasureStartMs;
  // 🔁 Следующее окно стартует сразу — непрерывный мониторинг
  fmMeasureStartMs = now;
  fmMeasureStartPulses = flowPulseCount;
  bool tainted = fmBoostTainted;
  fmBoostTainted = boostActive;  // затравка для следующего окна

  // 📟 Текущая скорость потока — обновляем ВСЕГДА (для отображения в /status),
  //    даже если окно «загрязнено» бустом или потока почти нет.
  fmRate = fmRateOf(dPulses, dt);

  // 💧 Почти нет потока при открытых клапанах — возможно пустой бак/сухой ход,
  //    это не «засор» — оценку фильтра пропускаем (без ложной тревоги).
  if (dPulses < FM_MIN_PULSES) {
    LOG_W("Почти нет потока (%lu имп.) при %d открытых клапанах — пустой бак/сухой ход?",
          dPulses, cnt);
    return;
  }

  // 💪 Окно с работавшим пусковым насосом — поток завышен, для оценки фильтра не годится
  if (tainted) {
    LOG_D("Окно с пусковым насосом — поток %.2f л/мин показан, оценка фильтра пропущена", fmRate);
    return;
  }

  fmProcessSample(cnt, fmDraining, fmRate);
}

// 📟 Получить актуальную дату и время (синхронизация NTP + RTC)
// Время между синхронизациями ведётся локально (millis + RTC), поэтому
// принудительный сетевой запрос делаем не чаще раза в NTP_SYNC_INTERVAL,
// а не при каждом вызове (раньше дёргали сеть на каждом открытии клапана).
Datime getDateTime() {
  static uint32_t lastSync = 0;
  uint32_t nowMs = millis();
  if (lastSync == 0 || nowMs - lastSync >= NTP_SYNC_INTERVAL) {
    lastSync = nowMs;
    NTP.updateNow();      // 🔄 Принудительная синхронизация по NTP (раз в час)
  }
  Datime now = NTP;       // 📟 Локальное время (millis + RTC) — без сети
  return now;
}

// 📡 Подключить внешнюю функцию отправки статуса в Telegram
void attachSendFunction(void (*function)(String text)) {
  p_sendTelegramFunction = function;
}

// 📨 Отправить статусное сообщение через подключённую функцию
void sendTelegramStatus(String text) {
  (*p_sendTelegramFunction)(text);
}