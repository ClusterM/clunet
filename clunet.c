/* Name: clunet.c
 * Project: CLUNET network driver
 * Author: Alexey Avdyukhin
 * Creation Date: 2013-09-09
 * License: DO WHAT THE FUCK YOU WANT TO PUBLIC LICENSE
 */

#include "defines.h"
#include "clunet_config.h"
#include "bits.h"
#include "clunet.h"

#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>

void (*on_data_received)(uint8_t src_address, uint8_t dst_address, uint8_t command, char* data, uint8_t size) = 0;
void (*on_data_received_sniff)(uint8_t src_address, uint8_t dst_address, uint8_t command, char* data, uint8_t size) = 0;

volatile uint8_t clunetSendingState = CLUNET_SENDING_STATE_IDLE;
volatile uint8_t clunetSendingDataLength;
volatile uint8_t clunetSendingCurrentByte;
volatile uint8_t clunetSendingCurrentBit;
volatile uint8_t clunetReadingState = CLUNET_READING_STATE_IDLE;
volatile uint8_t clunetReadingCurrentByte;
volatile uint8_t clunetReadingCurrentBit;
volatile uint8_t clunetCurrentPrio;

volatile uint8_t clunetTimerStart = 0;

volatile char dataToSend[CLUNET_SEND_BUFFER_SIZE];
volatile char dataToRead[CLUNET_READ_BUFFER_SIZE];

static inline void
clunet_start_send()
{
	clunetSendingState = CLUNET_SENDING_STATE_INIT;
	// подождем 1.5Т, чтобы нас гарантированно могли остановить при передаче на линии со стороны другого устройства в процедуре внешнего прерывания
	CLUNET_TIMER_REG_OCR = CLUNET_TIMER_REG + (CLUNET_T + CLUNET_T / 2);
	CLUNET_ENABLE_TIMER_COMP;	// Включаем прерывание сравнения таймера (передачу)
}

static inline char
check_crc(const char* data, const uint8_t size)
{
      uint8_t crc = 0;
      uint8_t i, j;
      for (i = 0; i < size; i++)
      {
            uint8_t inbyte = data[i];
            for (j = 0 ; j < 8 ; j++)
            {
                  uint8_t mix = (crc ^ inbyte) & 1;
                  crc >>= 1;
                  if (mix) crc ^= 0x8C;
                  inbyte >>= 1;
            }
      }
      return crc;
}

static inline void
clunet_data_received(const uint8_t src_address, const uint8_t dst_address, const uint8_t command, char* data, const uint8_t size)
{
	if (on_data_received_sniff)
		(*on_data_received_sniff)(src_address, dst_address, command, data, size);

	if (src_address == CLUNET_DEVICE_ID) return;	// Игнорируем сообщения от самого себя!

	if ((dst_address != CLUNET_DEVICE_ID) &&
		(dst_address != CLUNET_BROADCAST_ADDRESS)) return;	// Игнорируем сообщения не для нас

	// Команда перезагрузки. Перезагрузим по сторожевому таймеру
	if (command == CLUNET_COMMAND_REBOOT)
	{
		cli();
		set_bit(WDTCR, WDE);
		while(1);
	}

	if ((clunetSendingState == CLUNET_SENDING_STATE_IDLE) || (clunetCurrentPrio <= CLUNET_PRIORITY_MESSAGE))
	{
		/* Ответ на поиск устройств */
		if (command == CLUNET_COMMAND_DISCOVERY)
		{
#ifdef CLUNET_DEVICE_NAME
			char buf[] = CLUNET_DEVICE_NAME;
			uint8_t len = 0; while(buf[len]) len++;
			clunet_send(src_address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_DISCOVERY_RESPONSE, buf, len);
#else
			clunet_send(src_address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_DISCOVERY_RESPONSE, 0, 0);
#endif
		}
		/* Ответ на пинг */
		else if (command == CLUNET_COMMAND_PING)
			clunet_send(src_address, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_PING_REPLY, data, size);
	}

	if (on_data_received)
		(*on_data_received)(src_address, dst_address, command, data, size);
	
}

