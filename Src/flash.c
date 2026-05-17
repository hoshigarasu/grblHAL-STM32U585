/*

  flash.c - driver code for STM32F4xx/STM32U5xx ARM processors

  Part of grblHAL

  Copyright (c) 2019-2025 Terje Io

  This code reads/writes the whole RAM-based emulated EPROM contents from/to flash

  STM32U585 notes:
    - Flash is dual-bank (Bank1: 0x08000000-0x080FFFFF, Bank2: 0x08100000-0x081FFFFF)
    - Page size: 8KB (FLASH_PAGE_SIZE = 0x2000)
    - Write granularity: 128-bit quadword (FLASH_TYPEPROGRAM_QUADWORD)
    - Bank must be derived from address; hardcoding FLASH_BANK_1 causes erase failure
      when _EEPROM_Emul_Start resides in Bank2 (e.g. last page = 0x081FE000)

  grblHAL is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  grblHAL is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with grblHAL. If not, see <http://www.gnu.org/licenses/>.

*/

#include <string.h>

#include "driver.h"
#include "grbl/hal.h"

#if FLASH_ENABLE

extern void *_EEPROM_Emul_Start;
extern uint8_t _EEPROM_Emul_Sector;

bool memcpy_from_flash (uint8_t *dest)
{
    memcpy(dest, &_EEPROM_Emul_Start, hal.nvs.size);

    return true;
}

bool memcpy_to_flash (uint8_t *source)
{
    if (!memcmp(source, &_EEPROM_Emul_Start, hal.nvs.size))
        return true;

    HAL_StatusTypeDef status;

    if((status = HAL_FLASH_Unlock()) == HAL_OK) {

        #ifdef STM32U585xx
        uint32_t eeprom_addr = (uint32_t)&_EEPROM_Emul_Start;
        uint32_t eeprom_bank, eeprom_page;
        if (eeprom_addr >= (FLASH_BASE + 0x100000UL)) {
           eeprom_bank = FLASH_BANK_2;
           eeprom_page = (eeprom_addr - (FLASH_BASE + 0x100000UL)) / FLASH_PAGE_SIZE;
        } else {
           eeprom_bank = FLASH_BANK_1;
           eeprom_page = (eeprom_addr - FLASH_BASE) / FLASH_PAGE_SIZE;
        }
        FLASH_EraseInitTypeDef erase = { .Banks=eeprom_bank, .TypeErase=FLASH_TYPEERASE_PAGES, .Page=eeprom_page, .NbPages=1 };
#else
        static FLASH_EraseInitTypeDef erase = { .Banks=FLASH_BANK_1, .Sector=(uint32_t)&_EEPROM_Emul_Sector, .TypeErase=FLASH_TYPEERASE_SECTORS, .NbSectors=1, .VoltageRange=FLASH_VOLTAGE_RANGE_3 };
#endif

        uint32_t error;

        // Retry erase once if it fails (ref issue #121)
        if((status = HAL_FLASHEx_Erase(&erase, &error)) != HAL_OK)
            status = HAL_FLASHEx_Erase(&erase, &error);

        #ifdef STM32U585xx
        uint8_t *data=source, qbuf[16];
        uint32_t address=(uint32_t)&_EEPROM_Emul_Start, remaining=hal.nvs.size;
        while(remaining>0 && status==HAL_OK) {
            uint32_t chunk=remaining>=16?16:remaining;
            memset(qbuf,0xFF,16); memcpy(qbuf,data,chunk);
            status=HAL_FLASH_Program(FLASH_TYPEPROGRAM_QUADWORD,address,(uint32_t)qbuf);
            address+=16; data+=chunk; remaining-=chunk;
        }
#else
        uint16_t *data=(uint16_t*)source;
        uint32_t address=(uint32_t)&_EEPROM_Emul_Start, remaining=hal.nvs.size;
        while(remaining && status==HAL_OK) {
            if((status=HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,address,*data++))==HAL_OK)
                status=HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD,address+2,*data++);
            address+=4; remaining-=4;
        }
#endif

        HAL_FLASH_Lock();
    }

    return status == HAL_OK;
}

#endif
