/* Name: clunet.c
 * Project: CLUNET network driver
 * Author: Alexey Avdyukhin
 * Creation Date: 2013-07-22
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
volatile unsigned char clunetReceivingPrio = 0;
volatile unsigned char clunetTimerStart = 0;
volatile unsigned char clunetTimerPeriods = 0;

volatile char dataToSend[CLUNET_SEND_BUFFER_SIZE];
volatile char dataToRead[CLUNET_READ_BUFFER_SIZE];

char check_crc(char* data, unsigned char size)
{
      uint8_t crc=0;
      uint8_t i,j;
      for (i=0; i<size;i++) 
      {
            uint8_t inbyte = data[i];
            for (j=0;j<8;j++) 
            {
                  uint8_t mix = (crc ^ inbyte) & 0x01;
                  crc >>= 1;
                  if (mix) 
                        crc ^= 0x8C;
                  
                  inbyte >>= 1;
            }
      }
      return crc;
}

ISR(CLUNET_TIMER_COMP_VECTOR)
{		
	unsigned char now = CLUNET_TIMER_REG;     // Запоминаем текущее время
	
	switch (clunetSendingState)
	{
		case CLUNET_SENDING_STATE_PREINIT: // Нужно подождать перед отправкой
			CLUNET_TIMER_REG_OCR = now + CLUNET_T;	
			clunetSendingState = CLUNET_SENDING_STATE_INIT;  // Начинаем следующую фазу
			return;	
		case CLUNET_SENDING_STATE_STOP:	// Завершение передачи, но надо ещё подождать
			CLUNET_SEND_0;				// Отпускаем линию
			CLUNET_TIMER_REG_OCR = now + CLUNET_T;
			clunetSendingState = CLUNET_SENDING_STATE_DONE;
			return;
		case CLUNET_SENDING_STATE_DONE:	// Завершение передачи
			CLUNET_DISABLE_TIMER_COMP; // Выключаем таймер-сравнение
			clunetSendingState = CLUNET_SENDING_STATE_IDLE; // Ставим флаг, что передатчик свободен
			return;		
	}	

	if (/*!((clunetReadingState == CLUNET_READING_STATE_DATA) && // Если мы сейчас не [получаем данные
		(clunetCurrentPrio > clunetReceivingPrio) 				// И приоритет получаемых данных не ниже
		&& (clunetSendingState == CLUNET_SENDING_STATE_INIT))  // И мы не пытаемся начать инициализацию]		
		&&*/ (!CLUNET_SENDING && CLUNET_READING)) // То идёт проверка на конфликт. Если мы линию не держим, но она уже занята
	{
		CLUNET_DISABLE_TIMER_COMP; // Выключаем таймер-сравнение
		clunetSendingState = CLUNET_SENDING_STATE_WAITING_LINE; // Переходим в режим ожидания линии
		return;											 // И умолкаем
	}

	CLUNET_SEND_INVERT;	 // Сразу инвртируем значение сигнала, у нас это запланировано
	
	if (!CLUNET_SENDING)        // Если мы отпустили линию...
	{
		CLUNET_TIMER_REG_OCR = now + CLUNET_T; // То вернёмся в это прерывание через CLUNET_T единиц времени
		return;
	}
	switch (clunetSendingState)
	{
		case CLUNET_SENDING_STATE_INIT: // Инициализация
			CLUNET_TIMER_REG_OCR = now + CLUNET_INIT_T;	
			clunetSendingState = CLUNET_SENDING_STATE_PRIO1;  // Начинаем следующую фазу
			return;
		case CLUNET_SENDING_STATE_PRIO1: // Фаза передачи приоритета, старший бит
			if ((clunetCurrentPrio-1) & 2) // Если 1, то ждём 3T, а если 0, то 1T
				CLUNET_TIMER_REG_OCR = now + CLUNET_1_T;
			else CLUNET_TIMER_REG_OCR = now + CLUNET_0_T;	
			clunetSendingState = CLUNET_SENDING_STATE_PRIO2;
			return;
		case CLUNET_SENDING_STATE_PRIO2: // Фаза передачи приоритета, младший бит
			if ((clunetCurrentPrio-1) & 1) // Если 1, то ждём 3T, а если 0, то 1T
				CLUNET_TIMER_REG_OCR = now + CLUNET_1_T;
			else CLUNET_TIMER_REG_OCR = now + CLUNET_0_T;	
			clunetSendingState = CLUNET_SENDING_STATE_DATA;
			return;
		case CLUNET_SENDING_STATE_DATA: // Фаза передачи данных
			if (dataToSend[clunetSendingCurrentByte] & (1 << clunetSendingCurrentBit)) // Если 1, то ждём 3T, а если 0, то 1T
				CLUNET_TIMER_REG_OCR = now + CLUNET_1_T;
			else CLUNET_TIMER_REG_OCR = now + CLUNET_0_T;
			clunetSendingCurrentBit++; // Переходим к следующему биту
			if (clunetSendingCurrentBit >= 8)
			{
				clunetSendingCurrentBit = 0;
				clunetSendingCurrentByte++;
			}
			if (clunetSendingCurrentByte >= clunetSendingDataLength) // Данные закончились
			{
				clunetSendingState = CLUNET_SENDING_STATE_STOP; // Следующая фаза
			}
			return;
	}
}


