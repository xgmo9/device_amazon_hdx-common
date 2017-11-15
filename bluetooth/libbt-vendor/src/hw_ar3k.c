/*
 *
 *  Copyright (c) 2013, The Linux Foundation. All rights reserved.
 *  Not a Contribution.
 *
 *  Copyright 2017 The Android Open Source Project
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you
 *  may not use this file except in compliance with the License. You may
 *  obtain a copy of the License at
 *
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or
 *  implied. See the License for the specific language governing
 *  permissions and limitations under the License.
 *
 */

/******************************************************************************
 *
 *  Filename:      hw_ar3k.c
 *
 *  Description:   Contains controller-specific functions, like
 *                      firmware patch download
 *                      low power mode operations
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <sys/socket.h>
#include <utils/Log.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <signal.h>
#include <time.h>
#include <errno.h>
#include <fcntl.h>
#include <dirent.h>
#include <ctype.h>
#include <cutils/properties.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>

#include "bt_hci_bdroid.h"
#include "bt_vendor_qcom.h"
#include "hci_uart.h"
#include "hw_ar3k.h"

extern bt_vendor_callbacks_t *bt_vendor_cbacks;

/* for friendly debugging outpout string */
static char *lpm_mode[] = {
	"UNKNOWN",
	"disabled",
	"enabled"
};

static char *lpm_state[] = {
	"UNKNOWN",
	"de-asserted",
	"asserted"
};

static uint8_t upio_state[UPIO_MAX_COUNT];
struct ps_cfg_entry ps_list[MAX_TAGS];

#if (HW_NEED_END_WITH_HCI_RESET == TRUE)
/*******************************************************************************
**
** Function         hw_epilog_cback
**
** Description      Callback function for Command Complete Events from HCI
**                  commands sent in epilog process.
**
** Returns          None
**
*******************************************************************************/
void hw_epilog_cback(void *p_mem)
{
	HC_BT_HDR *p_evt_buf = (HC_BT_HDR *) p_mem;
	char        *p_name, *p_tmp;
	uint8_t     *p, status;
	uint16_t    opcode;

	status = *((uint8_t *)(p_evt_buf + 1) + HCI_EVT_CMD_CMPL_STATUS_RET_BYTE);
	p = (uint8_t *)(p_evt_buf + 1) + HCI_EVT_CMD_CMPL_OPCODE;
	STREAM_TO_UINT16(opcode,p);

	ALOGI("%s Opcode:0x%04X Status: %d", __FUNCTION__, opcode, status);

	if (bt_vendor_cbacks) {
		/* Must free the RX event buffer */
		bt_vendor_cbacks->dealloc(p_evt_buf);

		/* Once epilog process is done, must call callback to notify caller */
		bt_vendor_cbacks->epilog_cb(BT_VND_OP_RESULT_SUCCESS);
	}
}

/*******************************************************************************
**
** Function         hw_epilog_process
**
** Description      Sample implementation of epilog process
**
** Returns          None
**
*******************************************************************************/
void hw_epilog_process(void)
{
	HC_BT_HDR  *p_buf = NULL;
	uint8_t     *p;

	ALOGI("hw_epilog_process");

	/* Sending a HCI_RESET */
	if (bt_vendor_cbacks) {
		/* Must allocate command buffer via HC's alloc API */
		p_buf = (HC_BT_HDR *) bt_vendor_cbacks->alloc(BT_HC_HDR_SIZE + \
							      HCI_CMD_PREAMBLE_SIZE);
	}

	if (p_buf) {
		p_buf->event = MSG_STACK_TO_HC_HCI_CMD;
		p_buf->offset = 0;
		p_buf->layer_specific = 0;
		p_buf->len = HCI_CMD_PREAMBLE_SIZE;

		p = (uint8_t *) (p_buf + 1);
		UINT16_TO_STREAM(p, HCI_RESET);
		*p = 0; /* parameter length */

		/* Send command via HC's xmit_cb API */
		bt_vendor_cbacks->xmit_cb(HCI_RESET, p_buf, hw_epilog_cback);
	} else {
		if (bt_vendor_cbacks) {
			ALOGE("vendor lib epilog process aborted [no buffer]");
			bt_vendor_cbacks->epilog_cb(BT_VND_OP_RESULT_FAIL);
		}
	}
}
#endif // (HW_NEED_END_WITH_HCI_RESET == TRUE)

