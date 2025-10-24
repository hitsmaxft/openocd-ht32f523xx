/***************************************************************************
 *   Copyright (C) 2005 by Dominic Rath                                    *
 *   Dominic.Rath@gmx.de                                                   *
 *                                                                         *
 *   Copyright (C) 2008 by Spencer Oliver                                  *
 *   spen@spen-soft.co.uk                                                  *
 *                                                                         *
 *   Copyright (C) 2011 by Clement Burin des Roziers                       *
 *   clement.burin-des-roziers@hikob.com                                   *
 *                                                                         *
 *   Copyright (C) 2022 by Ziyang Zheng                                    *
 *   2421498578@qq.com                                                     *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "imp.h"
#include <helper/binarybuffer.h>
#include <target/algorithm.h>
#include <target/armv7m.h>
#include <target/cortex_m.h>

/*******HT32F523xx Flash Manager Contorler(FMC) regisister location*******/
#define FMC_REG_BASE  0x40080000  /*FLASH Manager Contorller base addr   */
/*************************************************************************/
#define FMC_REG_TADR  0x000  /*FLASH ADDR REG                            */
#define FMC_REG_WRDR  0x004  /*Data for writing REG                      */
#define FMC_REG_OCMR  0x00C  /*Operation CMD REG                         */
#define FMC_REG_OPCR  0x010  /*Operation CTRL REG                        */
#define FMC_REG_OIER  0x014  /*Interrupt ENABLE REG                      */
#define FMC_REG_OISR  0x018  /*Interrupt STATUS REG                      */
#define FMC_REG_PPSR  0x020  /*Page erase/Programming protect STATUS REG */
#define FMC_REG_CPSR  0x030  /*Safety protection STATUS REG              */
#define FMC_REG_VMCR  0x100  /*Vector mapping CTRL REG                   */
#define FMC_REG_MDID  0x180  /*Manufacture and Microcontroller ID REG    */
#define FMC_REG_PNSR  0x184  /*Pages STATUS REG                          */
#define FMC_REG_PSSR  0x188  /*Size of page STATUS REG                   */
#define FMC_REG_CFCR  0x200  /*Cache and pre-read control REG            */
#define FMC_REG_CIDR0 0x310  /*User-defined ID REG0                      */
#define FMC_REG_CIDR1 0x314  /*User-defined ID REG1                      */
#define FMC_REG_CIDR2 0x318  /*User-defined ID REG2                      */
#define FMC_REG_CIDR3 0x31C  /*User-defined ID REG3                      */
/*************************************************************************/

/***************************FMC OPRC regisister***************************/
#define FMC_OPM_MASK  0x1E
#define FMC_COMMIT   (0xA << 1)
#define FMC_FINISHED (0xE << 1)
#define FMC_START    (0x6 << 1)
/*************************************************************************/

/***************************FMC OCMR regisister***************************/
#define FMC_CMD_MASK        0xF
#define FMC_CMD_WORD_PROG   0x4
#define FMC_CMD_PAGE_ERASE  0x8
#define FMC_CMD_MASS_ERASE  0xA
/*************************************************************************/

/***************************FMC option byte regisister***************************/
#define OPTION_BYTE_BASE 0x1FF00000
#define OPT_OB_PP 0x000
#define OPT_OB_CP 0x010

/*************************************************************************/

#define FLASH_ERASE_TIMEOUT  1000  /*timeout count*/

/*Command flash bank ht32f523xx <base> <size> 0 0 <target>*/
FLASH_BANK_COMMAND_HANDLER(ht32f523xx_flash_bank_command)
{
    if(CMD_ARGC < 6)
        return ERROR_COMMAND_SYNTAX_ERROR;
    
    bank->driver_priv = NULL;

    return ERROR_OK;
}

static inline int ht32f523xx_get_flash_status(struct flash_bank *bank, uint32_t *status)
{
    struct target *target = bank->target;
    return target_read_u32(target,FMC_REG_BASE + FMC_REG_OPCR, status);
}

static int ht32f523xx_wait_status_busy(struct flash_bank *bank,int timeout)
{
    uint32_t status;
    int retval = ERROR_OK;

    for(;;){
        retval = ht32f523xx_get_flash_status(bank,&status);
        if(retval != ERROR_OK){
            return retval;
        }
        if( ((status & FMC_OPM_MASK)==FMC_FINISHED) || ((status & FMC_OPM_MASK) == FMC_START) ){
            return ERROR_OK;
        }
        if(timeout-- <= 0){
            LOG_DEBUG("Timed out waiting for flash: 0x%04x", status);
            return ERROR_FAIL;
        }
        alive_sleep(10);
    }

    return retval;
}

