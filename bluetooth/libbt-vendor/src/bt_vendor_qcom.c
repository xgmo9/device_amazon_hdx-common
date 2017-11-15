/*
 * Copyright 2017 The Android Open Source Project
 * Copyright (c) 2013, The Linux Foundation. All rights reserved.
 * Not a Contribution.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/******************************************************************************
 *
 *  Filename:      bt_vendor_qcom.c
 *
 *  Description:   vendor specific library implementation
 *
 ******************************************************************************/

#define LOG_TAG "bt_vendor"

#include <utils/Log.h>
#include <cutils/properties.h>
#include <fcntl.h>
#include <termios.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <cutils/sockets.h>
#include <linux/un.h>

#include "bt_vendor_lib.h"
#include "bt_vendor_qcom.h"
#include "hci_uart.h"
#include "hw_ar3k.h"

/******************************************************************************
**  Variables
******************************************************************************/
bt_vendor_callbacks_t *bt_vendor_cbacks = NULL;
uint8_t vnd_local_bd_addr[6] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
static int rfkill_id = -1;
static char *rfkill_state = NULL;
bool enable_extldo = FALSE;

static const tUSERIAL_CFG userial_init_cfg =
{
    (USERIAL_DATABITS_8 | USERIAL_PARITY_NONE | USERIAL_STOPBITS_1),
    USERIAL_BAUD_115200
};

#ifdef WIFI_BT_STATUS_SYNC
#include <string.h>
#include <errno.h>
#include <dlfcn.h>
#include "cutils/properties.h"

static const char WIFI_PROP_NAME[]    = "wlan.driver.status";
static const char SERVICE_PROP_NAME[] = "bluetooth.hsic_ctrl";
static const char BT_STATUS_NAME[]    = "bluetooth.enabled";
static const char WIFI_SERVICE_PROP[] = "wlan.hsic_ctrl";

#define WIFI_BT_STATUS_LOCK    "/data/misc/bluetooth/wifi_bt_lock"
int isInit = 0;
#endif /* WIFI_BT_STATUS_SYNC */

/******************************************************************************
**  Functions
******************************************************************************/

#ifdef WIFI_BT_STATUS_SYNC
int bt_semaphore_create(void)
{
	int fd;

	fd = open(WIFI_BT_STATUS_LOCK, O_RDONLY | O_CREAT);

	if (fd < 0)
		ALOGE("can't create file %d\n", fd);

	return fd;
}

int bt_semaphore_get(int fd)
{
	int ret;

	if (fd < 0)
		return -1;

	ret = flock(fd, LOCK_EX);
	if (ret != 0) {
		ALOGE("can't hold lock: %s\n", strerror(errno));
		return -1;
	}

	return ret;
}

int bt_semaphore_release(int fd)
{
	int ret;

	if (fd < 0)
		return -1;

	ret = flock(fd, LOCK_UN);
	if (ret != 0) {
		ALOGE("can't release lock: %s\n", strerror(errno));
		return -1;
	}

	return ret;
}

int bt_semaphore_destroy(int fd)
{
	if (fd < 0)
		return -1;

	return close(fd);
}

int bt_wait_for_service_done(void)
{
	char service_status[PROPERTY_VALUE_MAX];
	int count = 30;

	ALOGD("%s: check\n", __func__);

	/* wait for service done */
	while (count-- > 0) {
		property_get(WIFI_SERVICE_PROP, service_status, NULL);

		if (strcmp(service_status, "") != 0) {
			usleep(200000);
		} else {
			break;
		}
	}

	return 0;
}
#endif /* WIFI_BT_STATUS_SYNC */

