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
#include "session.h"

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


static mbedtls_ssl_recv_t websocket_recv_cb ;

static int net_would_block( const mbedtls_net_context *ctx )
{
    int err = errno;

    /*
     * Never return 'WOULD BLOCK' on a non-blocking socket
     */
    if( ( fcntl( ctx->fd, F_GETFL ) & O_NONBLOCK ) != O_NONBLOCK )
    {
        errno = err;
        return( 0 );
    }

    switch( errno = err )
    {
#if defined EAGAIN
        case EAGAIN:
#endif
#if defined EWOULDBLOCK && EWOULDBLOCK != EAGAIN
        case EWOULDBLOCK:
#endif
            return( 1 );
    }
    return( 0 );
}

static 	mbedtls_ssl_context ssl;

static int websocket_recv_cb (void *ctx,
							  unsigned char *buf,
							  size_t len) {

	printf ("State : %d\n", ssl.state);

	if (ssl.state < MBEDTLS_SSL_HANDSHAKE_OVER) {
		int ret;
		int fd = ((mbedtls_net_context *) ctx)->fd;

		if( fd < 0 )
			return( MBEDTLS_ERR_NET_INVALID_CONTEXT );

		ret = (int) read( fd, buf, len );

		if( ret < 0 )
		{
			if( net_would_block( ctx ) != 0 )
				return( MBEDTLS_ERR_SSL_WANT_READ );

			if( errno == EPIPE || errno == ECONNRESET )
				return( MBEDTLS_ERR_NET_CONN_RESET );

			if( errno == EINTR )
				return( MBEDTLS_ERR_SSL_WANT_READ );

			return( MBEDTLS_ERR_NET_RECV_FAILED );
		}

		return( ret );
	}

	else {
		int ret;
		int fd = ((mbedtls_net_context *) ctx)->fd;

		if( fd < 0 )
			return( MBEDTLS_ERR_NET_INVALID_CONTEXT );

		ret = (int) read( fd, buf, len );

		if( ret < 0 )
		{
			if( net_would_block( ctx ) != 0 )
				return( MBEDTLS_ERR_SSL_WANT_READ );

			if( errno == EPIPE || errno == ECONNRESET )
				return( MBEDTLS_ERR_NET_CONN_RESET );

			if( errno == EINTR )
				return( MBEDTLS_ERR_SSL_WANT_READ );

			return( MBEDTLS_ERR_NET_RECV_FAILED );
		}

		printf ("READ %d bytes\n", ret);
		return( ret );
	}

}

static TaskHandle_t xHandle = NULL;

static void gg_websockets_task (void *pvParameters) {

	ESP_LOGI(TAG, "Trying to Start the WSS..");

	char buf[1024];
	int ret, flags, len;

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
							websocket_recv_cb
							/* mbedtls_net_recv */
							, NULL);

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

		ESP_LOGI(TAG, "Reading HTTP response...");

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

			if(ret < 0)
			{
				ESP_LOGE(TAG, "mbedtls_ssl_read returned -0x%x", -ret);
				break;
			}

			if(ret == 0)
			{
				ESP_LOGI(TAG, "connection closed");
				break;
			}

			len = ret;
			ESP_LOGD(TAG, "%d bytes read", len);
			/* Print response directly to stdout as it is read */
			for(int i = 0; i < len; i++) {
				putchar(buf[i]);
			}
			putchar ('\n');

			/* TODO: */

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

	xTaskCreate(&gg_websockets_task, "wss_task", 8192, NULL, 5, &xHandle);

}
