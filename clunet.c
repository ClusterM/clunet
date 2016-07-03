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
#include <avr/io.h>
#include <avr/interrupt.h>

void (*on_data_received)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size) = 0;
void (*on_data_received_sniff)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size) = 0;

volatile unsigned char clunetSendingState = CLUNET_SENDING_STATE_IDLE;
volatile unsigned short int clunetSendingDataLength;
volatile unsigned char clunetSendingCurrentByte;
volatile unsigned char clunetSendingCurrentBit;
volatile unsigned char clunetReadingState = CLUNET_READING_STATE_IDLE;
volatile unsigned char clunetReadingCurrentByte;
volatile unsigned char clunetReadingCurrentBit;
volatile unsigned char clunetCurrentPrio;

volatile unsigned char clunetReceivingState = 0;
volatile unsigned char clunetTimerStart = 0;
volatile unsigned char clunetTimerPeriods = 0;

volatile char dataToSend[CLUNET_SEND_BUFFER_SIZE];
volatile char dataToRead[CLUNET_READ_BUFFER_SIZE];

char
check_crc(char* data, unsigned char size)
{
      uint8_t crc = 0;
      for (uint8_t i = 0; i < size; i++)
      {
            uint8_t inbyte = data[i];
            for (uint8_t j = 0 ; j < 8 ; j++)
            {
                  uint8_t mix = (crc ^ inbyte) & 1;
                  crc >>= 1;
                  if (mix) crc ^= 0x8C;
                  inbyte >>= 1;
            }
      }
      return crc;
}

/* Процедура прерывания сравнения таймера */
ISR(CLUNET_TIMER_COMP_VECTOR)
{
	uint8_t now = CLUNET_TIMER_REG;     // Запоминаем текущее время

	switch (clunetSendingState)
	{
	case CLUNET_SENDING_STATE_PREINIT:			// Нужно подождать перед отправкой
		CLUNET_TIMER_REG_OCR = now + CLUNET_T;
		clunetSendingState = CLUNET_SENDING_STATE_INIT;	// Начинаем следующую фазу
		return;
	case CLUNET_SENDING_STATE_STOP:				// Завершение передачи, но надо ещё подождать
		CLUNET_SEND_0;					// Отпускаем линию
		CLUNET_TIMER_REG_OCR = now + CLUNET_T;
		clunetSendingState = CLUNET_SENDING_STATE_DONE;
		return;
	case CLUNET_SENDING_STATE_DONE:				// Завершение передачи
		CLUNET_DISABLE_TIMER_COMP;			// Выключаем таймер-сравнение
		clunetSendingState = CLUNET_SENDING_STATE_IDLE;	// Ставим флаг, что передатчик свободен
		return;
	}

	if (	/*!((clunetReadingState == CLUNET_READING_STATE_DATA) && // Если мы сейчас не получаем данные
		(clunetCurrentPrio > clunetReceivingPrio) 				// И приоритет получаемых данных не ниже
		&& (clunetSendingState == CLUNET_SENDING_STATE_INIT))  // И мы не пытаемся начать инициализацию]		
		&&*/
		(!CLUNET_SENDING && CLUNET_READING)) // То идёт проверка на конфликт. Если мы линию не держим, но она уже занята
	{
		CLUNET_DISABLE_TIMER_COMP;				// Выключаем таймер-сравнение
		clunetSendingState = CLUNET_SENDING_STATE_WAITING_LINE;	// Переходим в режим ожидания линии
		return;							// И умолкаем
	}

	CLUNET_SEND_INVERT;	 // Сразу инвртируем значение сигнала, у нас это запланировано
	
	if (!CLUNET_SENDING)				// Если мы отпустили линию...
	{
		CLUNET_TIMER_REG_OCR = now + CLUNET_T;	// То вернёмся в это прерывание через CLUNET_T единиц времени
		return;
	}

	switch (clunetSendingState)
	{
	/* Инициализация */
	case CLUNET_SENDING_STATE_INIT:
		CLUNET_TIMER_REG_OCR = now + CLUNET_INIT_T;	
		clunetSendingState = CLUNET_SENDING_STATE_PRIO1;	// Начинаем следующую фазу
		return;
	/* Фаза передачи приоритета (старший бит) */
	case CLUNET_SENDING_STATE_PRIO1:
		CLUNET_TIMER_REG_OCR = now + (clunetCurrentPrio > 2) ? CLUNET_1_T : CLUNET_0_T;
		clunetSendingState = CLUNET_SENDING_STATE_PRIO2;
		return;
	/* Фаза передачи приоритета (младший бит) */
	case CLUNET_SENDING_STATE_PRIO2:
		CLUNET_TIMER_REG_OCR = now + (clunetCurrentPrio & 1) ? CLUNET_0_T : CLUNET_1_T;
		clunetSendingState = CLUNET_SENDING_STATE_DATA;
		return;
	/* Фаза передачи данных */
	case CLUNET_SENDING_STATE_DATA:
		CLUNET_TIMER_REG_OCR = now + (dataToSend[clunetSendingCurrentByte] & (1 << clunetSendingCurrentBit)) ? CLUNET_1_T : CLUNET_0_T;
		if (++clunetSendingCurrentBit & 8)
		{
			clunetSendingCurrentBit = 0;
			if (++clunetSendingCurrentByte >= clunetSendingDataLength)	// Данные закончились
				clunetSendingState = CLUNET_SENDING_STATE_STOP;		// Следующая фаза
		}
		return;
	}
}