/** Bluetooth Controller power up or shutdown */
static int bt_powerup(int enable)
{
	char rfkill_type[64], *enable_ldo_path = NULL;
	char type[16], enable_ldo[6];
	int fd, size, i, ret, fd_ldo;

	char disable[PROPERTY_VALUE_MAX];
	char state;
	char on = (enable) ? '1' : '0';

#ifdef WIFI_BT_STATUS_SYNC
	char wifi_status[PROPERTY_VALUE_MAX];
	int lock_fd;
#endif /*WIFI_BT_STATUS_SYNC*/

	ALOGD("bt_powerup: %c", on);

	/* Check if rfkill has been disabled */
	ret = property_get("ro.rfkilldisabled", disable, "0");
	if (!ret ){
		ALOGE("Couldn't get ro.rfkilldisabled (%d)", ret);
		return -1;
	}

	/* In case rfkill disabled, then no control power*/
	if (strcmp(disable, "1") == 0) {
		ALOGD("ro.rfkilldisabled : %s", disable);
		return -1;
	}

#ifdef WIFI_BT_STATUS_SYNC
	lock_fd = bt_semaphore_create();
	bt_semaphore_get(lock_fd);
	bt_wait_for_service_done();
#endif

	/* Assign rfkill_id and find bluetooth rfkill state path*/
	for (i = 0; (rfkill_id == -1) && (rfkill_state == NULL); i++) {
		snprintf(rfkill_type, sizeof(rfkill_type), "/sys/class/rfkill/rfkill%d/type", i);

		if ((fd = open(rfkill_type, O_RDONLY)) < 0) {
			ALOGE("open(%s) failed: %s (%d)\n", rfkill_type, strerror(errno), errno);
#ifdef WIFI_BT_STATUS_SYNC
			bt_semaphore_release(lock_fd);
			bt_semaphore_destroy(lock_fd);
#endif
			return -1;
		}

		size = read(fd, &type, sizeof(type));
		close(fd);

		if ((size >= 9) && !memcmp(type, "bluetooth", 9)) {
			asprintf(&rfkill_state, "/sys/class/rfkill/rfkill%d/state", rfkill_id = i);
			break;
		}
	}

	/* Get rfkill State to control */
	if (rfkill_state != NULL) {
		if ((fd = open(rfkill_state, O_RDWR)) < 0) {
			ALOGE("open(%s) for write failed: %s (%d)",
			      rfkill_state, strerror(errno), errno);
#ifdef WIFI_BT_STATUS_SYNC
			bt_semaphore_release(lock_fd);
			bt_semaphore_destroy(lock_fd);
#endif
			return -1;
		}
	}

#ifdef USE_EXTERNAL_RFKILL_LDO
	ret = asprintf(&enable_ldo_path, "/sys/class/rfkill/rfkill%d/device/extldo", rfkill_id);
	if ((ret < 0 ) || (enable_ldo_path == NULL)) {
		ALOGE("Memory Allocation failure");
		return -1;
	}

	if ((fd_ldo = open(enable_ldo_path, O_RDWR)) < 0) {
		ALOGE("open(%s) failed: %s (%d)", enable_ldo_path, strerror(errno), errno);
		return -1;
	}

	size = read(fd_ldo, &enable_ldo, sizeof(enable_ldo));
	close(fd_ldo);

	if (size <= 0) {
		ALOGE("read(%s) failed: %s (%d)", enable_ldo_path, strerror(errno), errno);
		return -1;
	}

	if (!memcmp(enable_ldo, "true", 4)) {
		ALOGI("External LDO has been configured");
		enable_extldo = TRUE;
	}
#endif

	ALOGD("Write %c to %s\n", on, rfkill_state);

	/* Write value to control rfkill */
	if ((size = write(fd, &on, 1)) < 0) {
		ALOGE("write(%s) failed: %s (%d)",rfkill_state, strerror(errno),errno);
#ifdef WIFI_BT_STATUS_SYNC
		bt_semaphore_release(lock_fd);
		bt_semaphore_destroy(lock_fd);
#endif
		return -1;
	}

#ifdef WIFI_BT_STATUS_SYNC
	/* query wifi status */
	property_get(WIFI_PROP_NAME, wifi_status, "");

	ALOGI("bt get wifi status: %s, isInit: %d\n",  wifi_status, isInit);

	/* If wlan driver is not loaded, and bt is changed from off => on */
	if (strncmp(wifi_status, "unloaded", strlen("unloaded")) == 0 ||
	    strlen(wifi_status) == 0) {
		if (on == '1') {
			ALOGD("%s: BT_VND_PWR_ON\n", __func__);
			if (property_set(SERVICE_PROP_NAME, "load_wlan") < 0) {
				ALOGE("%s Property setting failed",
				      SERVICE_PROP_NAME);
				close(fd);
				bt_semaphore_release(lock_fd);
				bt_semaphore_destroy(lock_fd);
				return -1;
			}
		} else if (isInit == 0 && on == '0') {
            		ALOGD("%s: BT_VND_PWR_OFF\n", __func__);
			if (property_set(SERVICE_PROP_NAME, "unbind_hsic") < 0) {
				ALOGE("%s Property setting failed",
				      SERVICE_PROP_NAME);
				close(fd);
				bt_semaphore_release(lock_fd);
				bt_semaphore_destroy(lock_fd);
				return -1;
			}
		}
	}

	if (isInit == 0 && on == '0')
		property_set(BT_STATUS_NAME, "false");
	else if (on == '1')
		property_set(BT_STATUS_NAME, "true");

	bt_semaphore_release(lock_fd);
	bt_semaphore_destroy(lock_fd);
#endif /* WIFI_BT_STATUS_SYNC */

done:
	if (fd >= 0)
		close(fd);

	return 0;
}

