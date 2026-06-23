/*
 * OTA update over HTTP(S) for the DirectXIP A/B layout.
 *
 * A console "update" command group fetches a prebuilt, signed image from a
 * static endpoint (GitHub Pages by default) and writes it into the slot the
 * board is NOT currently running from, then reboots. MCUboot (DirectXIP) then
 * boots whichever slot holds the higher image version, so the published image
 * must carry a higher version than the running one (CI stamps that; see
 * docs/OTA.md).
 *
 * Why a slot-specific download: under DirectXIP an image only runs from the slot
 * it was built for (the IROM/DROM flash offsets are baked into its header; see
 * docs/LOG.md [directxip]). The running image knows its own slot from its
 * compiled-in CONFIG_FLASH_LOAD_OFFSET, so we fetch the variant for the other
 * slot ("firmware-slot0.bin" or "firmware-slot1.bin").
 *
 * Transport: DNS (getaddrinfo, servers from DHCP) + a TLS socket for https://.
 * TLS peer verification is OFF for now (encrypt-only, no embedded CA): the image
 * is unsigned too, so the trust model is already "trust the network". Real
 * authenticity (image signing + cert verification) is the documented next step.
 *
 * The base URL persists in the settings/NVS "update/url" key, so it survives a
 * reboot/reflash and can be repointed at the console without rebuilding.
 */

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/shell/shell.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/net_compat.h>
#include <zephyr/net/net_ip.h>
#include <zephyr/net/http/client.h>
#include <zephyr/dfu/flash_img.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/reboot.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/logging/log.h>
#include <app_version.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(update, CONFIG_LOG_DEFAULT_LEVEL);

#define URL_MAX        128
#define HOST_MAX       80
#define PATH_MAX       160
#define RECV_BUF_SIZE  2048
#define VERSION_BUF    96
#define CHECK_TIMEOUT_MS    (15 * 1000)
#define DOWNLOAD_TIMEOUT_MS (90 * 1000)

/* Active base URL; defaulted at build time, overridable + persisted at runtime. */
static char base_url[URL_MAX] = CONFIG_APP_UPDATE_URL;

static uint8_t recv_buf[RECV_BUF_SIZE];

/* ---- persistence (settings/NVS "update/url") ---------------------------- */

static int update_settings_set(const char *name, size_t len, settings_read_cb read_cb,
			       void *cb_arg)
{
	if (settings_name_steq(name, "url", NULL)) {
		ssize_t rc = read_cb(cb_arg, base_url, sizeof(base_url) - 1);

		if (rc > 0) {
			base_url[rc] = '\0';
		}
		return 0;
	}
	return -ENOENT;
}

SETTINGS_STATIC_HANDLER_DEFINE(update, "update", NULL, update_settings_set, NULL, NULL);

static void save_url(void)
{
	settings_save_one("update/url", base_url, strlen(base_url));
}

/* ---- slot selection ----------------------------------------------------- */

/* The running image was linked for whichever slot its CONFIG_FLASH_LOAD_OFFSET
 * matches; the OTA target is the other one.
 */
static bool running_slot0(void)
{
	return CONFIG_FLASH_LOAD_OFFSET == PARTITION_OFFSET(slot0_partition);
}

static uint8_t target_area_id(void)
{
	return running_slot0() ? PARTITION_ID(slot1_partition) : PARTITION_ID(slot0_partition);
}

static const char *target_leaf(void)
{
	return running_slot0() ? "firmware-slot1.bin" : "firmware-slot0.bin";
}

/* ---- URL parsing -------------------------------------------------------- */

static int parse_base(const char *url, bool *tls, char *host, size_t host_sz, uint16_t *port,
		      const char **prefix)
{
	const char *rest;

	if (strncmp(url, "https://", 8) == 0) {
		*tls = true;
		*port = 443;
		rest = url + 8;
	} else if (strncmp(url, "http://", 7) == 0) {
		*tls = false;
		*port = 80;
		rest = url + 7;
	} else {
		return -EINVAL;
	}

	const char *slash = strchr(rest, '/');
	size_t host_len = slash ? (size_t)(slash - rest) : strlen(rest);

	if (host_len == 0 || host_len >= host_sz) {
		return -EINVAL;
	}
	memcpy(host, rest, host_len);
	host[host_len] = '\0';
	*prefix = slash ? slash : "";
	return 0;
}

/* ---- connection --------------------------------------------------------- */

