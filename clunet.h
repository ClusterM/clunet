/* Name: clunet.h
 * Project: CLUNET network driver
 * Author: Alexey Avdyukhin
 * Creation Date: 2013-09-09
 * License: DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 */
 
#ifndef __clunet_h_included__
#define __clunet_h_included__

#include <stdint.h>
#include "bits.h"
#include "clunet_config.h"

#define CLUNET_BROADCAST_ADDRESS 255

#define CLUNET_PRIORITY_NOTICE 1
/* Приоритет пакета 1 - неважное уведомление, которое вообще может быть потеряно без последствий */

#define CLUNET_PRIORITY_INFO 2
/* Приоритет пакета 2 - какая-то информация, не очень важная */

#define CLUNET_PRIORITY_MESSAGE 3
/* Приоритет пакета 3 - сообщение с какой-то важной информацией */

#define CLUNET_PRIORITY_COMMAND 4
/* Приоритет пакета 4 - команда, на которую нужно сразу отреагировать */


// Инициализация
void clunet_init();

// Возвращает 0, если готов к передаче, иначе приоритет текущей задачи
uint8_t clunet_ready_to_send();

// Отправка пакета
void clunet_send(const uint8_t address, const uint8_t prio, const uint8_t command, const char* data, const uint8_t size);

// Установка функций, которые вызываются при получении пакетов
// Эта - получает пакеты, которые адресованы нам
void clunet_set_on_data_received(void (*f)(uint8_t src_address, uint8_t dst_address, uint8_t command, char* data, uint8_t size));


#ifdef SNIFFER_ENABLE
// А эта - абсолютно все, которые ходят по сети, включая наши
void clunet_set_on_data_received_sniff(void (*f)(uint8_t src_address, uint8_t dst_address, uint8_t command, char* data, uint8_t size));
#endif

#endif