void
clunet_start_send()
{
	CLUNET_SEND_0;
	if (clunetSendingState != CLUNET_SENDING_STATE_PREINIT)	// Если не нужна пауза...
		clunetSendingState = CLUNET_SENDING_STATE_INIT;	// Инициализация передачи
	clunetSendingCurrentByte = clunetSendingCurrentBit = 0;	// Обнуляем счётчик
	CLUNET_TIMER_REG_OCR = CLUNET_TIMER_REG + CLUNET_T;	// Планируем таймер, обычно почему-то прерывание срабатывает сразу
	CLUNET_ENABLE_TIMER_COMP;				// Включаем прерывание таймера-сравнения
}

void
clunet_send(unsigned char address, unsigned char prio, unsigned char command, char* data, unsigned char size)
{
	if (CLUNET_OFFSET_DATA + size >= CLUNET_SEND_BUFFER_SIZE) return;	// Не хватает буфера
	CLUNET_DISABLE_TIMER_COMP;						// Прерываем текущую передачу, если есть такая
	CLUNET_SEND_0;
	// Заполняем переменные
	if (clunetSendingState != CLUNET_SENDING_STATE_PREINIT)
		clunetSendingState = CLUNET_SENDING_STATE_IDLE;
	clunetCurrentPrio = prio;
	dataToSend[CLUNET_OFFSET_SRC_ADDRESS] = CLUNET_DEVICE_ID;
	dataToSend[CLUNET_OFFSET_DST_ADDRESS] = address;
	dataToSend[CLUNET_OFFSET_COMMAND] = command;
	dataToSend[CLUNET_OFFSET_SIZE] = size;
	for (uint8_t i = 0; i < size; i++)
		dataToSend[CLUNET_OFFSET_DATA + i] = data[i];
	dataToSend[CLUNET_OFFSET_DATA + size] = check_crc((char*)dataToSend, CLUNET_OFFSET_DATA + size);
	clunetSendingDataLength = CLUNET_OFFSET_DATA + size + 1;
	if ((clunetReadingState == CLUNET_READING_STATE_IDLE)		// Если мы ничего не получаем в данный момент, то посылаем сразу
//		|| ((clunetReadingState == CLUNET_READING_STATE_DATA) && (prio > clunetReceivingPrio))	// Либо если получаем, но с более низким приоритетом
		)
		clunet_start_send();	// Запускаем передачу сразу
	else clunetSendingState = CLUNET_SENDING_STATE_WAITING_LINE;	// Иначе ждём линию
}

inline void
clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size)
{	
	if (on_data_received_sniff)
		(*on_data_received_sniff)(src_address, dst_address, command, data, size);

	if (src_address == CLUNET_DEVICE_ID) return;	// Игнорируем сообщения от самого себя!

	if ((dst_address != CLUNET_DEVICE_ID) &&
		(dst_address != CLUNET_BROADCAST_ADDRESS)) return;	// Игнорируем сообщения не для нас

	if (command == CLUNET_COMMAND_REBOOT)	// Просто ребут. И да, ребутнуть себя мы можем
	{
		cli();
		set_bit(WDTCR, WDE);
		while(1);
	}

	if ((clunetSendingState == CLUNET_SENDING_STATE_IDLE) || (clunetCurrentPrio <= CLUNET_PRIORITY_MESSAGE))
	{
		if (command == CLUNET_COMMAND_DISCOVERY)	// Ответ на поиск устройств
		{
			clunetSendingState = CLUNET_SENDING_STATE_PREINIT;
#ifdef CLUNET_DEVICE_NAME
			char buf[] = CLUNET_DEVICE_NAME;
			uint8_t len = 0; while(buf[len]) len++;
			clunet_send(src_address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_DISCOVERY_RESPONSE, buf, len);
#else
			clunet_send(src_address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_DISCOVERY_RESPONSE, 0, 0);
#endif
		}
		else if (command == CLUNET_COMMAND_PING)	// Ответ на пинг
		{
			clunetSendingState = CLUNET_SENDING_STATE_PREINIT;
			clunet_send(src_address, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_PING_REPLY, data, size);
		}
	}
	
	if (on_data_received)
		(*on_data_received)(src_address, dst_address, command, data, size);
		
	/* Если есть неотосланные данные, шлём, линия освободилась */
	if ((clunetSendingState == CLUNET_SENDING_STATE_WAITING_LINE) && !CLUNET_READING)
	{
		clunetSendingState = CLUNET_SENDING_STATE_PREINIT;
		clunet_start_send();
	}
}

