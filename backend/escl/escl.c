/* sane - Scanner Access Now Easy.

   Copyright (C) 2019 Touboul Nathane
   Copyright (C) 2019 Thierry HUCHARD <thierry@ordissimo.com>

   This file is part of the SANE package.

   SANE is free software; you can redistribute it and/or modify it under
   the terms of the GNU General Public License as published by the Free
   Software Foundation; either version 3 of the License, or (at your
   option) any later version.

   SANE is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
   FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
   for more details.

   You should have received a copy of the GNU General Public License
   along with sane; see the file COPYING.  If not, write to the Free
   Software Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.

   This file implements a SANE backend for eSCL scanners.  */

#include "escl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <setjmp.h>

#include <curl/curl.h>

#include "../include/sane/saneopts.h"
#include "../include/sane/sanei.h"
#include "../include/sane/sanei_backend.h"
#include "../include/sane/sanei_config.h"

#define min(A,B) (((A)<(B)) ? (A) : (B))
#define max(A,B) (((A)>(B)) ? (A) : (B))
#define INPUT_BUFFER_SIZE 4096

static const SANE_Device **devlist = NULL;
static ESCL_Device *list_devices_primary = NULL;
static int num_devices = 0;

typedef struct Handled {
    struct Handled *next;
    SANE_String_Const name;
    char *result;
    ESCL_ScanParam param;
    SANE_Option_Descriptor opt[NUM_OPTIONS];
    Option_Value val[NUM_OPTIONS];
    capabilities_t *scanner;
    SANE_Range x_range;
    SANE_Range y_range;
    SANE_Bool cancel;
    SANE_Bool write_scan_data;
    SANE_Bool decompress_scan_data;
    SANE_Bool end_read;
    SANE_Parameters ps;
} escl_sane_t;

static ESCL_Device *
escl_free_device(ESCL_Device *current)
{
    if (!current) return NULL;
    free((void*)current->ip_address);
    free((void*)current->model_name);
    free((void*)current->type);
    free(current);
    return NULL;
}