/*****************************************************************************
**   Functions
*****************************************************************************/

/*
 * Send HCI command and wait for command complete event.
 * The event buffer has to be freed by the caller.
 */

static int send_hci_cmd_sync(int dev, uint8_t *cmd, int len, uint8_t **event)
{
	int err;
	uint8_t *hci_event;
	uint8_t pkt_type = HCI_COMMAND_PKT;

	if (len == 0)
		return len;

	if (write(dev, &pkt_type, 1) != 1)
		return -EILSEQ;
	if (write(dev, (unsigned char *)cmd, len) != len)
		return -EILSEQ;

	hci_event = (uint8_t *)malloc(PS_EVENT_LEN);
	if (!hci_event)
		return -ENOMEM;

	err = read_hci_event(dev, (unsigned char *)hci_event, PS_EVENT_LEN);
	if (err > 0) {
		*event = hci_event;
	} else {
		free(hci_event);
		return -EILSEQ;
	}

	return len;
}

static void convert_bdaddr(char *str_bdaddr, char *bdaddr)
{
	char bdbyte[3];
	char *str_byte = str_bdaddr;
	int i, j;
	int colon_present = 0;

	if (strstr(str_bdaddr, ":"))
		colon_present = 1;

	bdbyte[2] = '\0';

	/* Reverse the BDADDR to LSB first */
	for (i = 0, j = 5; i < 6; i++, j--) {
		bdbyte[0] = str_byte[0];
		bdbyte[1] = str_byte[1];
		bdaddr[j] = strtol(bdbyte, NULL, 16);

		if (colon_present == 1)
			str_byte += 3;
		else
			str_byte += 2;
	}
}

static int uart_speed(int s)
{
    switch (s) {
	case 9600:
		return B9600;
	case 19200:
		return B19200;
	case 38400:
		return B38400;
	case 57600:
		return B57600;
	case 115200:
		return B115200;
	case 230400:
		return B230400;
	case 460800:
		return B460800;
	case 500000:
		return B500000;
	case 576000:
		return B576000;
	case 921600:
		return B921600;
	case 1000000:
		return B1000000;
	case 1152000:
		return B1152000;
	case 1500000:
		return B1500000;
	case 2000000:
		return B2000000;
#ifdef B2500000
	case 2500000:
		return B2500000;
#endif
#ifdef B3000000
	case 3000000:
		return B3000000;
#endif
#ifdef B3500000
	case 3500000:
		return B3500000;
#endif
#ifdef B4000000
	case 4000000:
		return B4000000;
#endif
	default:
		return B57600;
	}
}

int set_speed(int fd, struct termios *ti, int speed)
{
	if (cfsetospeed(ti, uart_speed(speed)) < 0)
		return -errno;

	if (cfsetispeed(ti, uart_speed(speed)) < 0)
		return -errno;

	if (tcsetattr(fd, TCSANOW, ti) < 0)
		return -errno;

	return 0;
}

static void load_hci_ps_hdr(uint8_t *cmd, uint8_t ps_op, int len, int index)
{
	hci_command_hdr *ch = (void *)cmd;

	ch->opcode = htobs(cmd_opcode_pack(HCI_VENDOR_CMD_OGF, HCI_PS_CMD_OCF));
	ch->plen = len + PS_HDR_LEN;
	cmd += HCI_COMMAND_HDR_SIZE;

	cmd[0] = ps_op;
	cmd[1] = index;
	cmd[2] = index >> 8;
	cmd[3] = len;
}


static int read_ps_event(uint8_t *event, uint16_t ocf)
{
	hci_event_hdr *eh;
	uint16_t opcode = htobs(cmd_opcode_pack(HCI_VENDOR_CMD_OGF, ocf));

	event++;

	eh = (void *)event;
	event += HCI_EVENT_HDR_SIZE;

	if (eh->evt == EVT_CMD_COMPLETE) {
		evt_cmd_complete *cc = (void *)event;

		event += EVT_CMD_COMPLETE_SIZE;

		if (cc->opcode == opcode && event[0] == HCI_EV_SUCCESS)
			return 0;
		else
			return -EILSEQ;
	}

	return -EILSEQ;
}

static int write_cmd(int fd, uint8_t *buffer, int len)
{
	uint8_t *event;
	int err;

	err = send_hci_cmd_sync(fd, buffer, len, &event);
	if (err < 0)
		return err;

	err = read_ps_event(event, HCI_PS_CMD_OCF);
	free(event);
	return err;
}

