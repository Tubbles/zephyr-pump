/*
 * Auto-connect to a stored WiFi network at boot.
 *
 * The credential lives in flash (provisioned once over the shell with
 * "wifi cred add ..."; see docs/LOG.md [wifi]); this asks the WiFi stack to
 * connect to it with NET_REQUEST_WIFI_CONNECT_STORED, the same request the
 * "wifi cred auto_connect" shell command issues. The request is only accepted
 * once the WiFi interface and supplicant are up, which is a little after kernel
 * boot, so a delayable work item retries until net_mgmt stops returning an
 * error (or a bounded number of tries elapses, e.g. on a board with no stored
 * credential, where the "wifi" shell remains the way to provision one).
 *
 * Association and DHCP run asynchronously; the result arrives as a management
 * event, logged here so the boot log shows whether the link came up.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_mgmt.h>
#include <zephyr/net/wifi_mgmt.h>
#include <zephyr/net/hostname.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(wifi, CONFIG_LOG_DEFAULT_LEVEL);

/* ~10 s window (20 * 500 ms) for the interface and supplicant to come up. */
#define CONNECT_RETRY_DELAY K_MSEC(500)
#define CONNECT_MAX_TRIES   20

static struct net_mgmt_event_callback wifi_cb;
static int connect_tries;

/* Append the last two MAC bytes (as hex) to the base hostname, e.g. "pump6820",
 * so two boards running this firmware get distinct names. Runs once, before the
 * connect, so DHCP (option 12) and mDNS both advertise the final name. The MAC
 * is the wlan0 link address, valid as soon as the interface exists.
 */
static void apply_hostname(struct net_if *iface)
{
	static bool done;

	if (done) {
		return;
	}

	struct net_linkaddr *lladdr = net_if_get_link_addr(iface);

	if (lladdr == NULL || lladdr->len < 2) {
		return;
	}

	char name[sizeof(CONFIG_NET_HOSTNAME) + 4];
	int len = snprintk(name, sizeof(name), "%s%02x%02x", CONFIG_NET_HOSTNAME,
			   lladdr->addr[lladdr->len - 2], lladdr->addr[lladdr->len - 1]);

	if (net_hostname_set(name, len) == 0) {
		done = true;
		LOG_INF("hostname %s", name);
	}
}

static void connect_work_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	struct net_if *iface = net_if_get_wifi_sta();

	if (iface != NULL) {
		apply_hostname(iface);
	}

	int ret = iface ? net_mgmt(NET_REQUEST_WIFI_CONNECT_STORED, iface, NULL, 0) : -ENODEV;

	if (ret == 0) {
		LOG_INF("connect request accepted; associating from stored credential");
		return;
	}

	if (++connect_tries < CONNECT_MAX_TRIES) {
		k_work_reschedule(k_work_delayable_from_work(work), CONNECT_RETRY_DELAY);
		return;
	}

	LOG_WRN("no stored network joined (iface not ready or no credential); "
		"use the 'wifi' shell");
}

K_WORK_DELAYABLE_DEFINE(connect_work, connect_work_handler);

static void wifi_event_handler(struct net_mgmt_event_callback *cb, uint64_t mgmt_event,
			       struct net_if *iface)
{
	ARG_UNUSED(iface);

	if (mgmt_event != NET_EVENT_WIFI_CONNECT_RESULT) {
		return;
	}

	const struct wifi_status *status = cb->info;

	if (status->status) {
		LOG_WRN("WiFi association failed (%d)", status->status);
	} else {
		LOG_INF("WiFi associated");
	}
}

static int wifi_autoconnect_init(void)
{
	net_mgmt_init_event_callback(&wifi_cb, wifi_event_handler, NET_EVENT_WIFI_CONNECT_RESULT);
	net_mgmt_add_event_callback(&wifi_cb);

	k_work_schedule(&connect_work, CONNECT_RETRY_DELAY);
	return 0;
}

SYS_INIT(wifi_autoconnect_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
