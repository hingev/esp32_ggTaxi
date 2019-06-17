#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "tcpip_adapter.h"

#include <endian.h>

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_tls.h"

/* My includes */
#include "common.h"
#include "session.h"

int tx_buff_encapsulate (TxBuff **res, TxBuff *src, uint32_t mask) {

	ESP_LOGI ("ENC", "MASK: %08X", mask);

	TxBuff *tb = calloc (1, sizeof (TxBuff));

	assert (tb != NULL);

	int header_size = -1;
	if (src->len <= 125) {
		header_size = 2;
	}
	else if (src->len > 125 	/* FIXME: what if really big ?? */) {
		header_size = 4;
	}

	ESP_LOGI ("ENC", "Header size: %d", header_size);

	tb->len = src->len + header_size + 4; /* 4 bytes for masking; 4 just in case */

	ESP_LOGI ("ENC", "TB-len = %d", tb->len);
	ESP_LOGI ("ENC", "SRc len: %d", src->len);

	tb->buff = malloc ( tb->len );

	assert (tb->buff != NULL);

	struct HEADER h = {0};
	h.opcode = src->opcode;
	h.mask = 1;
	h.fin = 1;
	if (header_size == 2) {
		h.payload_len = src->len;
	}
	else if (header_size == 4) {
		/* htole16 donesn't compile for some reason */
		h.payload_len = 126;
		h.len_ex = (0xFF & (src->len >> 8)) | ((src->len & 0xFF) << 8);
	}
	else {
		/* FIXME: */
		abort ();
	}

	memcpy (tb->buff, (char*)&h, header_size);
	size_t cur_i = header_size;
	size_t i;
	uint8_t masks[4] = {
		mask >> 24,
		(mask >> 16) & 0xFF,
		(mask >> 8) & 0xFF,
		mask & 0xFF,
	};
	tb->buff[cur_i++] = masks[0];
	tb->buff[cur_i++] = masks[1];
	tb->buff[cur_i++] = masks[2];
	tb->buff[cur_i++] = masks[3];

	for (i=0; i < src->len; ++i) {
		/* ESP_LOGI ("ENC", "Cur i: %d; i = %d", cur_i, i); */
		assert (cur_i < tb->len);
		tb->buff[cur_i++] = src->buff[i] ^ masks[i%4];
	}

	ESP_LOG_BUFFER_HEXDUMP ("ENC", tb->buff, cur_i, ESP_LOG_INFO);

	*res = tb;
	return 0;
}

EventGroupHandle_t wss_event_group;
QueueHandle_t tx_queue;

#define TAG "WSS"

#define WEB_SERVER "api.ggtaxi.com"
#define WEB_PORT "443"
#define WEB_URL "https://api.ggtaxi.com/socket.io/"

extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

static const char *REQUEST_FORMAT =
	"GET /socket.io/?%s HTTP/1.1\r\n"
	"Host: " WEB_SERVER "\r\n"
	"Upgrade: websocket\r\n"
	"Connection: Upgrade\r\n"
	"Sec-WebSocket-Key: %s\r\n"
	"Sec-WebSocket-Version: 13\r\n"
	"Origin: https://dashboard.ggtaxi.com\r\n"
	"\r\n";

static const char *PARAMS_FORMAT =
	"token=%s&userId=%s&mode=%s&EIO=3&transport=websocket";


static TaskHandle_t xHandle = NULL;

enum WebSocketState {
	HTTP_HEADER = 0,
	PAYLOAD_START,
	PAYLOAD_DATA
} cur_state;

static int header_get_size (struct HEADER *h) {
	if (h->payload_len <= 125)
		return 2;
	else if (h->payload_len == 126)
		return 4;
	else if (h->payload_len == 127)
		return 8;
	else
		abort ();
	return -1;
}

struct BUFFER {
	char * data;
	ssize_t size;
	ssize_t ind;
};
typedef struct BUFFER Buffer;

#define my_min(A,B)	( (A)>(B) ? (B) : (A) )

static void add_to_buffer (Buffer *b, char *add, int len) {
	if (b->data == NULL) {
		b->size = 2048;
		b->data = calloc (b->size, sizeof (char));
	}
	if (b->size < b->ind + len) {
		int new_size = b->size;
		while (new_size < b->ind + len) {
			new_size *= 2;
			assert (new_size < 16384);
		}

		b->data = realloc (b->data, new_size);
		b->size = new_size;
	}

	memcpy (&b->data[b->ind], add, len);
	b->ind += len;
}