/* Sends PS commands using vendor specficic HCI commands */
static int write_ps_cmd(int fd, uint8_t opcode, uint32_t ps_param)
{
	uint8_t cmd[HCI_MAX_CMD_SIZE];
	uint32_t i;

	switch (opcode) {

	case PS_ENABLE_PATCH:
		load_hci_ps_hdr(cmd, opcode, 0, 0x00);
		if (write_cmd(fd, cmd, HCI_PS_CMD_HDR_LEN) < 0)
			return -EILSEQ;
		break;

	case PS_RESET:
		load_hci_ps_hdr(cmd, opcode, PS_RESET_PARAM_LEN, 0x00);

		cmd[7] = 0x00;
		cmd[PS_RESET_CMD_LEN - 2] = ps_param & PS_ID_MASK;
		cmd[PS_RESET_CMD_LEN - 1] = (ps_param >> 8) & PS_ID_MASK;

		if (write_cmd(fd, cmd, PS_RESET_CMD_LEN) < 0)
			return -EILSEQ;
		break;

	case PS_WRITE:
		for (i = 0; i < ps_param; i++) {
			load_hci_ps_hdr(cmd, opcode, ps_list[i].len, ps_list[i].id);
			memcpy(&cmd[HCI_PS_CMD_HDR_LEN], ps_list[i].data,
			ps_list[i].len);

			if (write_cmd(fd, cmd, ps_list[i].len + HCI_PS_CMD_HDR_LEN) < 0)
				return -EILSEQ;
		}
		break;
	}

	return 0;
}

static void get_ps_file_name(uint32_t devtype, uint32_t rom_version,char *path)
{
	char *filename;

	if (devtype == 0xdeadc0de)
		filename = PS_ASIC_FILE;
	else
		filename = PS_FPGA_FILE;

	snprintf(path, PS_MAXPATHLEN, "%s%x/%s", FW_PATH,
		 rom_version, filename);
}

static void get_patch_file_name(uint32_t dev_type, uint32_t rom_version,
				uint32_t build_version, char *path)
{
	if (rom_version == FPGA_ROM_VERSION && dev_type != ROM_DEV_TYPE &&
	    dev_type != 0 && build_version == 1)
		path[0] = '\0';
	else
		snprintf(path, PS_MAXPATHLEN, "%s%x/%s", FW_PATH,
			 rom_version, PATCH_FILE);
}

static int set_cntrlr_baud(int fd, int speed)
{
	int baud;
	struct timespec tm = { 0, 500000};
	unsigned char cmd[MAX_CMD_LEN], rsp[HCI_MAX_EVENT_SIZE];
	unsigned char *ptr = cmd + 1;
	hci_command_hdr *ch = (void *)ptr;

	cmd[0] = HCI_COMMAND_PKT;

	/* set controller baud rate to user specified value */
	ptr = cmd + 1;
	ch->opcode = htobs(cmd_opcode_pack(HCI_VENDOR_CMD_OGF,
			   HCI_CHG_BAUD_CMD_OCF));
	ch->plen = 2;
	ptr += HCI_COMMAND_HDR_SIZE;

	baud = speed/100;
	ptr[0] = (char)baud;
	ptr[1] = (char)(baud >> 8);

	if (write(fd, cmd, WRITE_BAUD_CMD_LEN) != WRITE_BAUD_CMD_LEN) {
		ALOGI("Failed to write change baud rate command");
		return -ETIMEDOUT;
	}

	nanosleep(&tm, NULL);

	if (read_hci_event(fd, rsp, sizeof(rsp)) < 0)
		return -ETIMEDOUT;

	return 0;
}

#define PS_UNDEF		0
#define PS_ID			1
#define PS_LEN			2
#define PS_DATA			3

#define PS_MAX_LEN		500
#define LINE_SIZE_MAX		(PS_MAX_LEN * 2)
#define ENTRY_PER_LINE		16

#define __check_comment(buf)	(((buf)[0] == '/') && ((buf)[1] == '/'))
#define __skip_space(str)	while (*(str) == ' ') ((str)++)


#define __is_delim(ch)		((ch) == ':')
#define MAX_PREAMBLE_LEN	4

