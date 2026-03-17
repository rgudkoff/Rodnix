#pragma once
/**
 * @file mbr_part.h
 * @brief MBR partition table scanner — registers diskNpM virtual block devices.
 */

#include "../../../kernel/fabric/service/block_service.h"

/*
 * Scan the MBR of `parent` for partition entries.
 * For each valid entry, register a virtual block device named
 * "<parent->name>p1".."<parent->name>p4".
 * Returns the number of partitions registered (0 = raw disk / no MBR).
 */
int mbr_scan_and_register(fabric_blockdev_t* parent);