static void reset_buffer (Buffer *b) {
	b->ind = 0;
}

static int parse_payload (Buffer *b) {
	if (b->ind < 2)
		return -1;

	struct HEADER h;
	memcpy (&h, b->data, my_min (sizeof (struct HEADER), b->ind));

	h.len_ex = htons (h.len_ex);

	ESP_LOGW ("PARSE", "Payload len :%02X, len_ex: %04X", h.payload_len, h.len_ex);
	/* printf ("Payload len: %02X; ind: %02X\n", h.payload_len, b->ind); */

	if (h.payload_len <= 125) {
		if (h.payload_len + 2 == b->ind) {
			/* package is complete */
			return 0;
		}
		/* FIXME: what if more bytes are left from the next package !! */
		abort ();
	}
	if (h.payload_len == 126) {
		if (h.len_ex + 4 < b->ind)
			return 1;
		if (h.len_ex + 4 == b->ind)
			return 0;
		/* FIXME: same fixme as above */
		abort ();
	}

	return 1;
}

static TxBuff pong = {"2", 1, 0x01};

static void gg_websockets_task (void *pvParameters) {

	ESP_LOGI(TAG, "Trying to Start the WSS..");

#define TX_TIMER_MAX_CNT	10
	size_t tx_timer = 0; 			/* used for ping/pong counting */

	char buf[1024];

	Buffer payload = {0};

	int ret, flags, len;

	mbedtls_ssl_context ssl;
	mbedtls_entropy_context entropy;
	mbedtls_ctr_drbg_context ctr_drbg;

	mbedtls_x509_crt cacert;
	mbedtls_ssl_config conf;
	mbedtls_net_context server_fd;

	mbedtls_ssl_init(&ssl);
	mbedtls_x509_crt_init(&cacert);
	mbedtls_ctr_drbg_init(&ctr_drbg);
	ESP_LOGI(TAG, "Seeding the random number generator");

	mbedtls_ssl_config_init(&conf);

	mbedtls_entropy_init(&entropy);
	if((ret = mbedtls_ctr_drbg_seed(&ctr_drbg, mbedtls_entropy_func, &entropy,
									NULL, 0)) != 0)
	{
		ESP_LOGE(TAG, "mbedtls_ctr_drbg_seed returned %d", ret);
		abort();
	}

	ESP_LOGI(TAG, "Loading the CA root certificate...");

	ret = mbedtls_x509_crt_parse(&cacert, server_root_cert_pem_start,
								 server_root_cert_pem_end-server_root_cert_pem_start);

	if(ret < 0)
	{
		ESP_LOGE(TAG, "mbedtls_x509_crt_parse returned -0x%x\n\n", -ret);
		abort();
	}

	ESP_LOGI(TAG, "Setting hostname for TLS session...");

	/* Hostname set here should match CN in server certificate */
	if((ret = mbedtls_ssl_set_hostname(&ssl, WEB_SERVER)) != 0)
	{
		ESP_LOGE(TAG, "mbedtls_ssl_set_hostname returned -0x%x", -ret);
		abort();
	}

	ESP_LOGI(TAG, "Setting up the SSL/TLS structure...");

	if((ret = mbedtls_ssl_config_defaults(&conf,
										  MBEDTLS_SSL_IS_CLIENT,
										  MBEDTLS_SSL_TRANSPORT_STREAM,
										  MBEDTLS_SSL_PRESET_DEFAULT)) != 0)
	{
		ESP_LOGE(TAG, "mbedtls_ssl_config_defaults returned %d", ret);
		goto exit;
	}

	/* MBEDTLS_SSL_VERIFY_OPTIONAL is bad for security, in this example it will print
	   a warning if CA verification fails but it will continue to connect.

	   You should consider using MBEDTLS_SSL_VERIFY_REQUIRED in your own code.
	*/
	mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_OPTIONAL);
	mbedtls_ssl_conf_ca_chain(&conf, &cacert, NULL);
	mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &ctr_drbg);
	/* FIXME: certificate negotiation sometimes gives timeout */
	mbedtls_ssl_conf_read_timeout (&conf, 900);


