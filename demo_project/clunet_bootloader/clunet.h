/* Name: clunet.h
 * Project: CLUNET network driver
 * Author: Alexey Avdyukhin
 * Creation Date: 2013-09-09
 * License: DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 */
 
#ifndef __clunet_h_included__
#define __clunet_h_included__

#include "bits.h"
#include "clunet_config.h"

#define CLUNET_SENDING_STATE_IDLE 0
#define CLUNET_SENDING_STATE_INIT 1
#define CLUNET_SENDING_STATE_PRIO1 2
#define CLUNET_SENDING_STATE_PRIO2 3
#define CLUNET_SENDING_STATE_DATA 4
#define CLUNET_SENDING_STATE_WAITING_LINE 6
#define CLUNET_SENDING_STATE_PREINIT 7
#define CLUNET_SENDING_STATE_STOP 8
#define CLUNET_SENDING_STATE_DONE 9

#define CLUNET_READING_STATE_IDLE 0
#define CLUNET_READING_STATE_INIT 1
#define CLUNET_READING_STATE_PRIO1 2
#define CLUNET_READING_STATE_PRIO2 3
#define CLUNET_READING_STATE_HEADER 4
#define CLUNET_READING_STATE_DATA 5

#define CLUNET_OFFSET_SRC_ADDRESS 0
#define CLUNET_OFFSET_DST_ADDRESS 1
#define CLUNET_OFFSET_COMMAND 2
#define CLUNET_OFFSET_SIZE 3
#define CLUNET_OFFSET_DATA 4
#define CLUNET_BROADCAST_ADDRESS 0xFF

#define CLUNET_COMMAND_DISCOVERY 0x00
/* Поиск других устройств, параметров нет */

#define CLUNET_COMMAND_DISCOVERY_RESPONSE 0x01
/* Ответ устройств на поиск, в качестве параметра - название устройства (текст) */

#define CLUNET_COMMAND_BOOT_CONTROL 0x02
/* Работа с загрузчиком. Данные - субкоманда.
<-0 - загрузчик запущен
->1 - перейти в режим обновления прошивки
<-2 - подтверждение перехода, плюс два байта - размер страницы
->3 запись прошивки, 4 байта - адрес, всё остальное - данные (равные размеру страницы)
<-4 блок прошивки записан
->5 выход из режима прошивки */

#define CLUNET_COMMAND_REBOOT 0x03
/* Перезагружает устройство в загрузчик. */

#define CLUNET_COMMAND_BOOT_COMPLETED 0x04
/* Посылается устройством после инициализации библиотеки, сообщает об успешной загрузке устройства. Параметр - содержимое MCU регистра, говорящее о причине перезагрузки. */

#define CLUNET_COMMAND_DOOR_INFO 0x05
/*Информация об открытии двери.
0 - закрыта
1 - открыта
2 - тревога */

#define CLUNET_COMMAND_DEVICE_POWER_INFO_REQUEST 0x06
/* Запрашивает информацию о состоянии выключателей */

#define CLUNET_COMMAND_DEVICE_POWER_INFO 0x07
/* Состояние выключателей. Параметр - битовая маска состояния выключателей. */

#define CLUNET_COMMAND_DEVICE_POWER_COMMAND 0x08
/* Включает/выключает выключатели. Параметр - битовая маска. Для света 0xFE - убавить свет, а 0xFF - прибавить. */

#define CLUNET_COMMAND_SET_TIMER 0x09
/* Установка таймера. Параметр - кол-во секунд (два байта) */

#define CLUNET_COMMAND_RC_BUTTON_PRESSED 0x0A
/* Нажата кнопка на пульте. Первый байт - тип кода, далее - номер кнопки.
На данный момент 01 - самый популярный стандарт (Sony вроде?) длина кода кнопки - 4 байта.
02 - удержание кнопки его же. */

#define CLUNET_COMMAND_RC_BUTTON_PRESSED_RAW 0x0B
/* Недекодированные данные о нажатой кнопке на пульте для нестандартных пультов. Идут пачками по 4 байта. 2 из которых - длительность сигнала и 2 - длительность его отсутствия в 0.032 долях миллисекунды. (1/8000000*256 сек) */

#define CLUNET_COMMAND_RC_BUTTON_SEND 0x0C
/* Заэмулировать кнопку пульта. Формат данных аналогичен CLUNET_COMMAND_RC_BUTTON_PRESSED, плюс в конце опциональный байт длительности удержания кнопки (кол-во дополнительных сигналов), для Sony это ~30мс */

#define CLUNET_COMMAND_RC_BUTTON_SEND_RAW 0x0D
/* Заэмулировать кнопку пульта на основе сырых данных. Формат данных аналогичен CLUNET_COMMAND_RC_BUTTON_PRESSED_RAW. */