static SANE_Status
escl_check_and_add_device(ESCL_Device *current)
{
    if(!current) {
      DBG (10, "ESCL_Device *current us null.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (!current->ip_address) {
      DBG (10, "Ip Address allocation failure.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (current->port_nb == 0) {
      DBG (10, "No port defined.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (!current->model_name) {
      DBG (10, "Modele Name allocation failure.\n");
      return (SANE_STATUS_NO_MEM);
    }
    if (!current->type) {
      DBG (10, "Scanner Type allocation failure.\n");
      return (SANE_STATUS_NO_MEM);
    }
    ++num_devices;
    current->next = list_devices_primary;
    list_devices_primary = current;
    return (SANE_STATUS_GOOD);
}

/**
 * \fn static SANE_Status escl_add_in_list(ESCL_Device *current)
 * \brief Function that adds all the element needed to my list :
 *        the port number, the model name, the ip address, and the type of url (http/https).
 *        Moreover, this function counts the number of devices found.
 *
 * \return SANE_STATUS_GOOD if everything is OK.
 */
static SANE_Status
escl_add_in_list(ESCL_Device *current)
{
    if(!current) {
      DBG (10, "ESCL_Device *current us null.\n");
      return (SANE_STATUS_NO_MEM);
    }

    if (SANE_STATUS_GOOD ==
        escl_check_and_add_device(current)) {
        list_devices_primary = current;
        return (SANE_STATUS_GOOD);
    }
    current = escl_free_device(current);
    return (SANE_STATUS_NO_MEM);
}

/**
 * \fn SANE_Status escl_device_add(int port_nb, const char *model_name, char *ip_address, char *type)
 * \brief Function that browses my list ('for' loop) and returns the "escl_add_in_list" function to
 *        adds all the element needed to my list :
 *        the port number, the model name, the ip address and the type of the url (http / https).
 *
 * \return escl_add_in_list(current)
 */
SANE_Status
escl_device_add(int port_nb, const char *model_name, char *ip_address, char *type)
{
    char tmp[PATH_MAX] = { 0 };
    char *model = NULL;
    ESCL_Device *current = NULL;
    DBG (10, "escl_device_add\n");
    for (current = list_devices_primary; current; current = current->next) {
        if (strcmp(current->ip_address, ip_address) == 0 && current->port_nb == port_nb
            && strcmp(current->type, type) == 0)
            return (SANE_STATUS_GOOD);
    }
    current = (ESCL_Device*)calloc(1, sizeof(*current));
    if (current == NULL) {
       DBG (10, "New device allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    current->port_nb = port_nb;

    if (strcmp(type, "_uscan._tcp") != 0 && strcmp(type, "http") != 0) {
        snprintf(tmp, sizeof(tmp), "%s SSL", model_name);
    }
    model = (char*)(tmp[0] != 0 ? tmp : model_name);
    current->model_name = strdup(model);
    current->ip_address = strdup(ip_address);
    current->type = strdup(type);
    return escl_add_in_list(current);
}

/**
 * \fn static inline size_t max_string_size(const SANE_String_Const strings[])
 * \brief Function that browses the string ('for' loop) and counts the number of character in the string.
 *        --> this allows to know the maximum size of the string.
 *
 * \return max_size + 1 (the size max)
 */
static inline size_t
max_string_size(const SANE_String_Const strings[])
{
    size_t max_size = 0;
    int i = 0;

    for (i = 0; strings[i]; ++i) {
        size_t size = strlen (strings[i]);
        if (size > max_size)
            max_size = size;
    }
    return (max_size + 1);
}

/**
 * \fn static SANE_Device *convertFromESCLDev(ESCL_Device *cdev)
 * \brief Function that checks if the url of the received scanner is secured or not (http / https).
 *        --> if the url is not secured, our own url will be composed like "http://'ip':'port'".
 *        --> else, our own url will be composed like "https://'ip':'port'".
 *        AND, it's in this function that we gather all the informations of the url (that were in our list) :
 *        the model_name, the port, the ip, and the type of url.
 *        SO, leaving this function, we have in memory the complete url.
 *
 * \return sdev (structure that contains the elements of the url)
 */
static SANE_Device *
convertFromESCLDev(ESCL_Device *cdev)
{
    char tmp[PATH_MAX] = { 0 };
    SANE_Device *sdev = (SANE_Device*) calloc(1, sizeof(SANE_Device));
    if (!sdev) {
       DBG (10, "Sane_Device allocation failure.\n");
       return NULL;
    }

    if (strcmp(cdev->type, "_uscan._tcp") == 0 || strcmp(cdev->type, "http") == 0)
        snprintf(tmp, sizeof(tmp), "http://%s:%d", cdev->ip_address, cdev->port_nb);
    else
        snprintf(tmp, sizeof(tmp), "https://%s:%d", cdev->ip_address, cdev->port_nb);
    DBG( 1, "Escl add device : %s\n", tmp);
    sdev->name = strdup(tmp);
    if (!sdev->name) {
       DBG (10, "Name allocation failure.\n");
       goto freedev;
    }
    sdev->model = strdup(cdev->model_name);
    if (!sdev->model) {
       DBG (10, "Model allocation failure.\n");
       goto freename;
    }
    sdev->vendor = strdup("ESCL");
    if (!sdev->vendor) {
       DBG (10, "Vendor allocation failure.\n");
       goto freemodel;
    }
    sdev->type = strdup("flatbed scanner");
    if (!sdev->type) {
       DBG (10, "Scanner Type allocation failure.\n");
       goto freevendor;
    }
    return (sdev);
freevendor:
    free((void*)sdev->vendor);
freemodel:
    free((void*)sdev->model);
freename:
    free((void*)sdev->name);
freedev:
    free((void*)sdev);
    return NULL;
}

/**
 * \fn SANE_Status sane_init(SANE_Int *version_code, SANE_Auth_Callback authorize)
 * \brief Function that's called before any other SANE function ; it's the first SANE function called.
 *        --> this function checks the SANE config. and can check the authentication of the user if
 *        'authorize' value is more than SANE_TRUE.
 *        In this case, it will be necessary to define an authentication method.
 *
 * \return SANE_STATUS_GOOD (everything is OK)
 */
SANE_Status
sane_init(SANE_Int *version_code, SANE_Auth_Callback __sane_unused__ authorize)
{
    DBG_INIT();
    DBG (10, "escl sane_init\n");
    SANE_Status status = SANE_STATUS_GOOD;
    curl_global_init(CURL_GLOBAL_ALL);
    if (version_code != NULL)
        *version_code = SANE_VERSION_CODE(1, 0, 0);
    if (status != SANE_STATUS_GOOD)
        return (status);
    return (SANE_STATUS_GOOD);
}

/**
 * \fn void sane_exit(void)
 * \brief Function that must be called to terminate use of a backend.
 *        This function will first close all device handles that still might be open.
 *        --> by freeing all the elements of my list.
 *        After this function, no function other than 'sane_init' may be called.
 */
void
sane_exit(void)
{
    DBG (10, "escl sane_exit\n");
    ESCL_Device *next = NULL;

    while (list_devices_primary != NULL) {
        next = list_devices_primary->next;
        free(list_devices_primary);
        list_devices_primary = next;
    }
    if (devlist)
        free (devlist);
    list_devices_primary = NULL;
    devlist = NULL;
    curl_global_cleanup();
}

/**
 * \fn static SANE_Status attach_one_config(SANEI_Config *config, const char *line)
 * \brief Function that implements a configuration file to the user :
 *        if the user can't detect some devices, he will be able to force their detection with this config' file to use them.
 *        Thus, this function parses the config' file to use the device of the user with the information below :
 *        the type of protocol (http/https), the ip, the port number, and the model name.
 *
 * \return escl_add_in_list(escl_device) if the parsing worked, SANE_STATUS_GOOD otherwise.
 */
static SANE_Status
attach_one_config(SANEI_Config __sane_unused__ *config, const char *line)
{
    int port = 0;
    SANE_Status status;
    static ESCL_Device *escl_device = NULL;

    if (strncmp(line, "[device]", 8) == 0) {
        escl_device = escl_free_device(escl_device);
        escl_device = (ESCL_Device*)calloc(1, sizeof(ESCL_Device));
        if (!escl_device) {
           DBG (10, "New Escl_Device allocation failure.");
           return (SANE_STATUS_NO_MEM);
        }
    }
    if (strncmp(line, "ip", 2) == 0) {
        const char *ip_space = sanei_config_skip_whitespace(line + 2);
        DBG (10, "New Escl_Device IP [%s].", (ip_space ? ip_space : "VIDE"));
        if (escl_device != NULL && ip_space != NULL) {
            DBG (10, "New Escl_Device IP Affected.");
            escl_device->ip_address = strdup(ip_space);
        }
    }
    if (sscanf(line, "port %i", &port) == 1 && port != 0) {
        DBG (10, "New Escl_Device PORT [%d].", port);
        if (escl_device != NULL) {
            DBG (10, "New Escl_Device PORT Affected.");
            escl_device->port_nb = port;
        }
    }
    if (strncmp(line, "model", 5) == 0) {
        const char *model_space = sanei_config_skip_whitespace(line + 5);
        DBG (10, "New Escl_Device MODEL [%s].", (model_space ? model_space : "VIDE"));
        if (escl_device != NULL && model_space != NULL) {
            DBG (10, "New Escl_Device MODEL Affected.");
            escl_device->model_name = strdup(model_space);
        }
    }
    if (strncmp(line, "type", 4) == 0) {
        const char *type_space = sanei_config_skip_whitespace(line + 4);
        DBG (10, "New Escl_Device TYPE [%s].", (type_space ? type_space : "VIDE"));
        if (escl_device != NULL && type_space != NULL) {
            DBG (10, "New Escl_Device TYPE Affected.");
            escl_device->type = strdup(type_space);
        }
    }
    status = escl_check_and_add_device(escl_device);
    if (status == SANE_STATUS_GOOD)
       escl_device = NULL;
    return status;
}

/**
 * \fn SANE_Status sane_get_devices(const SANE_Device ***device_list, SANE_Bool local_only)
 * \brief Function that searches for connected devices and places them in our 'device_list'. ('for' loop)
 *        If the attribute 'local_only' is worth SANE_FALSE, we only returns the connected devices locally.
 *
 * \return SANE_STATUS_GOOD if devlist != NULL ; SANE_STATUS_NO_MEM otherwise.
 */
SANE_Status
sane_get_devices(const SANE_Device ***device_list, SANE_Bool local_only)
{
    if (local_only)             /* eSCL is a network-only protocol */
        return (device_list ? SANE_STATUS_GOOD : SANE_STATUS_INVAL);

    DBG (10, "escl sane_get_devices\n");
    ESCL_Device *dev = NULL;
    static const SANE_Device **devlist = 0;
    SANE_Status status;

    if (device_list == NULL)
        return (SANE_STATUS_INVAL);
    status = sanei_configure_attach(ESCL_CONFIG_FILE, NULL, attach_one_config);
    if (status != SANE_STATUS_GOOD)
        return (status);
    escl_devices(&status);
    if (status != SANE_STATUS_GOOD)
        return (status);
    if (devlist)
        free(devlist);
    devlist = (const SANE_Device **) calloc (num_devices + 1, sizeof (devlist[0]));
    if (devlist == NULL)
        return (SANE_STATUS_NO_MEM);
    int i = 0;
    for (dev = list_devices_primary; i < num_devices; dev = dev->next) {
        SANE_Device *s_dev = convertFromESCLDev(dev);
        devlist[i] = s_dev;
        i++;
    }
    devlist[i] = 0;
    *device_list = devlist;
    return (devlist) ? SANE_STATUS_GOOD : SANE_STATUS_NO_MEM;
}

/**
 * \fn static SANE_Status init_options(SANE_String_Const name, escl_sane_t *s)
 * \brief Function thzt initializes all the needed options of the received scanner
 *        (the resolution / the color / the margins) thanks to the informations received with
 *        the 'escl_capabilities' function, called just before.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD)
 */
static SANE_Status
init_options(SANE_String_Const name, escl_sane_t *s)
{
    DBG (10, "escl init_options\n");
    SANE_Status status = SANE_STATUS_GOOD;
    int i = 0;

    if (name == NULL)
        return (SANE_STATUS_INVAL);
    memset (s->opt, 0, sizeof (s->opt));
    memset (s->val, 0, sizeof (s->val));
    for (i = 0; i < NUM_OPTIONS; ++i) {
        s->opt[i].size = sizeof (SANE_Word);
        s->opt[i].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    }
    s->x_range.min = PIXEL_TO_MM(s->scanner->MinWidth, 300.0);
    s->x_range.max = PIXEL_TO_MM(s->scanner->MaxWidth, 300.0);
    s->x_range.quant = 0;
    s->y_range.min = PIXEL_TO_MM(s->scanner->MinHeight, 300.0);
    s->y_range.max = PIXEL_TO_MM(s->scanner->MaxHeight, 300.0);
    s->y_range.quant = 0;
    s->opt[OPT_NUM_OPTS].title = SANE_TITLE_NUM_OPTIONS;
    s->opt[OPT_NUM_OPTS].desc = SANE_DESC_NUM_OPTIONS;
    s->opt[OPT_NUM_OPTS].type = SANE_TYPE_INT;
    s->opt[OPT_NUM_OPTS].cap = SANE_CAP_SOFT_DETECT;
    s->val[OPT_NUM_OPTS].w = NUM_OPTIONS;

    s->opt[OPT_MODE_GROUP].title = SANE_TITLE_SCAN_MODE;
    s->opt[OPT_MODE_GROUP].desc = "";
    s->opt[OPT_MODE_GROUP].type = SANE_TYPE_GROUP;
    s->opt[OPT_MODE_GROUP].cap = 0;
    s->opt[OPT_MODE_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

    s->opt[OPT_MODE].name = SANE_NAME_SCAN_MODE;
    s->opt[OPT_MODE].title = SANE_TITLE_SCAN_MODE;
    s->opt[OPT_MODE].desc = SANE_DESC_SCAN_MODE;
    s->opt[OPT_MODE].type = SANE_TYPE_STRING;
    s->opt[OPT_MODE].unit = SANE_UNIT_NONE;
    s->opt[OPT_MODE].constraint_type = SANE_CONSTRAINT_STRING_LIST;
    s->opt[OPT_MODE].constraint.string_list = s->scanner->ColorModes;
    s->val[OPT_MODE].s = (char *)strdup(s->scanner->ColorModes[0]);
    if (!s->val[OPT_MODE].s) {
       DBG (10, "Color Mode Default allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    s->opt[OPT_MODE].size = max_string_size(s->scanner->ColorModes);
    s->scanner->default_color = (char *)strdup(s->scanner->ColorModes[0]);
    if (!s->scanner->default_color) {
       DBG (10, "Color Mode Default allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }

    s->opt[OPT_RESOLUTION].name = SANE_NAME_SCAN_RESOLUTION;
    s->opt[OPT_RESOLUTION].title = SANE_TITLE_SCAN_RESOLUTION;
    s->opt[OPT_RESOLUTION].desc = SANE_DESC_SCAN_RESOLUTION;
    s->opt[OPT_RESOLUTION].type = SANE_TYPE_INT;
    s->opt[OPT_RESOLUTION].unit = SANE_UNIT_DPI;
    s->opt[OPT_RESOLUTION].constraint_type = SANE_CONSTRAINT_WORD_LIST;
    s->opt[OPT_RESOLUTION].constraint.word_list = s->scanner->SupportedResolutions;
    s->val[OPT_RESOLUTION].w = s->scanner->SupportedResolutions[1];
    s->scanner->default_resolution = s->scanner->SupportedResolutions[1];

    s->opt[OPT_PREVIEW].name = SANE_NAME_PREVIEW;
    s->opt[OPT_PREVIEW].title = SANE_TITLE_PREVIEW;
    s->opt[OPT_PREVIEW].desc = SANE_DESC_PREVIEW;
    s->opt[OPT_PREVIEW].cap = SANE_CAP_SOFT_DETECT | SANE_CAP_SOFT_SELECT;
    s->opt[OPT_PREVIEW].type = SANE_TYPE_BOOL;
    s->val[OPT_PREVIEW].w = SANE_FALSE;

    s->opt[OPT_GRAY_PREVIEW].name = SANE_NAME_GRAY_PREVIEW;
    s->opt[OPT_GRAY_PREVIEW].title = SANE_TITLE_GRAY_PREVIEW;
    s->opt[OPT_GRAY_PREVIEW].desc = SANE_DESC_GRAY_PREVIEW;
    s->opt[OPT_GRAY_PREVIEW].type = SANE_TYPE_BOOL;
    s->val[OPT_GRAY_PREVIEW].w = SANE_FALSE;

    s->opt[OPT_GEOMETRY_GROUP].title = SANE_TITLE_GEOMETRY;
    s->opt[OPT_GEOMETRY_GROUP].desc = SANE_DESC_GEOMETRY;
    s->opt[OPT_GEOMETRY_GROUP].type = SANE_TYPE_GROUP;
    s->opt[OPT_GEOMETRY_GROUP].cap = SANE_CAP_ADVANCED;
    s->opt[OPT_GEOMETRY_GROUP].constraint_type = SANE_CONSTRAINT_NONE;

    s->opt[OPT_TL_X].name = SANE_NAME_SCAN_TL_X;
    s->opt[OPT_TL_X].title = SANE_TITLE_SCAN_TL_X;
    s->opt[OPT_TL_X].desc = SANE_DESC_SCAN_TL_X;
    s->opt[OPT_TL_X].type = SANE_TYPE_FIXED;
    s->opt[OPT_TL_X].size = sizeof(SANE_Fixed);
    s->opt[OPT_TL_X].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_TL_X].unit = SANE_UNIT_MM;
    s->opt[OPT_TL_X].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_TL_X].constraint.range = &s->x_range;
    s->val[OPT_TL_X].w = 0;

    s->opt[OPT_TL_Y].name = SANE_NAME_SCAN_TL_Y;
    s->opt[OPT_TL_Y].title = SANE_TITLE_SCAN_TL_Y;
    s->opt[OPT_TL_Y].desc = SANE_DESC_SCAN_TL_Y;
    s->opt[OPT_TL_Y].type = SANE_TYPE_FIXED;
    s->opt[OPT_TL_Y].size = sizeof(SANE_Fixed);
    s->opt[OPT_TL_Y].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_TL_Y].unit = SANE_UNIT_MM;
    s->opt[OPT_TL_Y].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_TL_Y].constraint.range = &s->y_range;
    s->val[OPT_TL_Y].w = 0;

    s->opt[OPT_BR_X].name = SANE_NAME_SCAN_BR_X;
    s->opt[OPT_BR_X].title = SANE_TITLE_SCAN_BR_X;
    s->opt[OPT_BR_X].desc = SANE_DESC_SCAN_BR_X;
    s->opt[OPT_BR_X].type = SANE_TYPE_FIXED;
    s->opt[OPT_BR_X].size = sizeof(SANE_Fixed);
    s->opt[OPT_BR_X].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_BR_X].unit = SANE_UNIT_MM;
    s->opt[OPT_BR_X].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_BR_X].constraint.range = &s->x_range;
    s->val[OPT_BR_X].w = s->x_range.max;

    s->opt[OPT_BR_Y].name = SANE_NAME_SCAN_BR_Y;
    s->opt[OPT_BR_Y].title = SANE_TITLE_SCAN_BR_Y;
    s->opt[OPT_BR_Y].desc = SANE_DESC_SCAN_BR_Y;
    s->opt[OPT_BR_Y].type = SANE_TYPE_FIXED;
    s->opt[OPT_BR_Y].size = sizeof(SANE_Fixed);
    s->opt[OPT_BR_Y].cap = SANE_CAP_SOFT_SELECT | SANE_CAP_SOFT_DETECT;
    s->opt[OPT_BR_Y].unit = SANE_UNIT_MM;
    s->opt[OPT_BR_Y].constraint_type = SANE_CONSTRAINT_RANGE;
    s->opt[OPT_BR_Y].constraint.range = &s->y_range;
    s->val[OPT_BR_Y].w = s->y_range.max;
    return (status);
}

/**
 * \fn SANE_Status sane_open(SANE_String_Const name, SANE_Handle *h)
 * \brief Function that establishes a connection with the device named by 'name',
 *        and returns a 'handler' using 'SANE_Handle *h', representing it.
 *        Thus, it's this function that calls the 'escl_status' function firstly,
 *        then the 'escl_capabilities' function, and, after, the 'init_options' function.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_open(SANE_String_Const name, SANE_Handle *h)
{
    DBG (10, "escl sane_open\n");
    SANE_Status status;
    escl_sane_t *handler = NULL;

    if (name == NULL)
        return (SANE_STATUS_INVAL);
    status = escl_status(name);
    if (status != SANE_STATUS_GOOD)
        return (status);
    handler = (escl_sane_t *)calloc(1, sizeof(escl_sane_t));
    if (handler == NULL)
        return (SANE_STATUS_NO_MEM);
    handler->name = strdup(name);
    if (!handler->name) {
       DBG (10, "Handle Name allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    handler->scanner = escl_capabilities(name, &status);
    if (status != SANE_STATUS_GOOD)
        return (status);
    status = init_options(name, handler);
    if (status != SANE_STATUS_GOOD)
        return (status);
    handler->ps.depth = 8;
    handler->ps.last_frame = SANE_TRUE;
    handler->ps.format = SANE_FRAME_RGB;
    handler->ps.pixels_per_line = MM_TO_PIXEL(handler->val[OPT_BR_X].w, 300.0);
    handler->ps.lines = MM_TO_PIXEL(handler->val[OPT_BR_Y].w, 300.0);
    handler->ps.bytes_per_line = handler->ps.pixels_per_line * 3;
    status = sane_get_parameters(handler, 0);
    if (status != SANE_STATUS_GOOD)
        return (status);
    handler->cancel = SANE_FALSE;
    handler->write_scan_data = SANE_FALSE;
    handler->decompress_scan_data = SANE_FALSE;
    handler->end_read = SANE_FALSE;
    *h = handler;
    return (status);
}

/**
 * \fn void sane_cancel(SANE_Handle h)
 * \brief Function that's used to, immediately or as quickly as possible, cancel the currently
 *        pending operation of the device represented by 'SANE_Handle h'.
 *        This functions calls the 'escl_scanner' functions, that resets the scan operations.
 */
void
sane_cancel(SANE_Handle h)
{
    DBG (10, "escl sane_cancel\n");
    escl_sane_t *handler = h;
    if (handler->scanner->tmp)
    {
      fclose(handler->scanner->tmp);
      handler->scanner->tmp = NULL;
    }
    handler->cancel = SANE_TRUE;
    escl_scanner(handler->name, handler->result);
}

/**
 * \fn void sane_close(SANE_Handle h)
 * \brief Function that closes the communication with the device represented by 'SANE_Handle h'.
 *        This function must release the resources that were allocated to the opening of 'h'.
 */
void
sane_close(SANE_Handle h)
{
    DBG (10, "escl sane_close\n");
    if (h != NULL) {
        free(h);
        h = NULL;
    }
}

/**
 * \fn const SANE_Option_Descriptor *sane_get_option_descriptor(SANE_Handle h, SANE_Int n)
 * \brief Function that retrieves a descriptor from the n number option of the scanner
 *        represented by 'h'.
 *        The descriptor remains valid until the machine is closed.
 *
 * \return s->opt + n
 */
const SANE_Option_Descriptor *
sane_get_option_descriptor(SANE_Handle h, SANE_Int n)
{
    DBG (10, "escl sane_get_option_descriptor\n");
    escl_sane_t *s = h;

    if ((unsigned) n >= NUM_OPTIONS || n < 0)
        return (0);
    return (&s->opt[n]);
}

/**
 * \fn SANE_Status sane_control_option(SANE_Handle h, SANE_Int n, SANE_Action a, void *v, SANE_Int *i)
 * \brief Function that defines the actions to perform for the 'n' option of the machine,
 *        represented by 'h', if the action is 'a'.
 *        There are 3 types of possible actions :
 *        --> SANE_ACTION_GET_VALUE: 'v' must be used to provide the value of the option.
 *        --> SANE_ACTION_SET_VALUE: The option must take the 'v' value.
 *        --> SANE_ACTION_SET_AUTO: The backend or machine must affect the option with an appropriate value.
 *        Moreover, the parameter 'i' is used to provide additional information about the state of
 *        'n' option if SANE_ACTION_SET_VALUE has been performed.
 *
 * \return SANE_STATUS_GOOD if everything is OK, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL
 */
SANE_Status
sane_control_option(SANE_Handle h, SANE_Int n, SANE_Action a, void *v, SANE_Int *i)
{
    DBG (10, "escl sane_control_option\n");
    escl_sane_t *handler = h;

    if (i)
        *i = 0;
    if (n >= NUM_OPTIONS || n < 0)
        return (SANE_STATUS_INVAL);
    if (a == SANE_ACTION_GET_VALUE) {
        switch (n) {
        case OPT_TL_X:
        case OPT_TL_Y:
        case OPT_BR_X:
        case OPT_BR_Y:
        case OPT_NUM_OPTS:
        case OPT_RESOLUTION:
        case OPT_PREVIEW:
        case OPT_GRAY_PREVIEW:
            *(SANE_Word *) v = handler->val[n].w;
            break;
        case OPT_MODE:
            strcpy (v, handler->val[n].s);
            break;
        case OPT_MODE_GROUP:
        default:
            break;
        }
        return (SANE_STATUS_GOOD);
    }
    if (a == SANE_ACTION_SET_VALUE) {
        switch (n) {
        case OPT_TL_X:
        case OPT_TL_Y:
        case OPT_BR_X:
        case OPT_BR_Y:
        case OPT_NUM_OPTS:
        case OPT_RESOLUTION:
        case OPT_PREVIEW:
        case OPT_GRAY_PREVIEW:
            handler->val[n].w = *(SANE_Word *) v;
            if (i)
                *i |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS | SANE_INFO_INEXACT;
            break;
        case OPT_MODE:
            if (handler->val[n].s)
                free (handler->val[n].s);
            handler->val[n].s = strdup (v);
            if (!handler->val[n].s) {
              DBG (10, "OPT_MODE allocation failure.\n");
              return (SANE_STATUS_NO_MEM);
            }
            if (i)
                *i |= SANE_INFO_RELOAD_PARAMS | SANE_INFO_RELOAD_OPTIONS | SANE_INFO_INEXACT;
            break;
        default:
            break;
        }
    }
    return (SANE_STATUS_GOOD);
}

/**
 * \fn SANE_Status sane_start(SANE_Handle h)
 * \brief Function that initiates aquisition of an image from the device represented by handle 'h'.
 *        This function calls the "escl_newjob" function and the "escl_scan" function.
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_start(SANE_Handle h)
{
    DBG (10, "escl sane_start\n");
    SANE_Status status = SANE_STATUS_GOOD;
    escl_sane_t *handler = h;
    int w = 0;
    int he = 0;
    int bps = 0;

    if (handler->name == NULL)
        return (SANE_STATUS_INVAL);
    handler->cancel = SANE_FALSE;
    handler->write_scan_data = SANE_FALSE;
    handler->decompress_scan_data = SANE_FALSE;
    handler->end_read = SANE_FALSE;
    if(handler->scanner->default_color)
       free(handler->scanner->default_color);
    if (handler->val[OPT_PREVIEW].w == SANE_TRUE)
    {
       int i = 0, val = 9999;;
       if (handler->val[OPT_GRAY_PREVIEW].w == SANE_TRUE ||
           !strcasecmp(handler->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY))
          handler->scanner->default_color = strdup("Grayscale8");
       else
          handler->scanner->default_color = strdup("RGB24");
       if (!handler->scanner->default_color) {
          DBG (10, "Default Color allocation failure.\n");
          return (SANE_STATUS_NO_MEM);
       }
       for (i = 1; i < handler->scanner->SupportedResolutionsSize; i++)
       {
          if (val > handler->scanner->SupportedResolutions[i])
              val = handler->scanner->SupportedResolutions[i];
       }
       handler->scanner->default_resolution = val;
    }
    else
    {
    handler->scanner->default_resolution = handler->val[OPT_RESOLUTION].w;
    if (!strcasecmp(handler->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_GRAY))
       handler->scanner->default_color = strdup("Grayscale8");
    else if (!strcasecmp(handler->val[OPT_MODE].s, SANE_VALUE_SCAN_MODE_LINEART))
       handler->scanner->default_color = strdup("BlackAndWhite1");
    else
       handler->scanner->default_color = strdup("RGB24");
    }
    handler->scanner->height = MM_TO_PIXEL(handler->val[OPT_BR_Y].w, 300.0);
    handler->scanner->width = MM_TO_PIXEL(handler->val[OPT_BR_X].w, 300.0);
    handler->scanner->pos_x = MM_TO_PIXEL(handler->val[OPT_TL_X].w, 300.0);
    handler->scanner->pos_y = MM_TO_PIXEL(handler->val[OPT_TL_Y].w, 300.0);
    DBG(10, "Calculate Size Image [%dx%d|%dx%d]\n",
             handler->scanner->pos_x,
             handler->scanner->pos_y,
             handler->scanner->height,
             handler->scanner->width);
    if (!handler->scanner->default_color) {
       DBG (10, "Default Color allocation failure.\n");
       return (SANE_STATUS_NO_MEM);
    }
    handler->result = escl_newjob(handler->scanner, handler->name, &status);
    if (status != SANE_STATUS_GOOD)
        return (status);
    status = escl_scan(handler->scanner, handler->name, handler->result);
    if (status != SANE_STATUS_GOOD)
        return (status);
    if (!strcmp(handler->scanner->default_format, "image/jpeg"))
    {
       status = get_JPEG_data(handler->scanner, &w, &he, &bps);
    }
    else if (!strcmp(handler->scanner->default_format, "image/png"))
    {
       status = get_PNG_data(handler->scanner, &w, &he, &bps);
    }
    else if (!strcmp(handler->scanner->default_format, "image/tiff"))
    {
       status = get_TIFF_data(handler->scanner, &w, &he, &bps);
    }
    else if (!strcmp(handler->scanner->default_format, "application/pdf"))
    {
       status = get_PDF_data(handler->scanner, &w, &he, &bps);
    }
    else {
      DBG(10, "Unknow image format\n");
      return SANE_STATUS_INVAL;
    }

    DBG(10, "2-Size Image [%dx%d|%dx%d]\n", 0, 0, w, he);
    if (status != SANE_STATUS_GOOD)
        return (status);
    handler->ps.depth = 8;
    handler->ps.pixels_per_line = w;
    handler->ps.lines = he;
    handler->ps.bytes_per_line = w * bps;
    handler->ps.last_frame = SANE_TRUE;
    handler->ps.format = SANE_FRAME_RGB;
    DBG(10, "Real Size Image [%dx%d|%dx%d]\n", 0, 0, w, he);
    return (status);
}

/**
 * \fn SANE_Status sane_get_parameters(SANE_Handle h, SANE_Parameters *p)
 * \brief Function that retrieves the device parameters represented by 'h' and stores them in 'p'.
 *        This function is normally used after "sane_start".
 *        It's in this function that we choose to assign the default color. (Color or Monochrome)
 *
 * \return status (if everything is OK, status = SANE_STATUS_GOOD, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_get_parameters(SANE_Handle h, SANE_Parameters *p)
{
    DBG (10, "escl sane_get_parameters\n");
    SANE_Status status = SANE_STATUS_GOOD;
    escl_sane_t *handler = h;

    if (status != SANE_STATUS_GOOD)
        return (status);
    if (p != NULL) {
        p->depth = 8;
        p->last_frame = SANE_TRUE;
        p->format = SANE_FRAME_RGB;
        p->pixels_per_line = handler->ps.pixels_per_line;
        p->lines = handler->ps.lines;
        p->bytes_per_line = handler->ps.bytes_per_line;
    }
    return (status);
}


/**
 * \fn SANE_Status sane_read(SANE_Handle h, SANE_Byte *buf, SANE_Int maxlen, SANE_Int *len)
 * \brief Function that's used to read image data from the device represented by handle 'h'.
 *        The argument 'buf' is a pointer to a memory area that is at least 'maxlen' bytes long.
 *        The number of bytes returned is stored in '*len'.
 *        --> When the call succeeds, the number of bytes returned can be anywhere in the range from 0 to 'maxlen' bytes.
 *
 * \return SANE_STATUS_GOOD (if everything is OK, otherwise, SANE_STATUS_NO_MEM/SANE_STATUS_INVAL)
 */
SANE_Status
sane_read(SANE_Handle h, SANE_Byte *buf, SANE_Int maxlen, SANE_Int *len)
{
    DBG (10, "escl sane_read\n");
    escl_sane_t *handler = h;
    SANE_Status status = SANE_STATUS_GOOD;
    long readbyte;

    if (!handler | !buf | !len)
        return (SANE_STATUS_INVAL);
    if (handler->cancel)
        return (SANE_STATUS_CANCELLED);
    if (!handler->write_scan_data)
        handler->write_scan_data = SANE_TRUE;
    if (!handler->decompress_scan_data) {
        if (status != SANE_STATUS_GOOD)
            return (status);
        handler->decompress_scan_data = SANE_TRUE;
    }
    if (handler->scanner->img_data == NULL)
        return (SANE_STATUS_INVAL);
    if (!handler->end_read) {
        readbyte = min((handler->scanner->img_size - handler->scanner->img_read), maxlen);
        memcpy(buf, handler->scanner->img_data + handler->scanner->img_read, readbyte);
        handler->scanner->img_read = handler->scanner->img_read + readbyte;
        *len = readbyte;
        if (handler->scanner->img_read == handler->scanner->img_size)
            handler->end_read = SANE_TRUE;
        else if (handler->scanner->img_read > handler->scanner->img_size) {
            *len = 0;
            handler->end_read = SANE_TRUE;
            free(handler->scanner->img_data);
            handler->scanner->img_data = NULL;
            return (SANE_STATUS_INVAL);
        }
    }
    else {
        *len = 0;
        free(handler->scanner->img_data);
        handler->scanner->img_data = NULL;
        return (SANE_STATUS_EOF);
    }
    return (SANE_STATUS_GOOD);
}

SANE_Status
sane_get_select_fd(SANE_Handle __sane_unused__ h, SANE_Int __sane_unused__ *fd)
{
    return (SANE_STATUS_UNSUPPORTED);
}

SANE_Status
sane_set_io_mode(SANE_Handle __sane_unused__ handle, SANE_Bool __sane_unused__ non_blocking)
{
    return (SANE_STATUS_UNSUPPORTED);
}
