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


	void local_parse (char *valuestring) {

		cJSON *newOrder = cJSON_Parse (valuestring);
		if (newOrder == NULL) {
			const char *error_ptr = cJSON_GetErrorPtr();
			ESP_LOGE (__FUNCTION__, "Error parsing the internal json: %s", error_ptr);
			goto end2;
		}

		cJSON *lat = cJSON_GetObjectItemCaseSensitive (newOrder, "latitude");
		cJSON *lng = cJSON_GetObjectItemCaseSensitive (newOrder, "longitude");

		if (cJSON_IsNumber (lat) && cJSON_IsNumber (lng)) {
			cur_status.order_lat = lat->valuedouble;
			cur_status.order_lng = lng->valuedouble;
		}

		cJSON *statusId = cJSON_GetObjectItemCaseSensitive (newOrder, "statusId");
		cJSON *orderId = cJSON_GetObjectItemCaseSensitive (newOrder, "orderId");

		int status_id = statusId->valueint;
		uint32_t order_id = orderId->valueint;

		cur_status.status_id = status_id;
		cur_status.order_id = order_id;

		ESP_LOGI (__FUNCTION__,
				  "Got ORDERID: %u; STATUS ID: %d",
				  cur_status.order_id,
				  cur_status.status_id);

		display_state_set (status_id);

		/* TODO: set the display_distance if update contains the location */
		cJSON *drv_location = cJSON_GetObjectItemCaseSensitive (newOrder, "location");
		if (cJSON_IsArray (drv_location) && cJSON_GetArraySize (drv_location) == 2) {
			double lat1, lng1;
			lat1 = cJSON_GetArrayItem (drv_location, 1)->valuedouble;
			lng1 = cJSON_GetArrayItem (drv_location, 0)->valuedouble;

			display_set_distance (calc_distance (
									  lat1, lng1,
									  cur_status.order_lat, cur_status.order_lng));

		}

	end2:
		cJSON_Delete (newOrder);

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

			cJSON *lat = cJSON_GetObjectItemCaseSensitive (ord, "latitude");
			cJSON *lng = cJSON_GetObjectItemCaseSensitive (ord, "longitude");

			if (cJSON_IsNumber (lat) && cJSON_IsNumber (lng)) {
				cur_status.order_lat = lat->valuedouble;
				cur_status.order_lng = lng->valuedouble;
			}

			int status_id = 0;
			uint32_t order_id = orderId->valueint;

			if (cJSON_IsNull (accepted_date)) {
				status_id = SEARCHING;
			}
			else if (cJSON_IsNull (waiting_date)) {
				/* TODO: fix the other cases, not sure for now */
			}

			cJSON * status_id_obj = cJSON_GetObjectItemCaseSensitive (ord, "statusId");
			if (status_id_obj != NULL &&
					cJSON_IsNumber (status_id_obj)) {
				status_id = status_id_obj->valueint;
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
		local_parse (new_order_body->valuestring);

	}
	else if (name->valuestring != NULL &&
			 strcmp (name->valuestring, "updateOrder") == 0) {
		/* this is an interesting one */
		cJSON *update_body  = cJSON_GetArrayItem (json, 1);

		cJSON *action = cJSON_GetObjectItemCaseSensitive (update_body, "action");
		assert (cJSON_IsString (action) == true);

		if (action->valuestring != NULL) {
			ESP_LOGW (__FUNCTION__,
								"Got updateOrder with action %s", action->valuestring);
		}

		cJSON *order = cJSON_GetObjectItemCaseSensitive (update_body, "order");
		assert (cJSON_IsString (order) == true);

		local_parse (order->valuestring);
	}
	else if (name->valuestring != NULL &&
					 strcmp (name->valuestring, "updateDriverLocation") == 0) {
		cJSON *obj = cJSON_GetArrayItem (json, 1);
		assert (cJSON_IsObject (obj) == true);

		double latA, lngA;
		cJSON *tmp = cJSON_GetObjectItemCaseSensitive (obj, "lat");
		assert (cJSON_IsNumber (tmp) == true);
		latA = tmp->valuedouble;

		tmp = cJSON_GetObjectItemCaseSensitive (obj, "lng");
		assert (cJSON_IsNumber (tmp) == true);
		lngA = tmp->valuedouble;

		display_set_distance (calc_distance (
								  latA, lngA,
								  cur_status.order_lat, cur_status.order_lng));

		if (cur_status.status_id == 0) {
			/* TODO: figure out how to figure out the status after a reboot */
			cur_status.status_id = IN_PROGRESS;
			display_state_set (cur_status.status_id);
		}

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

	uint32_t payment_id = 0, profile_id = 0;

	cJSON *profile = NULL;
	cJSON_ArrayForEach(profile, results)
    {
        cJSON *default_prof = cJSON_GetObjectItemCaseSensitive(profile, "default");

#if CONFIG_GG_PREFER_CARD
		cJSON *prof_id = cJSON_GetObjectItemCaseSensitive (profile, "id");

		if (prof_id->valueint != 0) {
			cJSON *payments = cJSON_GetObjectItemCaseSensitive (profile, "payments");
			assert (cJSON_IsArray (payments) == true);
			cJSON *p0 = cJSON_GetArrayItem (payments, 0);
			assert (cJSON_IsObject (p0) == true);

			cJSON *pay_id = cJSON_GetObjectItemCaseSensitive (p0, "id");

			payment_id = pay_id->valueint;
			profile_id = prof_id->valueint;
		}

#else
		if (cJSON_IsBool (default_prof) && cJSON_IsTrue (default_prof)) {
			/* SECTION: get the payment_id and profile_id  */
			cJSON *prof_id = cJSON_GetObjectItemCaseSensitive (profile, "id");

			cJSON *payments = cJSON_GetObjectItemCaseSensitive (profile, "payments");
			assert (cJSON_IsArray (payments) == true);
			cJSON *p0 = cJSON_GetArrayItem (payments, 0);
			assert (cJSON_IsObject (p0) == true);

			cJSON *pay_id = cJSON_GetObjectItemCaseSensitive (p0, "id");

			payment_id = pay_id->valueint;
			profile_id = prof_id->valueint;

		}
#endif

    }

	if (payment_id != 0) {
		ESP_LOGI (__FUNCTION__, "Got profile id: %d; payment id: %d",
				  profile_id, payment_id);


		/* SECTION: add storage into global state var */
		cur_status.profile_id = profile_id;
		cur_status.payment_id = payment_id;

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

int get_tariffs_handler (int msg_id, char *json_s, Tariff_t **types, int *size) {
	int res = 0;

	// ESP_LOGI (__FUNCTION__, "ID: %d; BUFF: %s", msg_id, json_s);

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

    // [0]["body"]["results"][1]["tariffInfo"][0]["keys"][2]["value"]["fare"]

	cJSON *err = cJSON_GetObjectItemCaseSensitive (body, "error");
	if (cJSON_IsBool (err) && cJSON_IsFalse (err)) {
		cJSON *results = cJSON_GetObjectItemCaseSensitive (body, "results");
		assert (cJSON_IsArray (results) == true);

		*size = cJSON_GetArraySize (results);
		(*types) = calloc (sizeof (Tariff_t), *size);
		int current_id = 0;

		cJSON *res = NULL;
		cJSON_ArrayForEach(res, results)
		{
			cJSON *tariff_info_arr = cJSON_GetObjectItemCaseSensitive (res, "tariffInfo");
			assert (cJSON_IsArray (tariff_info_arr) == true);
			if (cJSON_GetArraySize (tariff_info_arr) == 0)
				continue;
			cJSON *tariff_info = cJSON_GetArrayItem (tariff_info_arr, 0);
			assert (cJSON_IsObject (tariff_info) == true);

			cJSON *keys = cJSON_GetObjectItemCaseSensitive (tariff_info, "keys");
			cJSON *type_id = cJSON_GetObjectItemCaseSensitive (res, "typeId");
			assert (cJSON_IsNumber (type_id) == true);

			int itype_id = type_id->valueint;
			int minimal = -1;

			cJSON *key = NULL;
			cJSON_ArrayForEach (key, keys)
			{
				cJSON *key_name = cJSON_GetObjectItemCaseSensitive (key, "name");
				assert (cJSON_IsString (key_name) == true);

				if (key_name->valuestring != NULL &&
					strcmp (key_name->valuestring, "min") == 0) {
					cJSON *value = cJSON_GetObjectItemCaseSensitive (key, "value");
					assert (cJSON_IsNumber (value) == true);

					minimal = value->valueint;
				}
			}

			if (minimal != -1) {
				(*types)[current_id].minimal = minimal;
				(*types)[current_id].type_id = itype_id;
				current_id ++;
			}
		}
		*size = current_id;

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