#define CLUNET_COMMAND_LIGHT_LEVEL 0x0E
/* Сообщает об уровне освещения. Параметр - 2 байта (0x0000 - 0x1FFF, где 0x1FFF - 0 вольт между фотодиодом и землёй, а 0x0000 - 5 вольт). */

#define CLUNET_COMMAND_ONEWIRE_START_SEARCH 0x0F
/* Запуск поиска 1-wire устройств, данные пустые. */

#define CLUNET_COMMAND_ONEWIRE_DEVICE_FOUND 0x10
/* Сообщает о найденном 1-wire устройсте. Данные - 8 байт, включающие тип устройства, серийный номер и CRC. */

#define CLUNET_COMMAND_TEMPERATURE 0x11
/* Сообщает о температуре. 1 байт - тип устройства, 6 - серийник, 2 - температура в формате устройства (смотреть на тип, чтобы декодировать!) */

#define CLUNET_COMMAND_TIME 0x12
/* Сообщает время. Шесть байт - часы, минуты, секунды, год (от 1900), месяц (от 0), день (от 1) */

#define CLUNET_COMMAND_WHEEL 0x13
/* Сообщает обороты колеса мыши, первый байт - ID колеса, далее два байта - обороты */

#define CLUNET_COMMAND_VOLTAGE 0x14
/* Сообщает напряжение на батарейке, первый байт - ID устройства, далее два байта - вольтаж, где 0x3FF = 5.12 */

#define CLUNET_COMMAND_MOTION 0x15
/* Сообщает, что в помещении есть движение, первый байт - ID датчика/камеры */

#define CLUNET_COMMAND_INTERCOM_RING 0x16
/* Звонок в домофон */

#define CLUNET_COMMAND_INTERCOM_MESSAGE 0x17
/* Новое сообщение на автоответчике домофона, в данных 4 байта - номер сообщения */

#define CLUNET_COMMAND_INTERCOM_MODE_REQUEST 0x18
/* Запрашивает режим работы домофона */

#define CLUNET_COMMAND_INTERCOM_MODE_INFO 0x19
/* Сообщает режим работы домофона, первый байт - постоянный режим, второй - временный */

#define CLUNET_COMMAND_INTERCOM_MODE_SET 0x1A
/* Задаёт режим работы домофона, первый байт - постоянный режим (или 0xFF, чтобы не трогать), второй - временный (опционально) */

#define CLUNET_COMMAND_INTERCOM_RECORD_REQUEST 0x1B
/* Запрашивает запись у домофона, подтверждает доставку или завершает передачу
 Если 4 байта, то это номер запрашиваемой записи
 Если 1 байт, то 1 в случае подтверждения получения пакета, 0 - завершение передачи */
 
#define CLUNET_COMMAND_INTERCOM_RECORD_DATA 0x1C
/* Передаёт кусок записи с автоответчика. Первые 4 байта - номер записи, далее 4 байта - смещение от начала файла, всё далее - данные из файла */

#define CLUNET_COMMAND_DOOR_LOCK_COMMAND 0x1D
/* Открывает или закрывает дверь. Один байт данных. 1 - открыть, 2 - закрыть */

#define CLUNET_COMMAND_DOOR_LOCK_INFO 0x1E
/* Сообщает об открытии или закрытии двери. 1 - открыта, 2 - закрыта */

#define CLUNET_COMMAND_DOOR_LOCK_INFO_REQUEST 0x1F
/* Запрашивает текущее состояние дверного замка */

#define CLUNET_COMMAND_BUTTON 0x20
/* Нажата кнопка, в данных номер кнопки */

#define CLUNET_COMMAND_WINDOW_COMMAND 0x21
/* Открывает или закрывает окно. Один байт данных. 1 - открыть, 2 - закрыть */

#define CLUNET_COMMAND_WINDOW_INFO 0x22
/* Сообщает об открытии или закрытии окна. 1 - открыто, 2 - закрыто */

#define CLUNET_COMMAND_WINDOW_INFO_REQUEST 0x23
/* Запрашивает текущее состояние окна */

#define CLUNET_COMMAND_PING 0xFE
/* Пинг, на эту команду устройство должно ответить следующей командой, возвратив весь буфер */

#define CLUNET_COMMAND_PING_REPLY 0xFF
/* Ответ на пинг, в данных то, что было прислано в предыдущей команде */

#define CLUNET_PRIORITY_NOTICE 1
/* Приоритет пакета 1 - неважное уведомление, которое вообще может быть потеряно без последствий */