/* Процедура прерывания переполнения таймера */
ISR(CLUNET_TIMER_OVF_VECTOR)
{
	if (clunetTimerPeriods < 3)
		clunetTimerPeriods++;
	else	/* Слишком долго нет сигнала, сброс и отключение прерывания */
	{
		CLUNET_SEND_0; 	// А вдруг мы забыли линию отжать? Хотя по идее не должно...
		clunetReadingState = CLUNET_READING_STATE_IDLE;
		if ((clunetSendingState == CLUNET_SENDING_STATE_IDLE) && (!CLUNET_READING))
			CLUNET_DISABLE_TIMER_OVF;
		 /* Если есть неотосланные данные, шлём, линия освободилась */
		if ((clunetSendingState == CLUNET_SENDING_STATE_WAITING_LINE) && (!CLUNET_READING))
			clunet_start_send();
	}
}

/* Процедура внешнего прерывания по фронту и спаду сигнала */
ISR(CLUNET_INT_VECTOR)
{
	uint8_t now = CLUNET_TIMER_REG;		// Текущие значение таймера
	CLUNET_ENABLE_TIMER_OVF;		// Активируем прерывания переполнения таймера при любой активности линии
	/* Линию прижало к нулю */
	if (CLUNET_READING)
	{
		clunetTimerStart = now;		// Запомним время начала сигнала
		clunetTimerPeriods = 0;		// Сбросим счетчик периодов таймера
	}
	/* Линию отпустило */
	else
	{
		uint8_t ticks = now - clunetTimerStart;	// Вычислим длину сигнала в тиках таймера
		/* Если кто-то долго жмёт линию, это инициализация */
		if (ticks >= (CLUNET_INIT_T + CLUNET_1_T) / 2)
			clunetReadingState = CLUNET_READING_STATE_PRIO1;
		else	 // А если не долго, то смотрим на этап
		switch (clunetReadingState)
		{
		/* Получение приоритета (старший бит), клиенту он не нужен */
		case CLUNET_READING_STATE_PRIO1:
			clunetReadingState = CLUNET_READING_STATE_PRIO2;
			break;
		/* Получение приоритета (младший бит), клиенту он не нужен */
		case CLUNET_READING_STATE_PRIO2:
			clunetReadingState = CLUNET_READING_STATE_DATA;
			clunetReadingCurrentByte = clunetReadingCurrentBit = 0;
			dataToRead[0] = 0;
			break;
		/* Чтение данных */
		case CLUNET_READING_STATE_DATA:
			/* Если значащий бит */
			if (ticks > (CLUNET_0_T + CLUNET_1_T) / 2)
				dataToRead[clunetReadingCurrentByte] |= (1 << clunetReadingCurrentBit);
			if (++clunetReadingCurrentBit & 8)  // Переходим к следующему байту
			{
				/* Получили данные полностью, ура! */
				if ((++clunetReadingCurrentByte > CLUNET_OFFSET_SIZE) && (clunetReadingCurrentByte > dataToRead[CLUNET_OFFSET_SIZE] + CLUNET_OFFSET_DATA))
				{
					clunetReadingState = CLUNET_READING_STATE_IDLE;
					/* Проверяем CRC, при успехе начнем обработку принятого пакета */
					if (!check_crc((char*)dataToRead,clunetReadingCurrentByte))
						clunet_data_received (
							dataToRead[CLUNET_OFFSET_SRC_ADDRESS],
							dataToRead[CLUNET_OFFSET_DST_ADDRESS],
							dataToRead[CLUNET_OFFSET_COMMAND],
							(char*)(dataToRead + CLUNET_OFFSET_DATA),
							dataToRead[CLUNET_OFFSET_SIZE]
						);
				}
				else if (clunetReadingCurrentByte < CLUNET_READ_BUFFER_SIZE)
				{
					clunetReadingCurrentBit = 0;
					dataToRead[clunetReadingCurrentByte] = 0;
				}
				else // Если буфер закончился, то игнорируем входящий пакет
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
	uint8_t reset_source = MCUCSR;
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
int
clunet_ready_to_send()
{
	return (clunetSendingState) ? clunetCurrentPrio : 0;
}

void
clunet_set_on_data_received(void (*f)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size))
{	
	on_data_received = f;
}

void
clunet_set_on_data_received_sniff(void (*f)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size))
{	
	on_data_received_sniff = f;
}