static int connect_to(bool tls, const char *host, uint16_t port, int *out_sock)
{
	struct zsock_addrinfo hints = {
		.ai_family = AF_INET,
		.ai_socktype = SOCK_STREAM,
	};
	struct zsock_addrinfo *res = NULL;

	int rc = zsock_getaddrinfo(host, NULL, &hints, &res);

	if (rc != 0 || res == NULL) {
		LOG_ERR("getaddrinfo(%s) failed: %d", host, rc);
		return -EIO;
	}
	((struct net_sockaddr_in *)res->ai_addr)->sin_port = sys_cpu_to_be16(port);

	int proto = tls ? IPPROTO_TLS_1_2 : IPPROTO_TCP;
	int sock = zsock_socket(res->ai_family, SOCK_STREAM, proto);

	if (sock < 0) {
		LOG_ERR("socket() failed: %d", errno);
		zsock_freeaddrinfo(res);
		return -EIO;
	}

	if (tls) {
		int verify = TLS_PEER_VERIFY_NONE;

		zsock_setsockopt(sock, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
		zsock_setsockopt(sock, SOL_TLS, TLS_HOSTNAME, host, strlen(host) + 1);
	}

	rc = zsock_connect(sock, res->ai_addr, res->ai_addrlen);
	zsock_freeaddrinfo(res);
	if (rc < 0) {
		LOG_ERR("connect(%s) failed: %d", host, errno);
		zsock_close(sock);
		return -EIO;
	}

	*out_sock = sock;
	return 0;
}

/* ---- download into the inactive slot ------------------------------------ */

struct flash_sink {
	struct flash_img_context ctx;
	int err;
	int status;
	size_t written;
};

static struct flash_sink sink;

static int body_to_flash(struct http_response *rsp, enum http_final_call final, void *user_data)
{
	struct flash_sink *fs = user_data;

	if (rsp->http_status_code) {
		fs->status = rsp->http_status_code;
	}
	if (fs->err) {
		return 0;
	}
	if (fs->status && fs->status != 200) {
		/* Do not write an error page (e.g. a 404) into the slot. */
		fs->err = -EIO;
		return 0;
	}

	bool flush = (final == HTTP_DATA_FINAL);

	if (rsp->body_frag_len > 0 || flush) {
		const uint8_t *data = rsp->body_frag_start ? rsp->body_frag_start
							   : (const uint8_t *)fs;
		int rc = flash_img_buffered_write(&fs->ctx, data, rsp->body_frag_len, flush);

		if (rc) {
			fs->err = rc;
			return 0;
		}
		fs->written += rsp->body_frag_len;
	}
	return 0;
}

static int download_to_slot(const struct shell *sh)
{
	bool tls;
	char host[HOST_MAX];
	const char *prefix;
	uint16_t port;

	int rc = parse_base(base_url, &tls, host, sizeof(host), &port, &prefix);

	if (rc) {
		shell_error(sh, "bad base URL '%s'", base_url);
		return rc;
	}

	static char path[PATH_MAX];

	snprintk(path, sizeof(path), "%s/%s", prefix, target_leaf());

	uint8_t area = target_area_id();

	rc = flash_img_init_id(&sink.ctx, area);
	if (rc) {
		shell_error(sh, "flash_img_init(area %u) failed: %d", area, rc);
		return rc;
	}
	sink.err = 0;
	sink.status = 0;
	sink.written = 0;

	int sock;

	rc = connect_to(tls, host, port, &sock);
	if (rc) {
		shell_error(sh, "connect to %s failed", host);
		return rc;
	}

	struct http_request req = {
		.method = HTTP_GET,
		.url = path,
		.host = host,
		.protocol = "HTTP/1.1",
		.response = body_to_flash,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	shell_print(sh, "downloading %s%s into slot area %u ...", host, path, area);
	rc = http_client_req(sock, &req, DOWNLOAD_TIMEOUT_MS, &sink);
	zsock_close(sock);

	if (rc < 0) {
		shell_error(sh, "HTTP request failed: %d", rc);
		return rc;
	}
	if (sink.status != 200) {
		shell_error(sh, "server returned HTTP %d", sink.status);
		return -EIO;
	}
	if (sink.err) {
		shell_error(sh, "flash write failed: %d", sink.err);
		return sink.err;
	}
	if (sink.written == 0) {
		shell_error(sh, "no image data received");
		return -EIO;
	}

	shell_print(sh, "wrote %zu bytes to slot area %u", sink.written, area);
	return 0;
}

/* ---- version check ------------------------------------------------------ */

struct text_sink {
	char buf[VERSION_BUF];
	size_t len;
	int status;
};

static int body_to_text(struct http_response *rsp, enum http_final_call final, void *user_data)
{
	ARG_UNUSED(final);
	struct text_sink *ts = user_data;

	if (rsp->http_status_code) {
		ts->status = rsp->http_status_code;
	}
	size_t n = rsp->body_frag_len;

	if (n && ts->len + n < sizeof(ts->buf)) {
		memcpy(ts->buf + ts->len, rsp->body_frag_start, n);
		ts->len += n;
	}
	return 0;
}

static int fetch_version(const struct shell *sh)
{
	bool tls;
	char host[HOST_MAX];
	const char *prefix;
	uint16_t port;

	int rc = parse_base(base_url, &tls, host, sizeof(host), &port, &prefix);

	if (rc) {
		shell_error(sh, "bad base URL '%s'", base_url);
		return rc;
	}

	static char path[PATH_MAX];

	snprintk(path, sizeof(path), "%s/version", prefix);

	int sock;

	rc = connect_to(tls, host, port, &sock);
	if (rc) {
		shell_error(sh, "connect to %s failed", host);
		return rc;
	}

	struct text_sink ts = {0};
	struct http_request req = {
		.method = HTTP_GET,
		.url = path,
		.host = host,
		.protocol = "HTTP/1.1",
		.response = body_to_text,
		.recv_buf = recv_buf,
		.recv_buf_len = sizeof(recv_buf),
	};

	rc = http_client_req(sock, &req, CHECK_TIMEOUT_MS, &ts);
	zsock_close(sock);

	if (rc < 0) {
		shell_error(sh, "HTTP request failed: %d", rc);
		return rc;
	}
	if (ts.status != 200) {
		shell_error(sh, "server returned HTTP %d", ts.status);
		return -EIO;
	}

	/* Trim trailing whitespace/newline from the published version string. */
	while (ts.len > 0 && (ts.buf[ts.len - 1] == '\n' || ts.buf[ts.len - 1] == '\r' ||
			      ts.buf[ts.len - 1] == ' ')) {
		ts.len--;
	}
	ts.buf[ts.len] = '\0';

	shell_print(sh, "running: %s", APP_VERSION_STRING);
	shell_print(sh, "published: %s", ts.buf);
	return 0;
}

/* ---- shell commands ----------------------------------------------------- */

static int cmd_url(const struct shell *sh, size_t argc, char **argv)
{
	if (argc == 1) {
		shell_print(sh, "%s", base_url);
		return 0;
	}
	if (strlen(argv[1]) >= sizeof(base_url)) {
		shell_error(sh, "URL too long (max %d)", URL_MAX - 1);
		return -EINVAL;
	}
	strcpy(base_url, argv[1]);
	save_url();
	shell_print(sh, "%s", base_url);
	return 0;
}

static int cmd_check(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
	return fetch_version(sh);
}

static int cmd_now(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int rc = download_to_slot(sh);

	if (rc) {
		return rc;
	}
	shell_print(sh, "update staged; rebooting into the new slot in 1s");
	k_sleep(K_MSEC(1000));
	sys_reboot(SYS_REBOOT_COLD);
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "running version : %s", APP_VERSION_STRING);
	shell_print(sh, "running slot    : %s", running_slot0() ? "slot0" : "slot1");
	shell_print(sh, "target on update: %s (area %u)", target_leaf(), target_area_id());
	shell_print(sh, "base url        : %s", base_url);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(update_cmds,
	SHELL_CMD_ARG(url, NULL, "Show or set the base URL: update url [<url>]", cmd_url, 1, 1),
	SHELL_CMD(check, NULL, "Fetch and print the published version", cmd_check),
	SHELL_CMD(now, NULL, "Download the inactive-slot image, flash it, reboot", cmd_now),
	SHELL_CMD(status, NULL, "Show running slot, version and URL", cmd_status),
	SHELL_SUBCMD_SET_END);

SHELL_CMD_REGISTER(update, &update_cmds, "OTA firmware update", NULL);

/* ---- load the persisted URL at boot ------------------------------------- */

static int update_init(void)
{
	settings_subsys_init();
	settings_load_subtree("update");
	return 0;
}

SYS_INIT(update_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