/* Процедура прерывания сравнения таймера */
ISR(CLUNET_TIMER_COMP_VECTOR)
{
	uint8_t now = CLUNET_TIMER_REG;   				// Запоминаем текущее значение таймера
	
	/* Если достигли фазы завершения передачи, то завершим ее и освободим передатчик */
	if (clunetSendingState == CLUNET_SENDING_STATE_DONE)
	{
		CLUNET_DISABLE_TIMER_COMP;				// Выключаем прерывание сравнения таймера
		clunetSendingState = CLUNET_SENDING_STATE_IDLE;		// Указываем, что передатчик свободен
		CLUNET_SEND_0;						// Отпускаем линию
	}
	/* Иначе если передачу необходимо продолжить, то сначала проверим на конфликт */
	else if (!CLUNET_SENDING && CLUNET_READING)
	{
		CLUNET_DISABLE_TIMER_COMP;					// Выключаем прерывание сравнения таймера (передачу)
		clunetSendingState = CLUNET_SENDING_STATE_WAITING_LINE;		// Переходим в режим ожидания линии
	}
	/* Все в порядке, можем продолжать */
	else
	{
		CLUNET_SEND_INVERT;	// Инвертируем значение сигнала
		
		/* Если отпустили линию */
		if (!CLUNET_SENDING)
			CLUNET_TIMER_REG_OCR = now + CLUNET_T;	// то запланируем время паузы перед следующей передачей длительностью 1Т
	
		/* Если прижали линию к земле, то запланируем время передачи сигнала в зависимости от текущей фазы передачи */
		else
			switch (clunetSendingState)
			{
			/* Фаза передачи данных */
			case CLUNET_SENDING_STATE_DATA:
	
				// Планируем следующее прерывание чтобы отпустить линию в зависимости от значения бита
				CLUNET_TIMER_REG_OCR = now + ((dataToSend[clunetSendingCurrentByte] & (1 << clunetSendingCurrentBit)) ? CLUNET_1_T : CLUNET_0_T);
	
				/* Если передан байт данных */
				if (++clunetSendingCurrentBit & 8)
				{
					/* и не все данные отосланы */
					if (++clunetSendingCurrentByte < clunetSendingDataLength)
						clunetSendingCurrentBit = 0;	// то начинаем передачу следующего байта с бита 0
					/* и передача всех данных закончена */
					else
						clunetSendingState++;		// то переходим к следующей фазе завершения передачи пакета
				}
				break;

			/* Фаза инициализации передачи пакета (время 10Т) */
			case CLUNET_SENDING_STATE_INIT:

				CLUNET_TIMER_REG_OCR = now + CLUNET_INIT_T;	// Планируем следующее прерывание
				clunetSendingState++;				// К следующей фазе передачи старшего бита приоритета
				break;
	
			/* Фаза передачи приоритета (старший бит) */
			case CLUNET_SENDING_STATE_PRIO1:
	
				CLUNET_TIMER_REG_OCR = now + ((clunetCurrentPrio > 2) ? CLUNET_1_T : CLUNET_0_T);
				clunetSendingState++;	// К следующей фазе передачи младшего бита приоритета
				break;
	
			/* Фаза передачи приоритета (младший бит) */
			case CLUNET_SENDING_STATE_PRIO2:
	
				CLUNET_TIMER_REG_OCR = now + ((clunetCurrentPrio & 1) ? CLUNET_0_T : CLUNET_1_T);
				clunetSendingCurrentByte = clunetSendingCurrentBit = 0;	// Обнуляем счётчик
				clunetSendingState++;	// К следующей фазе передачи данных
			}
	}
}

void
clunet_send(const uint8_t address, const uint8_t prio, const uint8_t command, const char* data, const uint8_t size)
{
	/* Если размер данных в пределах буфера передачи (максимально для протокола 250 байт) */
	if (size < (CLUNET_SEND_BUFFER_SIZE - CLUNET_OFFSET_DATA))
	{
		/* Прерываем текущую передачу, если есть такая */
		if (clunetSendingState)
		{
			CLUNET_DISABLE_TIMER_COMP;
			CLUNET_SEND_0;
		}

		/* Заполняем переменные */
		if (prio)
		clunetCurrentPrio = (prio > 4) ? 4 : prio ? : 1;	// Ограничим приоритет диапазоном (1 ; 4)
		dataToSend[CLUNET_OFFSET_SRC_ADDRESS] = CLUNET_DEVICE_ID;
		dataToSend[CLUNET_OFFSET_DST_ADDRESS] = address;
		dataToSend[CLUNET_OFFSET_COMMAND] = command;
		dataToSend[CLUNET_OFFSET_SIZE] = size;
		
		/* Копируем данные в буфер */
		uint8_t i;
		for (i = 0; i < size; i++)
			dataToSend[CLUNET_OFFSET_DATA + i] = data[i];

		/* Добавляем контрольную сумму */
		dataToSend[CLUNET_OFFSET_DATA + size] = check_crc((char*)dataToSend, CLUNET_OFFSET_DATA + size);
		
		clunetSendingDataLength = size + (CLUNET_OFFSET_DATA + 1);

		clunet_start_send();	// Запускаем отправку данных и полагаемся на нашу отработку конфликтов при чтении и отправки

	}
}