#define CLUNET_PRIORITY_INFO 2
/* Приоритет пакета 2 - какая-то информация, не очень важная */

#define CLUNET_PRIORITY_MESSAGE 3
/* Приоритет пакета 3 - сообщение с какой-то важной информацией */

#define CLUNET_PRIORITY_COMMAND 4
/* Приоритет пакета 4 - команда, на которую нужно сразу отреагировать */

#ifndef CLUNET_T
#define CLUNET_T ((F_CPU / CLUNET_TIMER_PRESCALER) / 15625)
#endif
#if CLUNET_T < 8
#  error Timer frequency is too small, increase CPU frequency or decrease timer prescaler
#endif
#if CLUNET_T > 24
#  error Timer frequency is too big, decrease CPU frequency or increase timer prescaler
#endif

#define CLUNET_0_T (CLUNET_T)
#define CLUNET_1_T (3*CLUNET_T)
#define CLUNET_INIT_T (10*CLUNET_T)

#define CLUNET_CONCAT(a, b)            a ## b
#define CLUNET_OUTPORT(name)           CLUNET_CONCAT(PORT, name)
#define CLUNET_INPORT(name)            CLUNET_CONCAT(PIN, name)
#define CLUNET_DDRPORT(name)           CLUNET_CONCAT(DDR, name)

#ifndef CLUNET_WRITE_TRANSISTOR
#  define CLUNET_SEND_1 CLUNET_DDRPORT(CLUNET_WRITE_PORT) |= (1 << CLUNET_WRITE_PIN)
#  define CLUNET_SEND_0 CLUNET_DDRPORT(CLUNET_WRITE_PORT) &= ~(1 << CLUNET_WRITE_PIN)
#  define CLUNET_SENDING (CLUNET_DDRPORT(CLUNET_WRITE_PORT) & (1 << CLUNET_WRITE_PIN))
#  define CLUNET_SEND_INVERT CLUNET_DDRPORT(CLUNET_WRITE_PORT) ^= (1 << CLUNET_WRITE_PIN)
#  define CLUNET_SEND_INIT { CLUNET_OUTPORT(CLUNET_WRITE_PORT) &= ~(1 << CLUNET_WRITE_PIN); CLUNET_SEND_0; }
#else
#  define CLUNET_SEND_1 CLUNET_OUTPORT(CLUNET_WRITE_PORT) |= (1 << CLUNET_WRITE_PIN)
#  define CLUNET_SEND_0 CLUNET_OUTPORT(CLUNET_WRITE_PORT) &= ~(1 << CLUNET_WRITE_PIN)
#  define CLUNET_SENDING (CLUNET_OUTPORT(CLUNET_WRITE_PORT) & (1 << CLUNET_WRITE_PIN))
#  define CLUNET_SEND_INVERT CLUNET_OUTPORT(CLUNET_WRITE_PORT) ^= (1 << CLUNET_WRITE_PIN)
#  define CLUNET_SEND_INIT { CLUNET_DDRPORT(CLUNET_WRITE_PORT) |= (1 << CLUNET_WRITE_PIN); CLUNET_SEND_0; }
#endif

#define CLUNET_READ_INIT { CLUNET_DDRPORT(CLUNET_READ_PORT) &= ~(1 << CLUNET_READ_PIN); CLUNET_OUTPORT(CLUNET_READ_PORT) |= (1 << CLUNET_READ_PIN); }
#define CLUNET_READING (!(CLUNET_INPORT(CLUNET_READ_PORT) & (1 << CLUNET_READ_PIN)))

#ifndef CLUNET_SEND_BUFFER_SIZE
#  error CLUNET_SEND_BUFFER_SIZE is not defined
#endif
#ifndef CLUNET_READ_BUFFER_SIZE
#  error CLUNET_READ_BUFFER_SIZE is not defined
#endif
#if CLUNET_SEND_BUFFER_SIZE > 255
#  error CLUNET_SEND_BUFFER_SIZE must be <= 255
#endif
#if CLUNET_READ_BUFFER_SIZE > 255
#  error CLUNET_READ_BUFFER_SIZE must be <= 255
#endif

// Инициализация
void clunet_init();

// Отправка пакета
void clunet_send(unsigned char address, unsigned char prio, unsigned char command, char* data, unsigned char size);

// Возвращает 0, если готов к передаче, иначе приоритет текущей задачи
int clunet_ready_to_send();

// Установка функций, которые вызываются при получении пакетов
// Эта - получает пакеты, которые адресованы нам
void clunet_set_on_data_received(void (*f)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size));

// А эта - абсолютно все, которые ходят по сети, включая наши
void clunet_set_on_data_received_sniff(void (*f)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size));

char check_crc(char* data, unsigned char size);

#endif