/* Parse PS entry preamble of format [X:X] for main type and subtype */
static int get_ps_type(char *ptr, int index, char *type, char *sub_type)
{
	int i;
	int delim = FALSE;

	if (index > MAX_PREAMBLE_LEN)
		return -EILSEQ;

	for (i = 1; i < index; i++) {
		if (__is_delim(ptr[i])) {
			delim = TRUE;
			continue;
		}

		if (isalpha(ptr[i])) {
			if (delim == FALSE)
				(*type) = toupper(ptr[i]);
			else
				(*sub_type) = toupper(ptr[i]);
		}
	}

	return 0;
}

#define ARRAY	'A'
#define STRING	'S'
#define DECIMAL	'D'
#define BINARY	'B'

#define PS_HEX	0
#define PS_DEC	1

static int get_input_format(char *buf, struct ps_entry_type *format)
{
	char *ptr = NULL;
	char type = '\0';
	char sub_type = '\0';

	format->type = PS_HEX;
	format->array = TRUE;

	if (strstr(buf, "[") != buf)
		return 0;

	ptr = strstr(buf, "]");
	if (!ptr)
		return -EILSEQ;

	if (get_ps_type(buf, ptr - buf, &type, &sub_type) < 0)
		return -EILSEQ;

	/* Check is data type is of array */
	if (type == ARRAY || sub_type == ARRAY)
		format->array = TRUE;

	if (type == STRING || sub_type == STRING)
		format->array = FALSE;

	if (type == DECIMAL || type == BINARY)
		format->type = PS_DEC;
	else
		format->type = PS_HEX;

	return 0;
}

#define UNDEFINED 0xFFFF

static unsigned int read_data_in_section(char *buf, struct ps_entry_type type)
{
	char *ptr = buf;

	if (!buf)
		return UNDEFINED;

	if (buf == strstr(buf, "[")) {
		ptr = strstr(buf, "]");
		if (!ptr)
			return UNDEFINED;

		ptr++;
	}

	if (type.type == PS_HEX && type.array != TRUE)
		return strtol(ptr, NULL, 16);

	return UNDEFINED;
}


/* Read PS entries as string, convert and add to Hex array */
static void update_tag_data(struct ps_cfg_entry *tag,
			    struct tag_info *info, const char *ptr)
{
	char buf[3];

	buf[2] = '\0';

	strlcpy(buf, &ptr[info->char_cnt],sizeof(buf));
	tag->data[info->byte_count] = strtol(buf, NULL, 16);
	info->char_cnt += 3;
	info->byte_count++;

	strlcpy(buf, &ptr[info->char_cnt], sizeof(buf));
	tag->data[info->byte_count] = strtol(buf, NULL, 16);
	info->char_cnt += 3;
	info->byte_count++;
}

static inline int update_char_count(const char *buf)
{
	char *end_ptr;

	if (strstr(buf, "[") == buf) {
		end_ptr = strstr(buf, "]");
		if (!end_ptr)
			return 0;
	else
		return(end_ptr - buf) +	1;
	}

	return 0;
}

static int ath_parse_ps(FILE *stream)
{
	char buf[LINE_SIZE_MAX + 1];
	char *ptr;
	uint8_t tag_cnt = 0;
	int16_t byte_count = 0;
	struct ps_entry_type format;
	struct tag_info status = { 0, 0, 0, 0};

	do {
		int read_count;
		struct ps_cfg_entry *tag;

		ptr = fgets(buf, LINE_SIZE_MAX, stream);
		if (!ptr)
			break;

		__skip_space(ptr);
		if (__check_comment(ptr))
			continue;

		/* Lines with a '#' will be followed by new PS entry */
		if (ptr == strstr(ptr, "#")) {
			if (status.section != PS_UNDEF) {
				return -EILSEQ;
			} else {
				status.section = PS_ID;
				continue;
			}
		}

		tag = &ps_list[tag_cnt];

		switch (status.section) {

		case PS_ID:
			if (get_input_format(ptr, &format) < 0)
				return -EILSEQ;

			tag->id = read_data_in_section(ptr, format);
			status.section = PS_LEN;
			break;

		case PS_LEN:
			if (get_input_format(ptr, &format) < 0)
				return -EILSEQ;

			byte_count = read_data_in_section(ptr, format);
			if (byte_count > PS_MAX_LEN)
				return -EILSEQ;

			tag->len = byte_count;
			tag->data = (uint8_t *)malloc(byte_count);

			status.section = PS_DATA;
			status.line_count = 0;
			break;

		case PS_DATA:
			if (status.line_count == 0)
				if (get_input_format(ptr, &format) < 0)
					return -EILSEQ;

			__skip_space(ptr);

			status.char_cnt = update_char_count(ptr);

			read_count = (byte_count > ENTRY_PER_LINE) ?
				      ENTRY_PER_LINE : byte_count;

			if (format.type == PS_HEX && format.array == TRUE) {
				while (read_count > 0) {
					update_tag_data(tag, &status, ptr);
					read_count -= 2;
				}

				if (byte_count > ENTRY_PER_LINE)
					byte_count -= ENTRY_PER_LINE;
				else
					byte_count = 0;
			}

			status.line_count++;

			if (byte_count == 0)
				memset(&status, 0x00, sizeof(struct tag_info));

			if (status.section == PS_UNDEF)
				tag_cnt++;

			if (tag_cnt == MAX_TAGS)
				return -EILSEQ;

			break;

		}
	} while (ptr);

	return tag_cnt;
}

