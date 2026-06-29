// timing.h ⏱️ Интервалы, таймауты и параметры алгоритмов
#pragma once

#define CHECK_WIFI_INTERVAL_SMALL 300  // ⏱️ мс — малый интервал проверки
#define CHECK_WIFI_INTERVAL 30000      // ⏱️ мс — основной интервал проверки WiFi
#define CHECK_INTERVAL 10000           // ⏱️ мс — интервал цикла полива
#define CHECK_LIGHT true
#define CHECK_RAIN true
#define FILLING_WAIT 3000              // ⏱️ мс — время наполнения бака
#define TIMEOUT_WAIT 18000             // ⏱️ с — таймаут ожидания закрытия клапанов
#define DRAIN_TIMEOUT 12000            // ⏱️ мс — время пролива дренажа
#define PUMP_TIMEOUT 30                // ⏱️ с — максимальное время работы насоса
#define NTP_SYNC_INTERVAL 3600000UL    // ⏱️ мс — период принудительной синхронизации NTP (1 час)
#define TG_TIMEOUT_DEFAULT 150         // ⏱️ с — таймаут потери связи с Telegram по умолчанию (2.5× long-poll); хранится в Config

// 🧽 Контроль засора фильтра по скорости потока
#define FM_SETTLE_MS 5000              // ⏱️ выход на режим после смены конфигурации клапанов
#define FM_WINDOW_MS 30000             // ⏱️ длительность окна замера скорости потока
#define FM_MIN_PULSES 40               // 📟 минимум импульсов в окне для значимого замера
#define FM_BAD_WINDOWS 3               // 🔁 сколько «плохих» окон подряд подтверждают засор
#define FM_ALERT_REPEAT_MS 86400000UL  // ⏱️ повтор тревоги не чаще раза в сутки
