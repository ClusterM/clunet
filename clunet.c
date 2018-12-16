/* Name: clunet.c
 * Project: CLUNET bus driver
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

volatile uint8_t clunet_sending_state = CLUNET_SENDING_STATE_IDLE;
volatile uint8_t clunet_sending_data_length;
volatile uint8_t clunet_sending_current_byte;
volatile uint8_t clunet_sending_current_bit;
volatile uint8_t clunet_reading_state = CLUNET_READING_STATE_IDLE;
volatile uint8_t clunet_reading_current_byte;
volatile uint8_t clunet_reading_current_bit;
volatile uint8_t clunet_sending_priority;
volatile uint8_t clunetTimerStart = 0;
volatile char out_buffer[CLUNET_SEND_BUFFER_SIZE];
volatile char in_buffer[CLUNET_READ_BUFFER_SIZE];

static inline void
clunet_start_send()
{
    clunet_sending_state = CLUNET_SENDING_STATE_INIT;
    // подождем 1.5Т, чтобы нас гарантированно могли остановить при передаче на линии со стороны другого устройства в процедуре внешнего прерывания
    CLUNET_TIMER_REG_OCR = CLUNET_TIMER_REG + (CLUNET_T + CLUNET_T / 2);
    CLUNET_ENABLE_TIMER_COMP; // Включаем прерывание сравнения таймера (передачу)
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

    if (src_address == CLUNET_DEVICE_ID) return; // Игнорируем сообщения от самого себя!

    if ((dst_address != CLUNET_DEVICE_ID) &&
        (dst_address != CLUNET_BROADCAST_ADDRESS)) return; // Игнорируем сообщения не для нас

    // Команда перезагрузки. Перезагрузим по сторожевому таймеру
    if (command == CLUNET_COMMAND_REBOOT)
    {
        cli();
        set_bit(WDTCR, WDE);
        while(1);
    }

    if ((clunet_sending_state == CLUNET_SENDING_STATE_IDLE) || (clunet_sending_priority <= CLUNET_PRIORITY_MESSAGE))
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
    /* Если достигли фазы завершения передачи, то завершим ее и освободим передатчик */
    if (clunet_sending_state == CLUNET_SENDING_STATE_DONE)
    {
        CLUNET_DISABLE_TIMER_COMP;                // Выключаем прерывание сравнения таймера
        clunet_sending_state = CLUNET_SENDING_STATE_IDLE;        // Указываем, что передатчик свободен
        CLUNET_SEND_0;                        // Отпускаем линию
    }
    /* Иначе если передачу необходимо продолжить, то сначала проверим на конфликт */
    else if (!CLUNET_SENDING && CLUNET_READING)
    {
        CLUNET_DISABLE_TIMER_COMP;                    // Выключаем прерывание сравнения таймера (передачу)
        clunet_sending_state = CLUNET_SENDING_STATE_WAITING_LINE;        // Переходим в режим ожидания линии
    }
    /* Все в порядке, можем продолжать */
    else
    {
        CLUNET_SEND_INVERT;    // Инвертируем значение сигнала
        
        /* Если отпустили линию, то запланируем время паузы перед следующей передачей длительностью 1Т */
        if (!CLUNET_SENDING)
            CLUNET_TIMER_REG_OCR += CLUNET_T;
    
        /* Если прижали линию к земле, то запланируем время передачи сигнала в зависимости от текущей фазы передачи */
        /* Фаза передачи данных */
        else if (clunet_sending_state == CLUNET_SENDING_STATE_DATA)
        {
            /* Планируем следующее прерывание в зависимости от значения бита */
            CLUNET_TIMER_REG_OCR += ((out_buffer[clunet_sending_current_byte] & (1 << clunet_sending_current_bit)) ? CLUNET_1_T : CLUNET_0_T);
            /* Если передан байт данных */
            if (++clunet_sending_current_bit & 8)
            {
                /* Если не все данные отосланы */
                if (++clunet_sending_current_byte < clunet_sending_data_length)
                    clunet_sending_current_bit = 0; // начинаем передачу следующего байта с бита 0
                /* Иначе передача всех данных закончена */
                else
                    clunet_sending_state++; // переходим к следующей фазе завершения передачи пакета
            }
        }
        else
            switch (clunet_sending_state++)
            {
            /* Фаза инициализации передачи пакета (время 10Т) */
            case CLUNET_SENDING_STATE_INIT:
                CLUNET_TIMER_REG_OCR += CLUNET_INIT_T;
                break;
            /* Фаза передачи приоритета (старший бит) */
            case CLUNET_SENDING_STATE_PRIO1:
                CLUNET_TIMER_REG_OCR += ((clunet_sending_priority > 2) ? CLUNET_1_T : CLUNET_0_T);
                break;
            /* Фаза передачи приоритета (младший бит) */
            case CLUNET_SENDING_STATE_PRIO2:
                CLUNET_TIMER_REG_OCR += ((clunet_sending_priority & 1) ? CLUNET_0_T : CLUNET_1_T);
                clunet_sending_current_byte = clunet_sending_current_bit = 0;    // Готовим счётчики передачи данных
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
        if (clunet_sending_state)
        {
            CLUNET_DISABLE_TIMER_COMP;
            CLUNET_SEND_0;
        }

        /* Заполняем переменные */
        clunet_sending_priority = (prio > 4) ? 4 : prio ? : 1;    // Ограничим приоритет диапазоном (1 ; 4)
        out_buffer[CLUNET_OFFSET_SRC_ADDRESS] = CLUNET_DEVICE_ID;
        out_buffer[CLUNET_OFFSET_DST_ADDRESS] = address;
        out_buffer[CLUNET_OFFSET_COMMAND] = command;
        out_buffer[CLUNET_OFFSET_SIZE] = size;
        
        /* Копируем данные в буфер */
        uint8_t i;
        for (i = 0; i < size; i++)
            out_buffer[CLUNET_OFFSET_DATA + i] = data[i];

        /* Добавляем контрольную сумму */
        out_buffer[CLUNET_OFFSET_DATA + size] = check_crc((char*)out_buffer, CLUNET_OFFSET_DATA + size);
        
        clunet_sending_data_length = size + (CLUNET_OFFSET_DATA + 1);

        // Если линия свободна, то запланируем передачу сразу
        if (!CLUNET_READING)
            clunet_start_send();
        // Иначе будем ожидать когда освободится в процедуре внешнего прерывания
        else
            clunet_sending_state = CLUNET_SENDING_STATE_WAITING_LINE;
    }
}

