// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2020 Vlad Adumitroaie <celtare21@gmail.com>
 * 2020 Pal Zoltan Illes @tbalden at github
 */

extern struct selinux_state *get_extern_state(void);
extern struct selinux_state *extern_state;
extern bool is_decrypted;

extern int get_enforce_value(void);
extern void set_selinux(int value);