void clunet_start_send()
{
	CLUNET_SEND_0;
	if (clunetSendingState != CLUNET_SENDING_STATE_PREINIT) // Если не нужна пауза...
		clunetSendingState = CLUNET_SENDING_STATE_INIT; // Инициализация передачи
	clunetSendingCurrentByte = clunetSendingCurrentBit = 0; // Обнуляем счётчик
	CLUNET_TIMER_REG_OCR = CLUNET_TIMER_REG+CLUNET_T;			// Планируем таймер, обычно почему-то прерывание срабатывает сразу
	CLUNET_ENABLE_TIMER_COMP;			// Включаем прерывание таймера-сравнения
}

void clunet_send(unsigned char address, unsigned char prio, unsigned char command, char* data, unsigned char size)
{
	if (CLUNET_OFFSET_DATA+size+1 > CLUNET_SEND_BUFFER_SIZE) return; // Не хватает буфера
	CLUNET_DISABLE_TIMER_COMP;CLUNET_SEND_0; // Прерываем текущую передачу, если есть такая
	// Заполняем переменные
	if (clunetSendingState != CLUNET_SENDING_STATE_PREINIT)
		clunetSendingState = CLUNET_SENDING_STATE_IDLE;
	clunetCurrentPrio = prio;
	dataToSend[CLUNET_OFFSET_SRC_ADDRESS] = CLUNET_DEVICE_ID;
	dataToSend[CLUNET_OFFSET_DST_ADDRESS] = address;
	dataToSend[CLUNET_OFFSET_COMMAND] = command;
	dataToSend[CLUNET_OFFSET_SIZE] = size;
	unsigned char i;
	for (i = 0; i < size; i++)	
		dataToSend[CLUNET_OFFSET_DATA+i] = data[i];
	dataToSend[CLUNET_OFFSET_DATA+size] = check_crc((char*)dataToSend, CLUNET_OFFSET_DATA+size);
	clunetSendingDataLength = CLUNET_OFFSET_DATA + size + 1;
	if (
		(clunetReadingState == CLUNET_READING_STATE_IDLE) // Если мы ничего не получаем в данный момент, то посылаем сразу		
//		|| ((clunetReadingState == CLUNET_READING_STATE_DATA) && (prio > clunetReceivingPrio)) // Либо если получаем, но с более низким приоритетом
		)
		clunet_start_send(); // Запускаем передачу сразу
	else clunetSendingState = CLUNET_SENDING_STATE_WAITING_LINE; // Иначе ждём линию
}

inline void clunet_data_received(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size)
{	
	if (on_data_received_sniff)
		(*on_data_received_sniff)(src_address, dst_address, command, data, size);

	if (src_address == CLUNET_DEVICE_ID) return; // Игнорируем сообщения от самого себя!

	if ((dst_address != CLUNET_DEVICE_ID) &&
		(dst_address != CLUNET_BROADCAST_ADDRESS)) return; // Игнорируем сообщения не для нас					

	if (command == CLUNET_COMMAND_REBOOT) // Просто ребут. И да, ребутнуть себя мы можем
	{
		cli();
		set_bit(WDTCR, WDE);
		while(1);
	}

	if ((clunetSendingState == CLUNET_SENDING_STATE_IDLE) || (clunetCurrentPrio <= CLUNET_PRIORITY_MESSAGE))
	{
		if (command == CLUNET_COMMAND_DISCOVERY) // Ответ на поиск устройств
		{
			char buf[] = CLUNET_DEVICE_NAME;
			int len = 0; while(buf[len]) len++;
			clunetSendingState = CLUNET_SENDING_STATE_PREINIT;
			clunet_send(src_address, CLUNET_PRIORITY_MESSAGE, CLUNET_COMMAND_DISCOVERY_RESPONSE, buf, len);
		}
		else if (command == CLUNET_COMMAND_PING) // Ответ на пинг
		{
			clunetSendingState = CLUNET_SENDING_STATE_PREINIT;
			clunet_send(src_address, CLUNET_PRIORITY_COMMAND, CLUNET_COMMAND_PING_REPLY, data, size);
		}
	}
	
	if (on_data_received)
		(*on_data_received)(src_address, dst_address, command, data, size);
		
	if ((clunetSendingState == CLUNET_SENDING_STATE_WAITING_LINE) && !CLUNET_READING) // Если есть неотосланные данные, шлём, линия освободилась
	{
		clunetSendingState = CLUNET_SENDING_STATE_PREINIT;
		clunet_start_send();		
	}
}

