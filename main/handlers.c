#include <stdio.h>
#include <string.h>

#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_log.h"

#include "cJSON.h"

/* My include files */
#include "common.h"
#include "gg_https.h"
#include "gg_wss.h"

#include "ws2812.h"
#include "display.h"


void create_order_handler (int msg_id, char *json_s) {
	ESP_LOGI (__FUNCTION__, "ID: %d; BUFF: %s", msg_id, json_s);
	cJSON *json = cJSON_Parse(json_s);
	if (json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		ESP_LOGE (__FUNCTION__, "Error in json parsing: %s", error_ptr);
		goto end;
	}

	assert (cJSON_IsArray (json) == true);
	cJSON *obj = cJSON_GetArrayItem (json, 0);
	assert (cJSON_IsObject (obj) == true);

	cJSON *body = cJSON_GetObjectItemCaseSensitive (obj, "body");

	cJSON *err = cJSON_GetObjectItemCaseSensitive (body, "error");
	if (cJSON_IsBool (err) && cJSON_IsFalse (err)) {
		cJSON *orderId = cJSON_GetObjectItemCaseSensitive (body, "orderId");

		ESP_LOGW (__FUNCTION__, "Got order id: %d", orderId->valueint);
	} else {
		cJSON *err_msg = cJSON_GetObjectItemCaseSensitive (body, "error_msg");

		if ( cJSON_IsString (err_msg) && err_msg->valuestring != NULL) {
			ESP_LOGE (__FUNCTION__, "GG Error message: %s", err_msg->valuestring);
		}
	}

end:
	cJSON_Delete(json);
}
