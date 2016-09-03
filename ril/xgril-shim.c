#include "xgril-shim.h"

/* A copy of the original RIL function table. */
static const RIL_RadioFunctions *origRilFunctions;

/* A copy of the ril environment passed to RIL_Init. */
static const struct RIL_Env *rilEnv;

static void onRequestCompleteShim(RIL_Token t, RIL_Errno e, void *response, size_t responselen) {
	int request;
	RequestInfo *pRI;

	pRI = (RequestInfo *)t;

	/* If pRI is null, this entire function is useless. */
	if (pRI == NULL) {
		goto null_token_exit;
	}

	request = pRI->pCI->requestNumber;

	switch (request) {
		case RIL_REQUEST_GET_SIM_STATUS:
			/* Android 7.0 mishandles RIL_CardStatus_v5.
			 * We can just fake a v6 response instead. */
			if (responselen == sizeof(RIL_CardStatus_v5)) {
				RLOGI("%s: got request %s: Upgrading response.\n",
				      __func__, requestToString(request));

				RIL_CardStatus_v5 *v5response = ((RIL_CardStatus_v5 *) response);

				RIL_CardStatus_v6 *v6response = malloc(sizeof(RIL_CardStatus_v6));

				v6response->card_state = v5response->card_state;
				v6response->universal_pin_state = v5response->universal_pin_state;
				v6response->gsm_umts_subscription_app_index = v5response->gsm_umts_subscription_app_index;
				v6response->cdma_subscription_app_index = v5response->cdma_subscription_app_index;
				v6response->ims_subscription_app_index = -1;
				v6response->num_applications = v5response->num_applications;
				memcpy(v6response->applications, v5response->applications, sizeof(RIL_AppStatus) * 8);

				rilEnv->OnRequestComplete(t, e, v6response, sizeof(RIL_CardStatus_v6));

				free(v6response);
				return;
			}
			/* If this was already a v6 reply, continue as usual. */
			break;
	}

	RLOGD("%s: got request %s: forwarded to libril.\n", __func__, requestToString(request));
null_token_exit:
	rilEnv->OnRequestComplete(t, e, response, responselen);
}

const RIL_RadioFunctions* RIL_Init(const struct RIL_Env *env, int argc, char **argv)
{
	RIL_RadioFunctions const* (*origRilInit)(const struct RIL_Env *env, int argc, char **argv);
	static RIL_RadioFunctions shimmedFunctions;
	static struct RIL_Env shimmedEnv;
	void *origRil;

	/* Shim the RIL_Env passed to the real RIL, saving a copy of the original */
	rilEnv = env;
	shimmedEnv = *env;
	shimmedEnv.OnRequestComplete = onRequestCompleteShim;

	/* Open and Init the original RIL. */

	origRil = dlopen(RIL_LIB_PATH, RTLD_LOCAL);
	if (CC_UNLIKELY(!origRil)) {
		RLOGE("%s: failed to load '" RIL_LIB_PATH  "': %s\n", __func__, dlerror());
		return NULL;
	}

	origRilInit = dlsym(origRil, "RIL_Init");
	if (CC_UNLIKELY(!origRilInit)) {
		RLOGE("%s: couldn't find original RIL_Init!\n", __func__);
		goto fail_after_dlopen;
	}

	origRilFunctions = origRilInit(&shimmedEnv, argc, argv);
	if (CC_UNLIKELY(!origRilFunctions)) {
		RLOGE("%s: the original RIL_Init derped.\n", __func__);
		goto fail_after_dlopen;
	}

	/* Shim functions as needed. */
	shimmedFunctions = *origRilFunctions;

	return &shimmedFunctions;

fail_after_dlopen:
	dlclose(origRil);
	return NULL;
}
