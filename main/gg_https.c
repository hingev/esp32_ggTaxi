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

#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/dns.h"

#include "esp_tls.h"

/* My includes */
#include "gg_https.h"
#include "session.h"

/* Constants that aren't configurable in menuconfig */
#define WEB_SERVER "api.ggtaxi.com"
#define WEB_PORT "443"
#define WEB_URL "https://api.ggtaxi.com/"


/* Root cert for howsmyssl.com, taken from server_root_cert.pem

   The PEM file was extracted from the output of this command:
   openssl s_client -showcerts -connect dashboard.ggtaxi.com:443 </dev/null

   The CA root cert is the last cert given in the chain of certs.

   To embed it in the app binary, the PEM file is named
   in the component.mk COMPONENT_EMBED_TXTFILES variable.
*/
extern const uint8_t server_root_cert_pem_start[] asm("_binary_server_root_cert_pem_start");
extern const uint8_t server_root_cert_pem_end[]   asm("_binary_server_root_cert_pem_end");

static const char *TAG = "HTTPS";

static	const char *REQUEST_FORMAT =
	"POST " WEB_URL "v1/auth/login" " HTTP/1.1\r\n"
	"Host: "WEB_SERVER"\r\n"
	/* "User-Agent: esp-idf/1.0 esp32\r\n" */
	"Origin: https://dashboard.ggtaxi.com\r\n"
	"Referer: https://dashboard.ggtaxi.com/\r\n"
	"User-Agent: Mozilla/5.0\r\n"
	"Accept: application/json, text/plain, */*\r\n"
	"Content-Type: application/json;charset=UTF-8\r\n"
	"Content-Length: %d\r\n"
	"\r\n"
	"%s";


/* TODO: change the JSON generation */

const char *CONTENT_FORMAT =									\
	"{\"mobile\": \"%s\", \"password\": \"%s\", \"platform\": \"WEB\", \"platformVersion\": \"10\", \"appVersion\": \"toaster v0\", \"deviceName\": \"toaster\", \"deviceUUID\": \"fffffffffffff\"}";


int gg_https_login (char * mobile,
					char *password) {

	ESP_LOGI(TAG, "Trying to do HTTPS login..");

	tcpip_adapter_init();

	char buf[512];
	int ret, len;


	char *REQUEST = NULL;

	{
		size_t content_length = 0;

		char *content = NULL;
		asprintf (
			&content,
			CONTENT_FORMAT,
			CONFIG_GG_MOBILE,
			CONFIG_GG_PASSWORD
			);

		content_length = strlen (content);

		asprintf (&REQUEST,
							REQUEST_FORMAT,
							content_length,
							content);

		free (content);
	}

	assert (REQUEST != NULL);

	/* ESP_LOGI (TAG, "Request: %s", REQUEST); */

	while(1) {
		esp_tls_cfg_t cfg = {
			.cacert_pem_buf  = server_root_cert_pem_start,
			.cacert_pem_bytes = server_root_cert_pem_end - server_root_cert_pem_start,
		};

		struct esp_tls *tls = esp_tls_conn_http_new(
			WEB_URL "v1/auth/login", &cfg
			);

		if(tls != NULL) {
			ESP_LOGI(TAG, "Connection established...");
		} else {
			ESP_LOGE(TAG, "Connection failed...");
			goto exit;
		}


		size_t written_bytes = 0;
		do {
			ret = esp_tls_conn_write(tls,
															 REQUEST + written_bytes,
															 strlen(REQUEST) - written_bytes);
			if (ret >= 0) {
				ESP_LOGI(TAG, "%d bytes written", ret);
				written_bytes += ret;
			} else if (ret != MBEDTLS_ERR_SSL_WANT_READ  && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
				ESP_LOGE(TAG, "esp_tls_conn_write  returned 0x%x", ret);
				goto exit;
			}
		} while(written_bytes < strlen(REQUEST));

		ESP_LOGI(TAG, "Reading HTTP response...");

		do
		{
			len = sizeof(buf) - 1;
			bzero(buf, sizeof(buf));
			ret = esp_tls_conn_read(tls, (char *)buf, len);

			if(ret == MBEDTLS_ERR_SSL_WANT_WRITE  || ret == MBEDTLS_ERR_SSL_WANT_READ)
				continue;

			if(ret < 0)
			{
				ESP_LOGE(TAG, "esp_tls_conn_read  returned -0x%x", -ret);
				break;
			}

			if(ret == 0)
			{
				ESP_LOGI(TAG, "connection closed");
				break;
			}

			len = ret;
			ESP_LOGD(TAG, "%d bytes read", len);

			int status_code = -1;
			/* Parsing */
			enum {STATUS, HEADERS, DATA} cur_state = STATUS;
			char *ptr = NULL;
			char *cur = NULL;
			char *header = buf;
			char *body = NULL;
			size_t header_size = 0;
			for (; header_size < strlen (buf) - 4; ++header_size) {
				if (strncmp (&buf[header_size], "\r\n\r\n", 4) == 0) {
					buf[header_size] = 0;
					body = &buf[header_size+4];

					ESP_LOGI (TAG, "Header: %s", header);
					ESP_LOGI (TAG, "Body: %s", body);
				}
			}
			cur = strtok_r (header, "\r\n", &ptr);

			while (cur != NULL) {

				/* printf ("Line: [%s]\n", cur); */

				if (cur_state == STATUS) {
					status_code = atoi (&cur[9]);
					cur_state = HEADERS;
				}
				else if (cur_state == HEADERS) {
					size_t k = 0;
					while (k < strlen (cur) && cur[k++] != ':');
					cur[k-1] = 0;
					char *key = cur;
					char *value = &cur[k+1];

					if (strcmp (key, "X-Auth-UserId") == 0) {
						strcpy (session.x_auth_userid, value);
					}
					else if (strcmp (key, "X-Auth-Token") == 0) {
						strcpy (session.x_auth_token, value);
					}
					else if (strcmp (key, "X-Auth-UserType") == 0) {
						strcpy (session.x_auth_usertype, value);
					}
					else if (strcmp (key, "X-Auth-Balance") == 0) {
						strcpy (session.x_auth_balance, value);
					}
				}

				cur = strtok_r (NULL, "\r\n", &ptr);
			}

			ESP_LOGI (TAG, "Status code: %d", status_code);

			if (status_code == 400) {
				/* TODO: parse to see if an error message pops up */

				while (1) {}
			}

			assert (status_code == 200);

			ESP_LOGI (TAG, "Got the auth tokens!");

		} while(1);

	exit:
		free (REQUEST);
		esp_tls_conn_delete(tls);
		putchar('\n'); // JSON output doesn't have a newline at end

		static int request_count;
		ESP_LOGI(TAG, "Completed %d requests", ++request_count);

		/* for(int countdown = 2; countdown >= 0; countdown--) { */
		/* 	ESP_LOGI(TAG, "%d...", countdown); */
		/* 	vTaskDelay(1000 / portTICK_PERIOD_MS); */
		/* } */
		ESP_LOGI(TAG, "OUT!");
		break;
	}


	return 0;

}