static int ps_config_download(int fd, int tag_count)
{
	if (write_ps_cmd(fd, PS_RESET, PS_RAM_SIZE) < 0)
		return -1;

	if (tag_count > 0)
		if (write_ps_cmd(fd, PS_WRITE, tag_count) < 0)
			return -1;

	return 0;
}

static int write_bdaddr(int pConfig, char *bdaddr)
{
	uint8_t *event;
	int err;
	uint8_t cmd[13];
	uint8_t *ptr = cmd;
	hci_command_hdr *ch = (void *)cmd;

	memset(cmd, 0, sizeof(cmd));

	ch->opcode = htobs(cmd_opcode_pack(HCI_VENDOR_CMD_OGF, HCI_PS_CMD_OCF));
	ch->plen = 10;
	ptr += HCI_COMMAND_HDR_SIZE;

	ptr[0] = 0x01;
	ptr[1] = 0x01;
	ptr[2] = 0x00;
	ptr[3] = 0x06;

	convert_bdaddr(bdaddr, (char *)&ptr[4]);

	err = send_hci_cmd_sync(pConfig, cmd, sizeof(cmd), &event);
	if (err < 0)
		return err;

	err = read_ps_event(event, HCI_PS_CMD_OCF);
	free(event);
	return err;
}

static void write_bdaddr_from_file(int rom_version, int fd)
{
	FILE *stream;
	char bdaddr[PATH_MAX];
	char bdaddr_file[PATH_MAX];

	UNUSED(rom_version);

	/* read bdaddr from /proc/idme/bt_mac_addr */
#if 0
	snprintf(bdaddr_file, PS_MAXPATHLEN, "%s%x/%s",
		 FW_PATH, rom_version, BDADDR_FILE);
#else
	snprintf(bdaddr_file, PS_MAXPATHLEN, "/proc/idme/bt_mac_addr");
#endif

	stream = fopen(bdaddr_file, "r");
	if (!stream)
		return;

	if (fgets(bdaddr, PATH_MAX - 1, stream))
		write_bdaddr(fd, bdaddr);

	fclose(stream);
}

void baswap(bdaddr_t *dst, const bdaddr_t *src)
{
	register unsigned char *d = (unsigned char *) dst;
	register const unsigned char *s = (const unsigned char *) src;
	register int i;
	for (i = 0; i < 6; i++)
		d[i] = s[5-i];
}


static int str2ba(const char *str, bdaddr_t *ba)
{
	uint8_t b[6];
	const char *ptr = str;
	int i;

	for (i = 0; i < 6; i++) {
		b[i] = (uint8_t) strtol(ptr, NULL, 16);
		ptr = strchr(ptr, ':');
		if (i != 5 && !ptr)
			ptr = ":00:00:00:00:00";
		ptr++;
	}

	baswap(ba, (bdaddr_t *) b);
	return 0;
}