/*****************************************************************************
**
**   BLUETOOTH VENDOR INTERFACE LIBRARY FUNCTIONS
**
*****************************************************************************/

static int init(const bt_vendor_callbacks_t* p_cb,
		unsigned char *local_bdaddr)
{
	int i;

	if (p_cb == NULL) {
		ALOGE("init failed with no user callbacks!");
		return -1;
	}

	ALOGI("Initializing UART transport layer");
	userial_vendor_init();

	/* store reference to user callbacks */
	bt_vendor_cbacks = (bt_vendor_callbacks_t *) p_cb;

	/* Copy BD Address as little-endian byte order */
	if (local_bdaddr)
		for (i = 0; i < 6; i++)
			vnd_local_bd_addr[i] = *(local_bdaddr + (5 - i));

	ALOGI("%s: Local BD Address : %.2x:%.2x:%.2x:%.2x:%.2x:%.2x",
	      __FUNCTION__, vnd_local_bd_addr[0], vnd_local_bd_addr[1],
	      vnd_local_bd_addr[2], vnd_local_bd_addr[3], vnd_local_bd_addr[4],
	      vnd_local_bd_addr[5]);

#ifdef WIFI_BT_STATUS_SYNC
	isInit = 1;
#endif /* WIFI_BT_STATUS_SYNC */

	return 0;
}

