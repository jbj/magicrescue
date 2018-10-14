/*
 * Magic Rescue directory recursion functions
 * Copyright (C) 2004 Jonas Jensen <jbj@knef.dk>
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */
#include "config.h"

#include <sys/types.h>
#include <sys/stat.h>

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "recur.h"
#include "util.h"

void rm_rf(const char *dir)
{
    struct dirent *dirent;
    struct stat st;
    DIR *dh = opendir(dir);
    char fullname[PATH_MAX];

    if (!dh)
	return;

    while ((dirent = readdir(dh))) {
	if (   strcmp(dirent->d_name, "." ) == 0
	    || strcmp(dirent->d_name, "..") == 0
	    || snprintf(fullname, PATH_MAX, "%s/%s",
		    dir, dirent->d_name) >= PATH_MAX
	    || lstat(fullname, &st) != 0) {

	    continue;
	}

	if (S_ISDIR(st.st_mode))
	    rm_rf(fullname);
	else
	    unlink(fullname);
    }
    closedir(dh);
    rmdir(dir);
}

static void parentdir(char *s)
{
    char *p;
    
    for (p = &s[strlen(s) - 1]; p >= s; p--) {
	if (*p == '/') {
	    *p = '\0';
	    return;
	}
    }
}

struct dirstack *dirstack_open(const char *path)
{
    struct dirstack *stack;
    DIR *dh = opendir(path);

    if (!dh)
	return NULL;

    stack = malloc(sizeof(struct dirstack));
    stack->dirs[0] = dh;
    stack->pos = stack->dirs;

    strcpy(stack->prefix, path);
    {
	char *s = &stack->prefix[strlen(stack->prefix)-1];
	if (*s == '/')
	    *s = '\0';
    }

    return stack;
}

void dirstack_close(struct dirstack *stack)
{
    free(stack);
}

int dirstack_next(struct dirstack *stack, char *fullname, struct stat *st_arg)
{
    struct dirent *dirent;
    struct stat st;

    if (stack == NULL)
	return -1;

    do {
	while ((dirent = readdir(*stack->pos)) == NULL) {
	    closedir(*stack->pos);

	    if (stack->pos == stack->dirs) {
		return -1;
	    }
	    --stack->pos;
	    parentdir(stack->prefix);
	}

    } while (  strcmp(dirent->d_name, "." ) == 0
	    || strcmp(dirent->d_name, "..") == 0
	    || snprintf(fullname, PATH_MAX, "%s/%s",
		    stack->prefix, dirent->d_name) >= PATH_MAX
	    || lstat(fullname, &st) != 0);

    if (S_ISDIR(st.st_mode) && 
	    stack->pos - stack->dirs + 1 < RECUR_MAXDEPTH &&
	    (stack->pos[1] = opendir(fullname))) {

	++stack->pos;
	strcpy(stack->prefix, fullname);
    }
    
    if (st_arg)
	*st_arg = st;

    return 0;
}

struct recur *recur_open(char **paths)
{
    struct recur *recur;
    
    if (paths == NULL || paths[0] == NULL || strlen(paths[0]) >= PATH_MAX)
	return NULL;
    
    recur = malloc(sizeof(struct recur));
    recur->list = paths;
    recur->stack = NULL;

    return recur;
}

void recur_close(struct recur *recur)
{
    free(recur->stack);
    free(recur);
}

int recur_next(struct recur *recur, char *name, struct stat *st_arg)
{
    int rv = -1;

    while (recur->stack == NULL
	    || (rv = dirstack_next(recur->stack, name, st_arg)) != 0) {
	const char *cur = recur->list[0];
	recur->list++;

	if (!cur)
	    return -1;
	
	dirstack_close(recur->stack);
	
	recur->stack = dirstack_open(cur);
	if (!recur->stack) {
	    struct stat st;
	    if (lstat(cur, &st) == 0) {

		strcpy(name, cur);
		if (st_arg)
		    *st_arg = st;
		return 0;

	    } else {
		fprintf(stderr, "%s: %s\n", cur, strerror(errno));
	    }
	}
    }

    return rv;
}