/* Процедура внешнего прерывания по фронту и спаду сигнала */
ISR(CLUNET_INT_VECTOR)
{
    uint8_t now = CLUNET_TIMER_REG;        // Текущие значение таймера

    /* Если линию прижало к нулю */
    if (CLUNET_READING)
    {
        clunetTimerStart = now;        // Запомним время начала сигнала
        /* Если мы в режиме передачи и прижали не мы, то замолкаем и ожидаем, тем более, что наши передаваемые данные уже битые */
        /* Обеспечивается быстрая отработка ошибки на линии во избежание конфликтов */
        if (clunet_sending_state && !CLUNET_SENDING)
        {
            CLUNET_DISABLE_TIMER_COMP;
            clunet_sending_state = CLUNET_SENDING_STATE_WAITING_LINE;
        }
    }
    /* Иначе если линию отпустило */
    else
    {
        /* Линия свободна, пробуем запланировать отправку */
        if (clunet_sending_state == CLUNET_SENDING_STATE_WAITING_LINE)
            clunet_start_send();

        uint8_t ticks = now - clunetTimerStart;    // Вычислим время сигнала в тиках таймера

        /* Если кто-то долго жмёт линию (время >= 6.5Т) - это инициализация */
        if (ticks >= (CLUNET_INIT_T + CLUNET_1_T) / 2)
        {
            clunet_reading_state = CLUNET_READING_STATE_PRIO1;
        }
        /* Иначе если недолго, то смотрим на этап */
        else
            switch (clunet_reading_state)
            {
            
            /* Чтение данных */
            case CLUNET_READING_STATE_DATA:

                /* Если бит значащий (время > 2Т), то установим его в приемном буфере */
                if (ticks > (CLUNET_0_T + CLUNET_1_T) / 2)
                    in_buffer[clunet_reading_current_byte] |= (1 << clunet_reading_current_bit);

                /* Инкрементируем указатель бита, и при полной обработке всех 8 бит в байте выполним: */
                if (++clunet_reading_current_bit & 8)
                {

                    /* Проверка на окончание чтения пакета */
                    if ((++clunet_reading_current_byte > CLUNET_OFFSET_SIZE) && (clunet_reading_current_byte > in_buffer[CLUNET_OFFSET_SIZE] + CLUNET_OFFSET_DATA))
                    {

                        clunet_reading_state = CLUNET_READING_STATE_IDLE;

                        /* Проверяем CRC, при успехе начнем обработку принятого пакета */
                        if (!check_crc((char*)in_buffer, clunet_reading_current_byte))
                            clunet_data_received (
                                in_buffer[CLUNET_OFFSET_SRC_ADDRESS],
                                in_buffer[CLUNET_OFFSET_DST_ADDRESS],
                                in_buffer[CLUNET_OFFSET_COMMAND],
                                (char*)(in_buffer + CLUNET_OFFSET_DATA),
                                in_buffer[CLUNET_OFFSET_SIZE]
                            );
                    
                    }
                    
                    /* Иначе если пакет не прочитан и буфер не закончился - подготовимся к чтению следующего байта */
                    else if (clunet_reading_current_byte < CLUNET_READ_BUFFER_SIZE)
                    {
                        clunet_reading_current_bit = 0;
                        in_buffer[clunet_reading_current_byte] = 0;
                    }
                    
                    /* Иначе - нехватка приемного буфера -> игнорируем пакет */
                    else
                        clunet_reading_state = CLUNET_READING_STATE_IDLE;

                }
                break;

            /* Получение приоритета (младший бит), клиенту он не нужен */
            case CLUNET_READING_STATE_PRIO2:
                clunet_reading_current_byte = clunet_reading_current_bit = 0;
                in_buffer[0] = 0;

            /* Получение приоритета (старший бит), клиенту он не нужен */
            case CLUNET_READING_STATE_PRIO1:
                clunet_reading_state++;
                break;
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

/* Возвращает 0, если готов к передаче, иначе приоритет текущей задачи */
uint8_t
clunet_ready_to_send()
{
    return clunet_sending_state ? clunet_sending_priority : 0;
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