static int ht32f523xx_erase(struct flash_bank *bank, unsigned int first, unsigned int last)
{
    struct target *target = bank->target;

    LOG_DEBUG("ht32f523xx erase: %d - %d", first, last);

    if(target->state != TARGET_HALTED){
        LOG_ERROR("Target not halted");
        return ERROR_TARGET_NOT_HALTED;
    }

    for(unsigned int i = first ; i <= last; ++i){
        /*flash memory page erase*/
        int retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_TADR,bank->sectors[i].offset);
        if(retval != ERROR_OK){
            return retval;
        }

        retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_OCMR,FMC_CMD_PAGE_ERASE);
        if(retval != ERROR_OK){
            return retval;
        }

        retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_OPCR,FMC_COMMIT);
        if(retval != ERROR_OK){
            return retval;
        }

        //wait
        retval = ht32f523xx_wait_status_busy(bank,FLASH_ERASE_TIMEOUT);
        if(retval != ERROR_OK){
            return retval;
        }

        LOG_DEBUG("HT32F523xx erased page %d", i);
        bank->sectors[i].is_erased = 1;
    }

    return ERROR_OK;
}

static int ht32f523xx_protect(struct flash_bank *bank, int set, unsigned int first, unsigned int last)
{
    return ERROR_FLASH_OPER_UNSUPPORTED;
}

static int ht32f523xx_write(struct flash_bank *bank, const uint8_t *buffer,uint32_t offset, uint32_t count)
{
    struct target *target = bank->target;

    LOG_DEBUG("ht32f523xx flash write: 0x%x 0x%x", offset, count);

    if(target->state != TARGET_HALTED){
        LOG_ERROR("Target not halted");
        return ERROR_TARGET_NOT_HALTED;
    }
    if(offset & 0x03){
        LOG_ERROR("offset 0x%" PRIx32 " breaks required 4-byte alignment", offset);
        return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
    }
    if(count & 0x3){
        LOG_ERROR("size 0x%" PRIx32 " breaks required 4-byte alignment", count);
        return ERROR_FLASH_DST_BREAKS_ALIGNMENT;
    }

    uint32_t addr = offset;
    for(uint32_t i = 0; i < count; i += 4){
        uint32_t word = (buffer[i]   << 0)  |
                        (buffer[i+1] << 8)  |
                        (buffer[i+2] << 16) |
                        (buffer[i+3] << 24);

        LOG_DEBUG("ht32f523xx flash write word 0x%x 0x%x 0x%08x", i, addr, word);

        // flash memory word program
        int retval;
        retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_TADR, addr);
        if (retval != ERROR_OK)
            return retval;
        retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_WRDR, word);
        if (retval != ERROR_OK)
            return retval;
        retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_OCMR, FMC_CMD_WORD_PROG);
        if (retval != ERROR_OK)
            return retval;
        retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_OPCR, FMC_COMMIT);
        if (retval != ERROR_OK)
            return retval;

        // wait
        retval = ht32f523xx_wait_status_busy(bank, FLASH_ERASE_TIMEOUT);
        if (retval != ERROR_OK)
            return retval;
        addr += 4;
    }

    LOG_DEBUG("ht32f523xx flash write success");
    return ERROR_OK;
}

static int ht32f523xx_probe(struct flash_bank *bank)
{
    int page_size = 512;
    int num_pages = bank->size / page_size;

    LOG_INFO("ht32f523xxx probe: %d pages, 0x%x bytes, 0x%x total", num_pages, page_size, bank->size);

    if(bank->sectors){
        free(bank->sectors);
    }

    bank->base = 0x0;
    bank->num_sectors = num_pages;
    bank->sectors = malloc(sizeof(struct flash_sector) * num_pages);

    for(int i = 0; i < num_pages; ++i){
        bank->sectors[i].offset = i * page_size;
        bank->sectors[i].size = page_size;
        bank->sectors[i].is_erased = -1;
        bank->sectors[i].is_protected = 1;
    }

    return ERROR_OK;
}

static int ht32f523xx_auto_probe(struct flash_bank * bank)
{
    return ht32f523xx_probe(bank);
}

static int ht32f523xx_protect_check(struct flash_bank *bank)
{
    struct target *target = bank->target;
    uint32_t ob_pp[4];
    uint32_t ob_cp;
    
    //read OB_PP REG
    for(int i=0; i<4; ++i)
    {
        target_read_u32(target,(OPTION_BYTE_BASE + OPT_OB_PP) + (i << 2),ob_pp + i);
    }

    //read OB_CP REG
    target_read_u32(target,(OPTION_BYTE_BASE + OPT_OB_CP),&ob_cp);

    LOG_INFO("ht32f523xx opt byte: %04x %04x %04x %04x %04x", ob_pp[0], ob_pp[1], ob_pp[2], ob_pp[3], ob_cp);
    
    // Set page protection
    for(int i = 0 ; i < 128; ++i){
        int bit = (ob_pp[i / 32] << (i % 32)) & 1;
        bank->sectors[2*i].is_protected = bit ? 0 : 1;
        bank->sectors[(2*i)+1].is_protected = bit ? 0 : 1;
    }

    return ERROR_OK;
}