static int get_device_type(int dev, uint32_t *code)
{
	uint8_t cmd[8] = {0};
	uint8_t *event;
	uint32_t reg;
	int err;
	uint8_t *ptr = cmd;
	hci_command_hdr *ch = (void *)cmd;

	ch->opcode = htobs(cmd_opcode_pack(HCI_VENDOR_CMD_OGF, GET_DEV_TYPE_OCF));
	ch->plen = 5;
	ptr += HCI_COMMAND_HDR_SIZE;

	ptr[0] = (uint8_t)DEV_REGISTER;
	ptr[1] = (uint8_t)DEV_REGISTER >> 8;
	ptr[2] = (uint8_t)DEV_REGISTER >> 16;
	ptr[3] = (uint8_t)DEV_REGISTER >> 24;
	ptr[4] = 0x04;

	err = send_hci_cmd_sync(dev, cmd, sizeof(cmd), &event);
	if (err < 0)
		return err;

	err = read_ps_event(event, GET_DEV_TYPE_OCF);
	if (err < 0)
		goto cleanup;

	reg = event[10];
	reg = (reg << 8) | event[9];
	reg = (reg << 8) | event[8];
	reg = (reg << 8) | event[7];
	*code = reg;

cleanup:
	free(event);

	return err;
}

static int read_ath3k_version(int pConfig, uint32_t *rom_version,
			      uint32_t *build_version)
{
	uint8_t cmd[3] = {0};
	uint8_t *event;
	int err;
	int status;
	hci_command_hdr *ch = (void *)cmd;

	ch->opcode = htobs(cmd_opcode_pack(HCI_VENDOR_CMD_OGF, GET_VERSION_OCF));
	ch->plen = 0;

	err = send_hci_cmd_sync(pConfig, cmd, sizeof(cmd), &event);
	if (err < 0)
		return err;

	err = read_ps_event(event, GET_VERSION_OCF);
	if (err < 0)
		goto cleanup;

	status = event[10];
	status = (status << 8) | event[9];
	status = (status << 8) | event[8];
	status = (status << 8) | event[7];
	*rom_version = status;

	status = event[14];
	status = (status << 8) | event[13];
	status = (status << 8) | event[12];
	status = (status << 8) | event[11];
	*build_version = status;

cleanup:
	free(event);

	return err;
}

static int get_ath3k_crc(int dev)
{
	uint8_t cmd[7] = {0};
	uint8_t *event;
	int err;

	load_hci_ps_hdr(cmd, VERIFY_CRC, 0, PS_REGION | PATCH_REGION);

	err = send_hci_cmd_sync(dev, cmd, sizeof(cmd), &event);
	if (err < 0)
		return err;

	/* Send error code if CRC check patched */
	if (read_ps_event(event, HCI_PS_CMD_OCF) >= 0)
		err = -EILSEQ;

	free(event);
	return err;
}

static int set_patch_ram(int dev, char *patch_loc, int len)
{
	int err;
	uint8_t cmd[20] = {0};
	int i, j;
	char loc_byte[3];
	uint8_t *event;
	uint8_t *loc_ptr = &cmd[7];

	UNUSED(len);

	if (!patch_loc)
		return -1;

	loc_byte[2] = '\0';

	load_hci_ps_hdr(cmd, HCI_SET_PATCH_RAM_ID, PS_HDR_LEN, 0);

	for (i = 0, j = 3; i < 4; i++, j--) {
		loc_byte[0] = patch_loc[0];
		loc_byte[1] = patch_loc[1];
		loc_ptr[j] = strtol(loc_byte, NULL, 16);
		patch_loc += 2;
	}

	err = send_hci_cmd_sync(dev, cmd, SET_PATCH_RAM_CMD_SIZE, &event);
	if (err < 0)
		return err;

	err = read_ps_event(event, HCI_PS_CMD_OCF);
	free(event);
	return err;
}

static int ps_patch_download(int fd, FILE *stream)
{
	char byte[3];
	char ptr[MAX_PATCH_CMD + 1];
	int byte_cnt;
	int patch_count = 0;
	char patch_loc[PATCH_LOC_STRING_LEN + 1];

	byte[2] = '\0';

	while (fgets(ptr, MAX_PATCH_CMD, stream)) {
		if (strlen(ptr) <= 1)
			continue;
		else if (strstr(ptr, PATCH_LOC_KEY) == ptr) {
			strlcpy(patch_loc, &ptr[sizeof(PATCH_LOC_KEY) - 1],
				PATCH_LOC_STRING_LEN);
			if (set_patch_ram(fd, patch_loc, sizeof(patch_loc)) < 0)
				return -1;
		} else if (isxdigit(ptr[0]))
			break;
		else
			return -1;
	}

	byte_cnt = strtol(ptr, NULL, 16);

	while (byte_cnt > 0) {
		int i;
		uint8_t cmd[HCI_MAX_CMD_SIZE] = {0};
		struct patch_entry patch;

		if (byte_cnt > MAX_PATCH_CMD)
			patch.len = MAX_PATCH_CMD;
		else
			patch.len = byte_cnt;

		for (i = 0; i < patch.len; i++) {
			if (!fgets(byte, 3, stream))
				return -1;

			patch.data[i] = strtoul(byte, NULL, 16);
		}

		load_hci_ps_hdr(cmd, PS_WRITE_PATCH, patch.len, patch_count);
		memcpy(&cmd[HCI_PS_CMD_HDR_LEN], patch.data, patch.len);

		if (write_cmd(fd, cmd, patch.len + HCI_PS_CMD_HDR_LEN) < 0)
			return -1;

		patch_count++;
		byte_cnt = byte_cnt - MAX_PATCH_CMD;
	}

	if (write_ps_cmd(fd, PS_ENABLE_PATCH, 0) < 0)
		return -1;

	return patch_count;
}