ISR(CLUNET_TIMER_OVF_VECTOR)
{		
	if (clunetTimerPeriods < 3)
		clunetTimerPeriods++;
	else // Слишком долго нет сигнала, сброс и отключение прерывания
	{
		CLUNET_SEND_0; 					// А вдруг мы забыли линию отжать? Хотя по идее не должно...
		clunetReadingState = CLUNET_READING_STATE_IDLE;
		if ((clunetSendingState == CLUNET_SENDING_STATE_IDLE) && (!CLUNET_READING))
			CLUNET_DISABLE_TIMER_OVF;
		if ((clunetSendingState == CLUNET_SENDING_STATE_WAITING_LINE) && (!CLUNET_READING)) // Если есть неотосланные данные, шлём, линия освободилась
			clunet_start_send();
	}
}


ISR(CLUNET_INT_VECTOR)
{
	unsigned char time = (unsigned char)((CLUNET_TIMER_REG-clunetTimerStart) & 0xFF);
	if (!CLUNET_READING) // Линию отпустило
	{
		CLUNET_ENABLE_TIMER_OVF;
		if (time >= (CLUNET_INIT_T+CLUNET_1_T)/2) // Если кто-то долго жмёт линию, это инициализация
		{
			clunetReadingState = CLUNET_READING_STATE_PRIO1;
		}
		else switch (clunetReadingState) // А если не долго, то смотрим на этап
		{
			case CLUNET_READING_STATE_PRIO1:    // Получение приоритета, клиенту он не нужен
				if (time > (CLUNET_0_T+CLUNET_1_T)/2)
					clunetReceivingPrio = 3;
					else clunetReceivingPrio = 1;
				clunetReadingState = CLUNET_READING_STATE_PRIO2;
				break;
			case CLUNET_READING_STATE_PRIO2:	 // Получение приоритета, клиенту он не нужен
				if (time > (CLUNET_0_T+CLUNET_1_T)/2)
					clunetReceivingPrio++;
				clunetReadingState = CLUNET_READING_STATE_DATA;
				clunetReadingCurrentByte = 0;
				clunetReadingCurrentBit = 0;
				dataToRead[0] = 0;
				break;
			case CLUNET_READING_STATE_DATA:    // Чтение всех данных
				if (time > (CLUNET_0_T+CLUNET_1_T)/2)
					dataToRead[clunetReadingCurrentByte] |= (1 << clunetReadingCurrentBit);
				clunetReadingCurrentBit++;
				if (clunetReadingCurrentBit >= 8)  // Переходим к следующему байту
				{
					clunetReadingCurrentByte++;
					clunetReadingCurrentBit = 0;
					if (clunetReadingCurrentByte < CLUNET_READ_BUFFER_SIZE)
						dataToRead[clunetReadingCurrentByte] = 0;
					else // Если буфер закончился
					{
						clunetReadingState = CLUNET_READING_STATE_IDLE;
						return;
					}
				}				
				if ((clunetReadingCurrentByte > CLUNET_OFFSET_SIZE) && (clunetReadingCurrentByte > dataToRead[CLUNET_OFFSET_SIZE]+CLUNET_OFFSET_DATA))
				{
					// Получили данные полностью, ура!
					clunetReadingState = CLUNET_READING_STATE_IDLE;
					char crc = check_crc((char*)dataToRead,clunetReadingCurrentByte); // Проверяем CRC
					if (crc == 0)
						clunet_data_received(dataToRead[CLUNET_OFFSET_SRC_ADDRESS], dataToRead[CLUNET_OFFSET_DST_ADDRESS], dataToRead[CLUNET_OFFSET_COMMAND], (char*)(dataToRead+CLUNET_OFFSET_DATA), dataToRead[CLUNET_OFFSET_SIZE]);
				}
				break;
		}
	}	
	else 
	{
		clunetTimerStart = CLUNET_TIMER_REG;
		clunetTimerPeriods = 0;
		CLUNET_ENABLE_TIMER_OVF;
	}
}

void clunet_init()
{
	sei();
	CLUNET_SEND_INIT;
	CLUNET_READ_INIT;
	CLUNET_TIMER_INIT;
	CLUNET_INIT_INT;
	CLUNET_ENABLE_INT;
	char reset_source = MCUCSR;
	clunet_send(CLUNET_BROADCAST_ADDRESS, CLUNET_PRIORITY_MESSAGE,	CLUNET_COMMAND_BOOT_COMPLETED, &reset_source, 1);
	MCUCSR = 0;
}

int clunet_ready_to_send() // Возвращает 0, если готов к передаче, иначе приоритет текущей задачи
{
	if (clunetSendingState == CLUNET_SENDING_STATE_IDLE) return 0;
	return clunetCurrentPrio;
}

void clunet_set_on_data_received(void (*f)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size))
{	
	on_data_received = f;
}

void clunet_set_on_data_received_sniff(void (*f)(unsigned char src_address, unsigned char dst_address, unsigned char command, char* data, unsigned char size))
{	
	on_data_received_sniff = f;
}

