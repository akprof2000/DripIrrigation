// reports.cpp 📊 Отчёты/графики из CSV + удаление файлов
#include "reports.h"
#include "objects.h"        // bot, myConfig, getDateTime, NUM_CHANNELS
#include "botutil.h"        // getValue, IntWith2Zero
#include "bot_transport.h"  // sendReconnectMessage
#include "log.h"
#include <CharPlot.h>

// 🗑️ Рекурсивное удаление папки со всем содержимым на SD-карте
void rm(File dir, String tempPath) {
  while (true) {
    File entry = dir.openNextFile();
    String localPath;

    if (entry) {
      if (entry.isDirectory()) {
        localPath = tempPath + entry.name();
        rm(entry, localPath + "/");

        if (SD.rmdir(localPath)) {
          LOG_D("Удалена папка %s", localPath.c_str());
        } else {
          LOG_W("Не удалось удалить папку %s", localPath.c_str());
        }
      } else {
        localPath = tempPath + entry.name();

        if (SD.remove(localPath)) {
          LOG_D("Удалён файл %s", localPath.c_str());
        } else {
          LOG_W("Не удалось удалить файл %s", localPath.c_str());
        }
      }
    } else {
      break;  // 🔚 больше нет файлов
    }
  }
}

// 📊 Построение графика за указанный период дней (несколько файлов)
void fileToGrafPeriod(int period, String msgID) {
  sendReconnectMessage(F("📊 Началась генерация отчётов, ждите ..."), msgID);

  int del = 60 / period;
  uint8_t sz = period * del;

  float arr[NUM_CHANNELS][sz];
  for (int j = 0; j < NUM_CHANNELS; j++)
    for (int i = 0; i < sz; i++) {
      arr[j][i] = 0.0;
    }

  String buffer;
  int count[NUM_CHANNELS];
  int value[NUM_CHANNELS];

  for (int i = 0; i < NUM_CHANNELS; i++) {
    count[i] = 0;
    value[i] = 0;
  }

  int64_t ut = getDateTime().getUnix() - 60 * 60 * 24 * period;

  for (int p = 0; p < period; p++) {
    int64_t cut = ut + 60 * 60 * 24 * p;
    Datime t(cut);
    String fn = "/" + String(t.year) + "/" + IntWith2Zero(t.month) + "/" + IntWith2Zero(t.day) + ".csv";
    LOG_D("График: читаю файл %s", fn.c_str());
    if (SD.exists(fn)) {
      File printFile = SD.open(fn, FILE_READ);

      if (!printFile) {
        LOG_W("Не удалось открыть файл %s", fn.c_str());
        continue;
      }
      unsigned long part = printFile.size() / del;
      for (int d = 0; d < del; d++) {
        printFile.seek(part * d);
        printFile.readStringUntil('\n');
        int ind = 0;
        while (printFile.available()) {
          buffer = printFile.readStringUntil('\n');
          String data = getValue(buffer, ',', 0);
          String curr = getValue(buffer, ',', 2);
          int index = curr.toInt() - 1;
          curr = getValue(buffer, ',', 4);
          value[index] += curr.toInt();
          count[index]++;
          ind++;
          if (ind > 72) break;
        }
        for (int i = 0; i < NUM_CHANNELS; i++) {
          arr[i][p * del + d] = value[i] * 1.0 / count[i];
          value[i] = 0;
          count[i] = 0;
        }
      }
      printFile.close();
    }
  }

  // 📊 Отправляем графики в режиме MarkdownV2
  for (int i = 0; i < NUM_CHANNELS; i++) {
    fb::Message msg;
    msg.text = String("```\n🌱 Канал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X2>(arr[i], sz, 10) + String("\n```");
    msg.chatID = msgID;
    msg.setModeMD();
    bot.sendMessage(msg);
  }
}

// 📊 Построение графика за один день (один файл CSV)
void fileToGraf(String fn, String msgID) {
  sendReconnectMessage(F("📊 Началась генерация отчётов, ждите ..."), msgID);
  File printFile = SD.open(fn, FILE_READ);

  if (!printFile) {
    LOG_W("Не удалось открыть файл %s", fn.c_str());
    return;
  }
  uint8_t sz = 24;
  float arr[NUM_CHANNELS][sz];
  for (int j = 0; j < NUM_CHANNELS; j++)
    for (int i = 0; i < sz; i++) {
      arr[j][i] = 0.0;
    }

  String buffer;
  int ind[NUM_CHANNELS];
  int count[NUM_CHANNELS];
  int value[NUM_CHANNELS];

  for (int i = 0; i < NUM_CHANNELS; i++) {
    ind[i] = -1;
    count[i] = 0;
    value[i] = 0;
  }
  printFile.readStringUntil('\n');

  while (printFile.available()) {
    buffer = printFile.readStringUntil('\n');
    String data = getValue(buffer, ',', 0);
    String curr = getValue(buffer, ',', 2);
    int index = curr.toInt() - 1;
    curr = getValue(buffer, ',', 4);
    int64_t ut = data.toInt();
    int hour = (ut % 86400) / 3600;
    if (hour > ind[index]) {
      if (ind[index] >= 0) {
        arr[index][ind[index]] = value[index] * 1.0 / count[index];
        value[index] = 0;
        count[index] = 0;
      }
      ind[index] = hour;
    }

    value[index] += curr.toInt();
    count[index]++;
  }
  for (int i = 0; i < NUM_CHANNELS; i++) {
    if (ind[i] >= 0) {
      arr[i][ind[i]] = value[i] * 1.0 / count[i];
    }
  }
  printFile.close();

  // 📊 Отправляем графики в режиме MarkdownV2
  for (int i = 0; i < NUM_CHANNELS; i++) {
    fb::Message msg;
    msg.text = String("```\n🌱 Канал №") + String(i + 1) + String(" (") + String(myConfig.chanel[i].title) + String(")\n") + CharPlot<LINE_X1>(arr[i], sz, 10) + String("\n```");
    msg.chatID = msgID;
    msg.setModeMD();
    bot.sendMessage(msg);
  }
}
