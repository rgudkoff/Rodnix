/**
 * @file devfs.h
 * @brief Device filesystem driver bootstrap interface
 */

#pragma once

int devfs_fs_init(void);
int devfs_register_blockdev(const char* name);