/** Requested operations */
static int op(bt_vendor_opcode_t opcode, void *param)
{
	int retval = 0;
	int nCnt = 0;
	int nState = -1;
	bool is_ant_req = false;
	char wipower_status[PROPERTY_VALUE_MAX];
	char emb_wp_mode[PROPERTY_VALUE_MAX];
	char bt_version[PROPERTY_VALUE_MAX];
	bool ignore_boot_prop = TRUE;
	bool skip_init = true;

	switch(opcode) {

	case BT_VND_OP_POWER_CTRL:
		nState = *(int *)param;
		ALOGD("BT_VND_OP_POWER_CTRL: %s",
		      (nState == BT_VND_PWR_ON)? "On" : "Off" );

		/* BT Chipset Power Control through Device Tree Node */
		retval = bt_powerup(nState);
		break;

	case BT_VND_OP_FW_CFG:
		ALOGD("BT_VND_OP_FW_CFG");
		/* call hciattach to initalize the stack */
		if (bt_vendor_cbacks) {
			ALOGI("Bluetooth FW and transport layer are initialized");
			bt_vendor_cbacks->fwcfg_cb(BT_VND_OP_RESULT_SUCCESS);
		} else {
			ALOGE("bt_vendor_cbacks is null");
			ALOGE("Error : hci, smd initialization Error");
			retval = -1;
		}
		break;

	case BT_VND_OP_SCO_CFG:
		ALOGD("BT_VND_OP_SCO_CFG");
		if (bt_vendor_cbacks) {
			/* dummy */
			bt_vendor_cbacks->scocfg_cb(BT_VND_OP_RESULT_SUCCESS);
		}
		break;

	case BT_VND_OP_USERIAL_OPEN:
		ALOGD("BT_VND_OP_USERIAL_OPEN");
		{
			int (*fd_array)[] = (int (*)[]) param;
			int idx, fd;

			fd = userial_vendor_open((tUSERIAL_CFG *) &userial_init_cfg);
			if (fd != -1) {
				for (idx = 0; idx < CH_MAX; idx++)
					(*fd_array)[idx] = fd;
				retval = 1;
			} else {
				retval = -1;
				break;
			}

			/* Vendor Specific Process should happened during
			 * userial_open process.  After userial_open, rx read
			 * thread is running immediately, so it will affect VS
			 * event read process.
			 */
			if (ath3k_init(fd, 3000000, 115200, NULL,
				       &vnd_userial.termios) < 0)
				retval = -1;
		}
		break;

	case BT_VND_OP_USERIAL_CLOSE:
		ALOGD("BT_VND_OP_USERIAL_CLOSE");
		property_set("wc_transport.clean_up","1");
		userial_vendor_close();
		break;

	case BT_VND_OP_GET_LPM_IDLE_TIMEOUT:
		ALOGD("BT_VND_OP_GET_LPM_IDLE_TIMEOUT");
		{
			uint32_t *timeout_ms = (uint32_t *) param;
			*timeout_ms = 1000;
		}
		break;

	case BT_VND_OP_LPM_SET_MODE:
		ALOGD("BT_VND_OP_LPM_SET_MODE");
		{
			uint8_t *mode = (uint8_t *) param;

			if (*mode)
				lpm_set_ar3k(UPIO_LPM_MODE, UPIO_ASSERT, 0);
			else
				lpm_set_ar3k(UPIO_LPM_MODE, UPIO_DEASSERT, 0);
			if (bt_vendor_cbacks)
				bt_vendor_cbacks->lpm_cb(BT_VND_OP_RESULT_SUCCESS);
		}
		break;

	case BT_VND_OP_LPM_WAKE_SET_STATE:
		{
			uint8_t *state = (uint8_t *) param;
			uint8_t wake_assert = (*state == BT_VND_LPM_WAKE_ASSERT) ? \
					       UPIO_ASSERT : UPIO_DEASSERT;
			lpm_set_ar3k(UPIO_BT_WAKE, wake_assert, 0);
		}
		break;

	case BT_VND_OP_SET_AUDIO_STATE:
		/* TODO */
		ALOGD("BT_VND_OP_SET_AUDIO_STATE");
		break;

	case BT_VND_OP_EPILOG:
		ALOGD("BT_VND_OP_EPILOG");
#if (HW_NEED_END_WITH_HCI_RESET == TRUE)
		hw_epilog_process();
#else
		if (bt_vendor_cbacks)
			bt_vendor_cbacks->epilog_cb(BT_VND_OP_RESULT_SUCCESS);
#endif
		break;

	case BT_VND_OP_A2DP_OFFLOAD_START:
		/* TODO */
		ALOGD("BT_VND_OP_A2DP_OFFLOAD_START");
		break;

	case BT_VND_OP_A2DP_OFFLOAD_STOP:
		/* TODO */
		ALOGD("BT_VND_OP_A2DP_OFFLOAD_STOP");
		break;

	case FM_VND_OP_POWER_CTRL:
		/* TODO */
		ALOGD("FM_VND_OP_POWER_CTRL");
		break;

	case BT_VND_OP_FM_USERIAL_OPEN:
		/* TODO */
		ALOGD("BT_VND_OP_FM_USERIAL_OPEN");
		break;

	case BT_VND_OP_FM_USERIAL_CLOSE:
		/* TODO */
		ALOGD("BT_VND_OP_FM_USERIAL_CLOSE");
		break;

	}

	return retval;
}

/** Closes the interface */
static void cleanup(void)
{
	ALOGD("cleanup");
	bt_vendor_cbacks = NULL;
#ifdef WIFI_BT_STATUS_SYNC
	isInit = 0;
#endif /* WIFI_BT_STATUS_SYNC */
}

static void ssr_cleanup(int reason)
{
	UNUSED(reason);

	ALOGD("ssr_cleanup");
}

/** Entry point of DLib */
const bt_vendor_interface_t BLUETOOTH_VENDOR_LIB_INTERFACE = {
	sizeof(bt_vendor_interface_t),
	init,
	op,
	cleanup,
	ssr_cleanup
};