#ifdef CONFIG_MBEDTLS_DEBUG
	mbedtls_esp_enable_debug_log(&conf, 4);
#endif


	if ((ret = mbedtls_ssl_setup(&ssl, &conf)) != 0)
	{
		ESP_LOGE(TAG, "mbedtls_ssl_setup returned -0x%x\n\n", -ret);
		goto exit;
	}

	/* Prepare the REQUEST */

	char *REQUEST = NULL;
	char *PARAMS = NULL;

	asprintf (&PARAMS,
			  PARAMS_FORMAT,
			  session.x_auth_token, /* token */
			  session.x_auth_userid, /* userid */
			  session.x_auth_usertype); /* mode */

	asprintf (&REQUEST,
			  REQUEST_FORMAT,
			  PARAMS,
			  "x3JJHMbDL1EzLkh9GBhXDw=="); /* FIXME!!! Must be random */

	free (PARAMS);


	while(1) {
		mbedtls_net_init(&server_fd);

		ESP_LOGI(TAG, "Connecting to %s:%s...", WEB_SERVER, WEB_PORT);

		if ((ret = mbedtls_net_connect(&server_fd, WEB_SERVER,
									   WEB_PORT, MBEDTLS_NET_PROTO_TCP)) != 0)
		{
			ESP_LOGE(TAG, "mbedtls_net_connect returned -%x", -ret);
			goto exit;
		}

		ESP_LOGI(TAG, "Connected.");

		mbedtls_ssl_set_bio(&ssl, &server_fd, mbedtls_net_send,
							NULL, mbedtls_net_recv_timeout);

		ESP_LOGI(TAG, "Performing the SSL/TLS handshake...");

		while ((ret = mbedtls_ssl_handshake(&ssl)) != 0)
		{
			if (ret != MBEDTLS_ERR_SSL_WANT_READ && ret != MBEDTLS_ERR_SSL_WANT_WRITE)
			{
				ESP_LOGE(TAG, "mbedtls_ssl_handshake returned -0x%x", -ret);
				goto exit;
			}
		}

		ESP_LOGI(TAG, "Verifying peer X.509 certificate...");

		if ((flags = mbedtls_ssl_get_verify_result(&ssl)) != 0)
		{
			/* In real life, we probably want to close connection if ret != 0 */
			ESP_LOGW(TAG, "Failed to verify peer certificate!");
			bzero(buf, sizeof(buf));
			mbedtls_x509_crt_verify_info(buf, sizeof(buf), "  ! ", flags);
			ESP_LOGW(TAG, "verification info: %s", buf);
		}
		else {
			ESP_LOGI(TAG, "Certificate verified.");
		}

		ESP_LOGI(TAG, "Cipher suite is %s", mbedtls_ssl_get_ciphersuite(&ssl));

		ESP_LOGI(TAG, "Writing HTTP request...");

		size_t written_bytes = 0;
		do {
			ret = mbedtls_ssl_write(&ssl,
									(const unsigned char *)REQUEST + written_bytes,
									strlen(REQUEST) - written_bytes);
			if (ret >= 0) {
				ESP_LOGI(TAG, "%d bytes written", ret);
				written_bytes += ret;
			} else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE && ret != MBEDTLS_ERR_SSL_WANT_READ) {
				ESP_LOGE(TAG, "mbedtls_ssl_write returned -0x%x", -ret);
				goto exit;
			}
		} while(written_bytes < strlen(REQUEST));

		ESP_LOGI(TAG, "Reading response...");
		xEventGroupSetBits (wss_event_group, WSS_CONNECTED);

		do
		{
			len = sizeof(buf) - 1;
			bzero(buf, sizeof(buf));
			ret = mbedtls_ssl_read(&ssl, (unsigned char *)buf, len);

			if(ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE)
				continue;

			if(ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
				ret = 0;
				break;
			}

			if (ret == 0) {
				ESP_LOGI(TAG, "connection closed");
				break;
			}

			if(ret == MBEDTLS_ERR_SSL_TIMEOUT)
			{
				ESP_LOGI (TAG, "Read returned nothing..");

				/* SECTION: check on tx_queue */
				TxBuff tmp;
				if (xQueueReceive (tx_queue, &tmp, 10 / portTICK_PERIOD_MS)) {
					/* Got things to transmit */
					ESP_LOGW(TAG, "-> %s", tmp.buff);

					TxBuff *enc;
					tx_buff_encapsulate (&enc, &tmp, esp_random ());

					/* ESP_LOGW (TAG, "Enc len: %d; ENC: %p; ENC->buff: %p", */
					/* 		  enc->len, */
					/* 		  enc, enc->buff); */
					/* size_t ti; */
					/* for (ti=0; ti < enc->len; ++ti) { */
					/* 	printf ("%02X", enc->buff[ti]); */
					/* } */


					written_bytes = 0;
#if 1
					do {
						ret = mbedtls_ssl_write(
							&ssl,
							(const unsigned char *)enc->buff + written_bytes,
							enc->len - written_bytes);
						if (ret >= 0) {
							ESP_LOGI(TAG, "%d bytes written", ret);
							written_bytes += ret;
						} else if (ret != MBEDTLS_ERR_SSL_WANT_WRITE &&
								   ret != MBEDTLS_ERR_SSL_WANT_READ) {
							ESP_LOGE(TAG, "mbedtls_ssl_write returned -0x%x", -ret);
							goto exit;
						}
					} while(written_bytes < enc->len);
#endif
					/* free up stuff */
					free (enc->buff);
					free (enc);
				}
				else {
					tx_timer ++;
					if (tx_timer > TX_TIMER_MAX_CNT) {
						tx_timer = 0;
						xQueueSend (tx_queue, &pong, (TickType_t) 0);
					}
				}

				continue;
			}
			else if(ret < 0)
			{
				ESP_LOGE(TAG, "mbedtls_ssl_read returned -0x%x", -ret);
				break;
			}

			else {
				len = ret;

				ESP_LOGD(TAG, "%d bytes read", len);
				/* Print response directly to stdout as it is read */
				for(int i = 0; i < len; i++) {
					printf ("%02X", buf[i]);
					/* putchar(buf[i]); */
				}
				putchar ('\n');

				if (cur_state == HTTP_HEADER) {
					ESP_LOGI (TAG, "HTTP header found!");
					if (strstr (buf, "\r\n\r\n") != NULL) {
						cur_state = PAYLOAD_START;
						continue;
					}
				}
				if (cur_state == PAYLOAD_START) {
					ESP_LOGI (TAG, "got payload start: ");
					add_to_buffer (&payload, buf, len);

					if (parse_payload (&payload) == 0) {
						/* if the frame was fully received- */
						/* SECTION: handle the package */
						struct HEADER h;
						memcpy (&h, payload.data,
								my_min (sizeof (struct HEADER), payload.ind));
						int header_size = header_get_size (&h);
						payload.data[payload.ind] = 0; /* Null terminate */
						char *text = &payload.data[header_size];
						ESP_LOGW (TAG, "<-- Recv: %s", text);

						reset_buffer (&payload);
						cur_state = PAYLOAD_START;
					} else {
						cur_state = PAYLOAD_DATA;
					}
				}
			}

		} while(1);

		mbedtls_ssl_close_notify(&ssl);

	exit:
		free (REQUEST);
		mbedtls_ssl_session_reset(&ssl);
		mbedtls_net_free(&server_fd);

		if(ret != 0)
		{
			mbedtls_strerror(ret, buf, 100);
			ESP_LOGE(TAG, "Last error was: -0x%x - %s", -ret, buf);
		}

		putchar('\n'); // JSON output doesn't have a newline at end

		static int request_count;
		ESP_LOGI(TAG, "Completed %d requests", ++request_count);

		for(int countdown = 10; countdown >= 0; countdown--) {
			ESP_LOGI(TAG, "%d...", countdown);
			vTaskDelay(1000 / portTICK_PERIOD_MS);
		}
		ESP_LOGI(TAG, "OUT!");
		break;
	}

	vTaskDelete ( xHandle );

}


void gg_start_websockets () {

	/* create the event group */
	wss_event_group = xEventGroupCreate();

	/* create a queue */
	tx_queue = xQueueCreate( 10, sizeof( TxBuff ) );

	xTaskCreate(&gg_websockets_task, "wss_task", 8192, NULL, 5, &xHandle);

}