static int ht32f523xx_info(struct flash_bank *bank, struct command_invocation *cmd)
{
    command_print_sameline(cmd, "ht32f523xx");
    return ERROR_OK;
}

static int ht32f523xx_mass_erase(struct flash_bank *bank)
{
    struct target *target = bank->target;

    if (target->state != TARGET_HALTED) {
        LOG_ERROR("Target not halted");
        return ERROR_TARGET_NOT_HALTED;
    }

    //WITHOUT FMC_BUSY CHECK?!

    // flash memory mass erase
    int retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_OCMR, FMC_CMD_MASS_ERASE);
    if (retval != ERROR_OK)
        return retval;
    retval = target_write_u32(target, FMC_REG_BASE + FMC_REG_OPCR, FMC_COMMIT);
    if (retval != ERROR_OK)
        return retval;

    retval = ht32f523xx_wait_status_busy(bank, FLASH_ERASE_TIMEOUT);
    if (retval != ERROR_OK)
        return retval;

    return ERROR_OK;
}

COMMAND_HANDLER(ht32f523xx_handle_mass_erase_command)
{
    if(CMD_ARGC < 6){
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    struct flash_bank *bank;
    int retval = CALL_COMMAND_HANDLER(flash_command_get_bank,0,&bank);
    if(retval != ERROR_OK){
        return retval;
    }

    retval = ht32f523xx_mass_erase(bank);
    if (retval == ERROR_OK) {
        // set all sectors as erased
        unsigned int i;
        for (i = 0; i < bank->num_sectors; i++)
            bank->sectors[i].is_erased = 1;

        //command_print(CMD_CTX, "ht32f523xx mass erase complete");
    } else {
        //command_print(CMD_CTX, "ht32f523xx mass erase failed");
    }

    return ERROR_OK;
}

COMMAND_HANDLER(ht32f523xx_handle_test_write)
{
    if(CMD_ARGC < 6){
        return ERROR_COMMAND_SYNTAX_ERROR;
    }

    struct flash_bank *bank;
    int retval = CALL_COMMAND_HANDLER(flash_command_get_bank,0,&bank);
    if(retval != ERROR_OK){
        return retval;
    }

    uint8_t buffer[32];
    for(int i = 0; i < 32; ++i){
        buffer[i] = i;
    }

    retval = ht32f523xx_erase(bank, 0, 0);
    if (retval != ERROR_OK)
        return retval;

    retval = ht32f523xx_write(bank, buffer, 0, 32);
    if (retval == ERROR_OK) {
        //command_print(CMD_CTX, "ht32f523xx test write complete");
    } else {
        //command_print(CMD_CTX, "ht32f523xx test write failed");
    }

    return retval;
}

static const struct command_registration ht32f523xx_exec_command_handlers[] = {
    {
        .name = "mass_erase",
        .handler = ht32f523xx_handle_mass_erase_command,
        .mode = COMMAND_EXEC,
        .usage = "bank_id",
        .help = "test flash write",
    },
    {
        .name = "test_write",
        .handler = ht32f523xx_handle_test_write,
        .mode = COMMAND_EXEC,
        .usage = "bank_id",
        .help = "test flash write",
    },
    COMMAND_REGISTRATION_DONE
};

static const struct command_registration ht32f523xx_command_handlers[] = {
    {
        .name = "ht32f523xx",
        .mode = COMMAND_ANY,
        .help = "ht32f523xx flash command group",
        .usage = "",
        .chain = ht32f523xx_exec_command_handlers,
    },
    COMMAND_REGISTRATION_DONE
};

const struct flash_driver ht32f523xx_flash = {
    .name = "ht32f523xx",
    .commands = ht32f523xx_command_handlers,
    .flash_bank_command = ht32f523xx_flash_bank_command,

    .erase = ht32f523xx_erase,
    .protect = ht32f523xx_protect,
    .write = ht32f523xx_write,
    .read = default_flash_read,
    .probe = ht32f523xx_probe,
    .auto_probe = ht32f523xx_auto_probe,
    .erase_check = default_flash_blank_check,
    .protect_check = ht32f523xx_protect_check,
    .info = ht32f523xx_info,
	.free_driver_priv = default_flash_free_driver_priv,
};