/* Процедура внешнего прерывания по фронту и спаду сигнала */
ISR(CLUNET_INT_VECTOR)
{

	uint8_t now = CLUNET_TIMER_REG;		// Текущие значение таймера

	/* Если линию прижало к нулю */
	if (CLUNET_READING)
	{
		/* Если мы в режиме передачи и прижали не мы, то замолкаем и ожидаем, тем более, что наши передаваемые данные уже битые */
		/* Обеспечивается быстрая отработка ошибки на линии во избежание конфликтов */
		if (clunetSendingState && !CLUNET_SENDING)
		{
			CLUNET_DISABLE_TIMER_COMP;
			clunetSendingState = CLUNET_SENDING_STATE_WAITING_LINE;
		}
		clunetTimerStart = now;		// Запомним время начала сигнала
	}

	/* Иначе если линию отпустило */
	else
	{
		/* Надеемся на то, что линию освободило совсем и пробуем запланировать отправку, если нет, то конфликт будет 100% отработан */
		if (clunetSendingState == CLUNET_SENDING_STATE_WAITING_LINE)
			clunet_start_send();

		uint8_t ticks = now - clunetTimerStart;	// Вычислим время сигнала в тиках таймера

		/* Если кто-то долго жмёт линию (время >= 6.5Т) - это инициализация */
		if (ticks >= (CLUNET_INIT_T + CLUNET_1_T) / 2)
			clunetReadingState = CLUNET_READING_STATE_PRIO1;

		/* Иначе если недолго, то смотрим на этап */
		else
			switch (clunetReadingState)
			{
			
			/* Получение приоритета (младший бит), клиенту он не нужен */
			case CLUNET_READING_STATE_PRIO2:
				clunetReadingCurrentByte = clunetReadingCurrentBit = 0;
				dataToRead[0] = 0;

			/* Получение приоритета (старший бит), клиенту он не нужен */
			case CLUNET_READING_STATE_PRIO1:
				clunetReadingState++;
				break;
			
			/* Чтение данных */
			case CLUNET_READING_STATE_DATA:

				/* Если бит значащий (время > 2Т), то установим его в приемном буфере */
				if (ticks > (CLUNET_0_T + CLUNET_1_T) / 2)
					dataToRead[clunetReadingCurrentByte] |= (1 << clunetReadingCurrentBit);

				/* Инкрементируем указатель бита, и при полной обработке всех 8 бит в байте выполним: */
				if (++clunetReadingCurrentBit & 8)
				{

					/* Проверка на окончание чтения пакета */
					if ((++clunetReadingCurrentByte > CLUNET_OFFSET_SIZE) && (clunetReadingCurrentByte > dataToRead[CLUNET_OFFSET_SIZE] + CLUNET_OFFSET_DATA))
					{

						clunetReadingState = CLUNET_READING_STATE_IDLE;

						/* Проверяем CRC, при успехе начнем обработку принятого пакета */
						if (!check_crc((char*)dataToRead, clunetReadingCurrentByte))
							clunet_data_received (
								dataToRead[CLUNET_OFFSET_SRC_ADDRESS],
								dataToRead[CLUNET_OFFSET_DST_ADDRESS],
								dataToRead[CLUNET_OFFSET_COMMAND],
								(char*)(dataToRead + CLUNET_OFFSET_DATA),
								dataToRead[CLUNET_OFFSET_SIZE]
							);
					
					}
					
					/* Иначе если пакет не прочитан и буфер не закончился - подготовимся к чтению следующего байта */
					else if (clunetReadingCurrentByte < CLUNET_READ_BUFFER_SIZE)
					{
						clunetReadingCurrentBit = 0;
						dataToRead[clunetReadingCurrentByte] = 0;
					}
					
					/* Иначе - нехватка приемного буфера -> игнорируем пакет */
					else
						clunetReadingState = CLUNET_READING_STATE_IDLE;

				}

			}
	}
}

void
clunet_init()
{
	sei();
	CLUNET_SEND_INIT;
	CLUNET_READ_INIT;
	CLUNET_TIMER_INIT;
	CLUNET_INIT_INT;
	char reset_source = MCUCSR;
	clunet_send (
		CLUNET_BROADCAST_ADDRESS,
		CLUNET_PRIORITY_MESSAGE,
		CLUNET_COMMAND_BOOT_COMPLETED,
		&reset_source,
		sizeof(reset_source)
	);
	MCUCSR = 0;
}

/*
	Возвращает 0, если готов к передаче, иначе приоритет текущей задачи
*/
uint8_t
clunet_ready_to_send()
{
	return clunetSendingState ? clunetCurrentPrio : 0;
}

void
clunet_set_on_data_received(void (*f)(uint8_t src_address, uint8_t dst_address, uint8_t command, char* data, uint8_t size))
{
	on_data_received = f;
}

void
clunet_set_on_data_received_sniff(void (*f)(uint8_t src_address, uint8_t dst_address, uint8_t command, char* data, uint8_t size))
{
	on_data_received_sniff = f;
}