static int ath_ps_download(int fd)
{
	int err = 0;
	int tag_count;
	int patch_count = 0;
	uint32_t rom_version = 0;
	uint32_t build_version = 0;
	uint32_t dev_type = 0;
	char patch_file[PATH_MAX];
	char ps_file[PATH_MAX];
	FILE *stream;

	/*
	* Verfiy firmware version. depending on it select the PS
	* config file to download.
	*/
	if (get_device_type(fd, &dev_type) < 0) {
		err = -EILSEQ;
		goto download_cmplete;
	}

	if (read_ath3k_version(fd, &rom_version, &build_version) < 0) {
		err = -EILSEQ;
		goto download_cmplete;
	}

	/* Do not download configuration if CRC passes */
	if (get_ath3k_crc(fd) < 0) {
		err = 0;
		goto download_cmplete;
	}

	get_ps_file_name(dev_type, rom_version, ps_file);
	get_patch_file_name(dev_type, rom_version, build_version, patch_file);

	stream = fopen(ps_file, "r");
	if (!stream) {
		ALOGI("firmware file open error:%s, ver:%x\n", ps_file, rom_version);
		if (rom_version == PS_ROM_VERSION)
			err = 0;
		else
			err = -EILSEQ;
		goto download_cmplete;
	}

	tag_count = ath_parse_ps(stream);
	fclose(stream);

	if (tag_count < 0) {
		err = -EILSEQ;
		goto download_cmplete;
	}

	/*
	 * It is not necessary that Patch file be available,
	 * continue with PS Operations if patch file is not available.
	 */
	if (patch_file[0] == '\0')
		err = 0;

	stream = fopen(patch_file, "r");
	if (!stream)
		err = 0;
	else {
		patch_count = ps_patch_download(fd, stream);
		fclose(stream);

		if (patch_count < 0) {
			err = -EILSEQ;
			goto download_cmplete;
		}
	}

	err = ps_config_download(fd, tag_count);

download_cmplete:
	if (!err)
		write_bdaddr_from_file(rom_version, fd);

	return err;
}

