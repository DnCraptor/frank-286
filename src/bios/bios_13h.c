#include <stdio.h>
#include <ff.h>
#include "i286.h"
#include "bios.h"
#include "disk.h"

static bool no_handler() {
    print_line("DISK BIOS - ERROR: no handler defined", 1);
    char buf[10];
    snprintf(buf, 10, "AX: %04xh", CPU_AX); print_line(buf, 2);
    snprintf(buf, 10, "BX: %04xh", CPU_BX); print_line(buf, 3);
    snprintf(buf, 10, "CX: %04xh", CPU_CX); print_line(buf, 4);
    snprintf(buf, 10, "DX: %04xh", CPU_DX); print_line(buf, 5);
    snprintf(buf, 10, "SI: %04xh", CPU_SI); print_line(buf, 5);
    snprintf(buf, 10, "DI: %04xh", CPU_DI); print_line(buf, 6);
    snprintf(buf, 10, "BP: %04xh", CPU_BP); print_line(buf, 7);
    snprintf(buf, 10, "DS: %04xh", CPU_DS); print_line(buf, 8);
    snprintf(buf, 10, "SS: %04xh", CPU_SS); print_line(buf, 9);
    snprintf(buf, 10, "FS: %04xh", CPU_FS); print_line(buf, 10);
    snprintf(buf, 10, "GS: %04xh", CPU_GS); print_line(buf, 11);
    snprintf(buf, 10, "ES: %04xh", CPU_ES); print_line(buf, 12);
while(1); // remove it
    return true;
}

/*
DISK - READ SECTOR(S) INTO MEMORY
AH = 02h
AL = number of sectors to read (must be nonzero)
CH = low eight bits of cylinder number
CL = sector number 1-63 (bits 0-5)
high two bits of cylinder (bits 6-7, hard disk only)
DH = head number
DL = drive number (bit 7 set for hard disk)
ES:BX -> data buffer

Return:
CF set on error
if AH = 11h (corrected ECC error), AL = burst length
CF clear if successful
AH = status (see #00234)
AL = number of sectors transferred (only valid if CF set for some
BIOSes)

Notes: Errors on a floppy may be due to the motor failing to spin up quickly enough; the read should be retried at least three times,
 resetting the disk with AH=00h between attempts. Most BIOSes support "multitrack" reads, where the value in AL exceeds the number of
 sectors remaining on the track, in which case any additional sectors are read beginning at sector 1 on the following head in the
 same cylinder; the MSDOS CONFIG.SYS command MULTITRACK (or the Novell DOS DEBLOCK=) can be used to force DOS to split disk accesses
 which would wrap across a track boundary into two separate calls. The IBM AT BIOS and many other BIOSes use only the low four bits
 of DH (head number) since the WD-1003 controller which is the standard AT controller (and the controller that IDE emulates) only 
 supports 16 heads. AWARD AT BIOS and AMI 386sx BIOS have been extended to handle more than 1024 cylinders by placing bits 10 and 11
 of the cylinder number into bits 6 and 7 of DH. Under Windows95, a volume must be locked (see INT 21/AX=440Dh/CX=084Bh) in order to 
 perform direct accesses such as INT 13h reads and writes. All versions of MS-DOS (including MS-DOS 7 [Windows 95]) have a bug which 
 prevents booting on hard disks with 256 heads (FFh), so many modern BIOSes provide mappings with at most 255 (FEh) heads. Some cache 
 drivers flush their buffers when detecting that DOS is bypassed by directly issuing INT 13h from applications.
 A dummy read can be used as one of several methods to force cache flushing for unknown caches (e.g. before rebooting).
*/
static bool bios_13h_02h() {
    uint8_t count = CPU_AL;
    uint16_t cyl;
    uint8_t sect;
    uint8_t head;
    uint8_t drive;
    uint32_t lba;
    uint32_t addr;
    uint32_t offset;
    uint16_t heads;
    uint16_t sects;
    FIL *f = NULL;
    if (count == 0) {
        CPU_AH = 0x01;
        cf = 1;
        return true;
    }
    /*
     * CH = cylinder low 8 bits
     * CL bits 0-5 = sector
     * CL bits 6-7 = cylinder bits 8-9
     */
    cyl = ((uint16_t)(CPU_CL & 0xC0) << 2) | (uint16_t)CPU_CH;
    sect = CPU_CL & 0x3F;
    head = CPU_DH;
    drive = CPU_DL;
    if (sect == 0) {
        CPU_AH = 0x04; /* sector not found */
        cf = 1;
        return true;
    }
    if ((drive & 0x80) == 0) { // FDD
        if (drive >= 2 || !fdd_is_inserted(drive)) {
            CPU_AH = 0x80;
            cf = 1;
            return true;
        }
        heads = fdd_get_heads(drive);
        sects = fdd_get_sects(drive);
        f = fdd_get_file(drive);
    } else { // HDD
        uint8_t hdd = drive & 0x7F;
        if (hdd >= 4 || !ata_is_inserted(hdd) || ata_is_cdrom(hdd)) {
            CPU_AH = 0x80;
            cf = 1;
            return true;
        }
        heads = ata_get_heads(hdd);
        sects = ata_get_sects(hdd);
        f = ata_get_file(hdd);
    }
    if (!heads || !sects || !f) {
        CPU_AH = 0x80;
        cf = 1;
        return true;
    }
    if (head >= heads || sect > sects) {
        CPU_AH = 0x04;
        cf = 1;
        return true;
    }
    lba = ((uint32_t)cyl * heads + head) * sects + (uint32_t)(sect - 1);
    if (f_lseek(f, lba * 512u) != FR_OK) {
        CPU_AH = 0x40; /* seek failed */
        cf = 1;
        return true;
    }
    addr = ((uint32_t)CPU_ES << 4) + CPU_BX;
    for (uint32_t i = 0; i < count; i++) {
        uint8_t buf[512];
        UINT br = 0;
        if (f_read(f, buf, 512, &br) != FR_OK || br != 512) {
            CPU_AH = 0x20; /* controller failure */
            CPU_AL = (uint8_t)i;
            cf = 1;
            return true;
        }
        offset = addr + i * 512u;
        for (uint32_t j = 0; j < 512; j++) {
            pstore8(offset + j, buf[j]);
        }
    }
    CPU_AH = 0x00;
    CPU_AL = count;
    cf = 0;
    return true;
}

bool bios_13h() {
    switch(CPU_AH) {
        case 0x02:
            return bios_13h_02h(); // READ SECTOR(S) INTO MEMORY
        default:
            no_handler();
    }
    return true;
}
