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


void status_update_handler (char *json_s) {

	cJSON *json = cJSON_Parse(json_s);
	if (json == NULL) {
		const char *error_ptr = cJSON_GetErrorPtr();
		ESP_LOGE (__FUNCTION__, "Error in json parsing: %s", error_ptr);
		goto end;
	}


	assert (cJSON_IsArray (json) == true);
	cJSON *name = cJSON_GetArrayItem (json, 0);
	assert (cJSON_IsString (name) == true);

	if (name->valuestring != NULL &&
		strcmp (name->valuestring, "status") == 0) {
		/* ?? */
		cJSON *body = cJSON_GetArrayItem (json, 1);
		assert (cJSON_IsObject (body) == true);

		cJSON *orders = cJSON_GetObjectItemCaseSensitive (body, "orders");
		assert (cJSON_IsArray (orders) == true);

		if (cJSON_GetArraySize (orders) != 0) {
			cJSON *ord = cJSON_GetArrayItem (orders, 0);
			assert (cJSON_IsObject (ord) == true);

			cJSON *orderId = cJSON_GetObjectItemCaseSensitive (ord, "orderId");

			cJSON *accepted_date = cJSON_GetObjectItemCaseSensitive (ord, "acceptedDate");
			cJSON *cancel_date = cJSON_GetObjectItemCaseSensitive (ord, "cancelDate");
			cJSON *waiting_date = cJSON_GetObjectItemCaseSensitive (ord, "waitingDate");
			cJSON *processing_date = cJSON_GetObjectItemCaseSensitive (ord, "processingDate");

			int status_id = 0;
			uint32_t order_id = orderId->valueint;

			if (cJSON_IsNull (accepted_date)) {
				status_id = SEARCHING;
			}
			else if (cJSON_IsNull (waiting_date)) {
				/* TODO: fix the other cases, not sure for now */
			}

			cur_status.status_id = status_id;
			cur_status.order_id = order_id;

			if (status_id != 0) {
				display_state_set (status_id);
			}

		}
	}
	else if (name->valuestring != NULL &&
			 strcmp (name->valuestring, "newOrder") == 0) {
		/* this is an interesting one */
		cJSON *new_order_body  = cJSON_GetArrayItem (json, 1);
		assert (cJSON_IsString (new_order_body));
		cJSON *newOrder = cJSON_Parse (new_order_body->valuestring);
		if (newOrder == NULL) {
			const char *error_ptr = cJSON_GetErrorPtr();
			ESP_LOGE (__FUNCTION__, "Error parsing the internal json: %s", error_ptr);
			goto end2;
		}

		cJSON *statusId = cJSON_GetObjectItemCaseSensitive (newOrder, "statusId");
		cJSON *orderId = cJSON_GetObjectItemCaseSensitive (newOrder, "orderId");

		/* TODO: add storage for these two */

		int status_id = statusId->valueint;
		uint32_t order_id = orderId->valueint;

		cur_status.status_id = status_id;
		cur_status.order_id = order_id;

		ESP_LOGI (__FUNCTION__,
				  "Got ORDERID: %u; STATUS ID: %d",
				  cur_status.order_id,
				  cur_status.status_id);

		if (status_id == 1) {
			display_state_set (SEARCHING);
		}
		/* TODO: add other display modes */

	end2:
		cJSON_Delete (newOrder);
	}


end:
	cJSON_Delete(json);

}

int get_profiles_handler (int msg_id, char *json_s) {
	int res = 0;
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
	assert (cJSON_IsObject (body) == true);

	cJSON *err = cJSON_GetObjectItemCaseSensitive (body, "error");
	if (cJSON_IsBool (err) && cJSON_IsTrue (err)) {
		cJSON *err_msg = cJSON_GetObjectItemCaseSensitive (body, "error_msg");

		if ( cJSON_IsString (err_msg) && err_msg->valuestring != NULL) {
			ESP_LOGE (__FUNCTION__, "GG Error message: %s", err_msg->valuestring);
		}
		goto end;
	}

	cJSON *results = cJSON_GetObjectItemCaseSensitive (body, "results");
	assert (cJSON_IsArray (results) == true);

	cJSON *profile = NULL;
	cJSON_ArrayForEach(profile, results)
    {
        cJSON *default_prof = cJSON_GetObjectItemCaseSensitive(profile, "default");

		if (cJSON_IsBool (default_prof) && cJSON_IsTrue (default_prof)) {
			/* SECTION: get the payment_id and profile_id  */
			uint32_t payment_id, profile_id;
			cJSON *prof_id = cJSON_GetObjectItemCaseSensitive (profile, "id");

			cJSON *payments = cJSON_GetObjectItemCaseSensitive (profile, "payments");
			assert (cJSON_IsArray (payments) == true);
			cJSON *p0 = cJSON_GetArrayItem (payments, 0);
			assert (cJSON_IsObject (p0) == true);

			cJSON *pay_id = cJSON_GetObjectItemCaseSensitive (p0, "id");

			payment_id = pay_id->valueint;
			profile_id = prof_id->valueint;

			ESP_LOGW (__FUNCTION__, "Got profile id: %d; payment id: %d",
					  profile_id, payment_id);
			/* TODO: add storage into global state var */
		}
    }


end:
	cJSON_Delete(json);
	return res;
}

int create_order_handler (int msg_id, char *json_s) {
	int res = 0;
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
	return res;
}