int ath3k_init(int fd, int speed, int init_speed, char *bdaddr, struct termios *ti)
{
	ALOGI(" %s ", __FUNCTION__);

	int r;
	int err = 0;
	struct timespec tm = { 0, 500000};
	unsigned char cmd[MAX_CMD_LEN] = {0};
	unsigned char rsp[HCI_MAX_EVENT_SIZE];
	unsigned char *ptr = cmd + 1;
	hci_command_hdr *ch = (void *)ptr;
	int flags = 0;

	if (ioctl(fd, TIOCMGET, &flags) < 0) {
		ALOGI("TIOCMGET failed in init\n");
		return -1;
	}

	flags |= TIOCM_RTS;
	if (ioctl(fd, TIOCMSET, &flags) < 0) {
		ALOGI("TIOCMSET failed in init: HW Flow-on error\n");
		return -1;
	}

	/* set both controller and host baud rate to maximum possible value */
	err = set_cntrlr_baud(fd, speed);
	ALOGI("set_cntrlr_baud : ret:%d \n", err);
	if (err < 0)
		return err;

	err = set_speed(fd, ti, speed);
	if (err < 0) {
		ALOGI("Can't set required baud rate");
		return err;
	}

	/* Download PS and patch */
	r = ath_ps_download(fd);
	if (r < 0) {
		ALOGI("Failed to Download configuration");
		err = -ETIMEDOUT;
		goto failed;
	}

	ALOGI("ath_ps_download is done\n");

	cmd[0] = HCI_COMMAND_PKT;
	/* Write BDADDR */
	if (bdaddr) {
		ch->opcode = htobs(cmd_opcode_pack(HCI_VENDOR_CMD_OGF, HCI_PS_CMD_OCF));
		ch->plen = 10;
		ptr += HCI_COMMAND_HDR_SIZE;

		ptr[0] = 0x01;
		ptr[1] = 0x01;
		ptr[2] = 0x00;
		ptr[3] = 0x06;
		str2ba(bdaddr, (bdaddr_t *)(ptr + 4));

		if (write(fd, cmd, WRITE_BDADDR_CMD_LEN) != WRITE_BDADDR_CMD_LEN) {
			ALOGI("Failed to write BD_ADDR command\n");
			err = -ETIMEDOUT;
			goto failed;
		}

		if (read_hci_event(fd, rsp, sizeof(rsp)) < 0) {
			ALOGI("Failed to set BD_ADDR\n");
			err = -ETIMEDOUT;
			goto failed;
		}
	}

	/* Send HCI Reset */
	cmd[1] = 0x03;
	cmd[2] = 0x0C;
	cmd[3] = 0x00;

	r = write(fd, cmd, 4);
	if (r != 4) {
		err = -ETIMEDOUT;
		goto failed;
	}

	nanosleep(&tm, NULL);
	if (read_hci_event(fd, rsp, sizeof(rsp)) < 0) {
		err = -ETIMEDOUT;
		goto failed;
	}

	ALOGI("HCI Reset is done\n");
	err = set_cntrlr_baud(fd, speed);
	if (err < 0)
		ALOGI("set_cntrlr_baud0:%d,%d\n", speed, err);

failed:
	if (err < 0) {
		set_cntrlr_baud(fd, init_speed);
		set_speed(fd, ti, init_speed);
	}

	return err;
}

void lpm_set_ar3k(uint8_t pio, uint8_t action, uint8_t polarity)
{
	int rc;
	int fd = -1;
	char buffer;

	UNUSED(polarity);

	switch (pio) {

	case UPIO_LPM_MODE:
		if (upio_state[UPIO_LPM_MODE] == action) {
			ALOGI("LPM is %s already", lpm_mode[action]);
			return;
		}

		fd = open(VENDOR_LPM_PROC_NODE, O_WRONLY);
		if (fd < 0) {
			ALOGE("upio_set : open(%s) for write failed: %s (%d)",
			VENDOR_LPM_PROC_NODE, strerror(errno), errno);
			return;
		}

		if (action == UPIO_ASSERT)
			buffer = '1';
		else
			buffer = '0';

		if (write(fd, &buffer, 1) < 0) {
			ALOGE("upio_set : write(%s) failed: %s (%d)",
			VENDOR_LPM_PROC_NODE, strerror(errno),errno);
		} else {
			upio_state[UPIO_LPM_MODE] = action;
			ALOGD("LPM is set to %s", lpm_mode[action]);
		}

		if (fd >= 0)
			close(fd);

		break;

	case UPIO_BT_WAKE:
		/* UPIO_DEASSERT should be allowed because in Rx case assert occur
		* from the remote side where as deassert  will be initiated from Host
		*/
		if ((action == UPIO_ASSERT) && (upio_state[UPIO_BT_WAKE] == action)) {
			// ALOGI("BT_WAKE is %s already", lpm_state[action]);
			return;
		}

		ALOGD("BT_VND_OP_LPM_WAKE_SET_STATE");

		if (action == UPIO_DEASSERT)
			buffer = '0';
		else
			buffer = '1';

		fd = open(VENDOR_BTWRITE_PROC_NODE, O_WRONLY);
		if (fd < 0) {
			ALOGE("upio_set : open(%s) for write failed: %s (%d)",
			VENDOR_BTWRITE_PROC_NODE, strerror(errno), errno);
			return;
		}

		if (write(fd, &buffer, 1) < 0) {
			ALOGE("upio_set : write(%s) failed: %s (%d)",
			VENDOR_BTWRITE_PROC_NODE, strerror(errno),errno);
		} else {
			upio_state[UPIO_BT_WAKE] = action;
			ALOGD("BT_WAKE is set to %s", lpm_state[action]);
		}

		if (fd >= 0)
			close(fd);

		break;

	case UPIO_HOST_WAKE:
		ALOGD("upio_set: UPIO_HOST_WAKE");
		break;

	}
}
