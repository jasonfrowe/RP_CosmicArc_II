#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include "game.h"
#include "save.h"

#define SAVE_MAGIC 0x53324143u /* 'CA2S' */

static const char *slot_path(uint8_t slot)
{
    static const char *const paths[SAVE_SLOTS] = {
        "SAVE:ca2_s0.dat",
        "SAVE:ca2_s1.dat",
        "SAVE:ca2_s2.dat"
    };
    return paths[slot < SAVE_SLOTS ? slot : 0];
}

static uint8_t popcount16(uint16_t v)
{
    uint8_t c = 0;
    while (v) {
        c += (uint8_t)(v & 1u);
        v >>= 1;
    }
    return c;
}

bool save_write(uint8_t slot)
{
    g_voy.magic = SAVE_MAGIC;
    int fd = open(slot_path(slot), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return false;
    int n = write(fd, &g_voy, sizeof(g_voy));
    close(fd);
    return n == (int)sizeof(g_voy);
}

bool save_read(uint8_t slot)
{
    voyage_t tmp;
    int fd = open(slot_path(slot), O_RDONLY);
    if (fd < 0)
        return false;
    int n = read(fd, &tmp, sizeof(tmp));
    close(fd);
    if (n != (int)sizeof(tmp) || tmp.magic != SAVE_MAGIC)
        return false;
    g_voy = tmp;
    return true;
}

void save_erase(uint8_t slot)
{
    int fd = open(slot_path(slot), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        uint32_t zero = 0;
        (void)write(fd, &zero, sizeof(zero));
        close(fd);
    }
}

void save_probe(uint8_t slot, save_info_t *info)
{
    memset(info, 0, sizeof(*info));
    voyage_t tmp;
    int fd = open(slot_path(slot), O_RDONLY);
    if (fd < 0)
        return;
    int n = read(fd, &tmp, sizeof(tmp));
    close(fd);
    if (n != (int)sizeof(tmp) || tmp.magic != SAVE_MAGIC)
        return;
    info->valid = true;
    info->sector = tmp.sector;
    info->stardate = tmp.stardate;
    info->credits = tmp.credits;
    info->ach_count = popcount16(tmp.ach_bits);
}
