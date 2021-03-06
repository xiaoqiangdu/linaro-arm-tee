/*
 * Copyright (c) 2014, STMicroelectronics International N.V.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <kernel/tee_dispatch.h>

#include <kernel/tee_ta_manager.h>
#include <mm/core_memprot.h>
#include <mm/core_mmu.h>
#include <kernel/tee_compat.h>

/* Sessions opened from normal world */
static struct tee_ta_session_head tee_open_sessions =
TAILQ_HEAD_INITIALIZER(tee_open_sessions);

static void update_out_param(const struct tee_ta_param *in, TEE_Param *out)
{
	size_t n;

	for (n = 0; n < 4; n++) {
		switch (TEE_PARAM_TYPE_GET(in->types, n)) {
		case TEE_PARAM_TYPE_MEMREF_OUTPUT:
		case TEE_PARAM_TYPE_MEMREF_INOUT:
			out[n].memref.size = in->params[n].memref.size;
			break;
		case TEE_PARAM_TYPE_VALUE_OUTPUT:
		case TEE_PARAM_TYPE_VALUE_INOUT:
			out[n].value = in->params[n].value;
			break;
		default:
			break;
		}
	}
}

static TEE_Result update_clnt_id(const TEE_Identity *in, TEE_Identity *out)
{
	/*
	 * Check that only login types from normal world are allowed.
	 */
	out->login = in->login;
	switch (out->login) {
	case TEE_LOGIN_PUBLIC:
	case TEE_LOGIN_KERNEL:
		memset(&out->uuid, 0, sizeof(TEE_UUID));
		break;
	case TEE_LOGIN_USER:
	case TEE_LOGIN_GROUP:
	case TEE_LOGIN_APPLICATION:
	case TEE_LOGIN_APPLICATION_USER:
	case TEE_LOGIN_APPLICATION_GROUP:
		out->uuid = in->uuid;
		break;
	default:
		return TEE_ERROR_BAD_PARAMETERS;
	}
	return TEE_SUCCESS;
}

TEE_Result tee_dispatch_open_session(struct tee_dispatch_open_session_in *in,
				     struct tee_dispatch_open_session_out *out)
{
	TEE_Result res = TEE_ERROR_BAD_PARAMETERS;
	struct tee_ta_session *s = NULL;
	uint32_t res_orig = TEE_ORIGIN_TEE;

	struct tee_ta_param *param = malloc(sizeof(struct tee_ta_param));
	TEE_Identity *clnt_id = malloc(sizeof(TEE_Identity));

	if (param == NULL || clnt_id == NULL) {
		res = TEE_ERROR_OUT_OF_MEMORY;
		goto cleanup_return;
	}

	res = update_clnt_id(&in->clnt_id, clnt_id);
	if (res != TEE_SUCCESS)
		goto cleanup_return;

	param->types = in->param_types;
	memcpy(param->params, in->params, sizeof(in->params));
	memcpy(out->params, in->params, sizeof(in->params));
	memcpy(param->param_attr, in->param_attr, sizeof(in->param_attr));

	res = tee_ta_open_session(&res_orig, &s, &tee_open_sessions, &in->uuid,
				  in->ta, clnt_id, TEE_TIMEOUT_INFINITE, param);
	if (res_orig == TEE_ORIGIN_TEE && res == TEE_ERROR_ITEM_NOT_FOUND) {
		kta_signed_header_t *ta = NULL;
		struct tee_ta_nwumap lp;

		/* Load TA */
		res = tee_ta_rpc_load(&in->uuid, &ta, &lp, &res_orig);
		if (res != TEE_SUCCESS)
			goto cleanup_return;

		res = tee_ta_open_session(&res_orig, &s, &tee_open_sessions,
					  NULL, ta, clnt_id,
					  TEE_TIMEOUT_INFINITE, param);
		if (res != TEE_SUCCESS)
			goto cleanup_return;

		s->ctx->nwumap = lp;

	}
	if (res != TEE_SUCCESS)
		goto cleanup_return;

	out->sess = (TEE_Session *)s;
	update_out_param(param, out->params);

cleanup_return:
	if (res != TEE_SUCCESS)
		DMSG("  => Error: %x of %d", (unsigned int)res, (int)res_orig);

	free(param);
	free(clnt_id);

	out->msg.err = res_orig;
	out->msg.res = res;
	return res;
}

TEE_Result tee_dispatch_close_session(struct tee_close_session_in *in)
{
	return tee_ta_close_session(in->sess, &tee_open_sessions);
}

TEE_Result tee_dispatch_invoke_command(struct tee_dispatch_invoke_command_in *
				       in,
				       struct tee_dispatch_invoke_command_out *
				       out)
{
	struct tee_ta_param param;
	struct tee_ta_session *sess; /*= (struct tee_ta_session *)arg->sess; */
	TEE_Result res;
	TEE_ErrorOrigin err;

	/* PRINTF("in tee_dispatch_invoke_command\n"); */
	/* PRINTF("arg : %08x\n", (unsigned int)arg); */

	sess = (struct tee_ta_session *)in->sess;

	res = tee_ta_verify_session_pointer(sess, &tee_open_sessions);
	if (res != TEE_SUCCESS)
		goto cleanup_return;

	param.types = in->param_types;
	memcpy(param.params, in->params, sizeof(in->params));
	memcpy(out->params, in->params, sizeof(in->params));
	memcpy(param.param_attr, in->param_attr, sizeof(in->param_attr));

	res = tee_ta_invoke_command(&err, sess, NULL,
				    TEE_TIMEOUT_INFINITE, in->cmd, &param);
	update_out_param(&param, out->params);

cleanup_return:
	out->msg.res = res;
	out->msg.err = err;
	return out->msg.res;
}

TEE_Result tee_dispatch_cancel_command(struct tee_dispatch_cancel_command_in *
				       in,
				       struct tee_dispatch_cancel_command_out *
				       out)
{
	TEE_Result res = TEE_ERROR_BAD_PARAMETERS;
	struct tee_ta_session *sess = (struct tee_ta_session *)in->sess;
	uint32_t res_orig = TEE_ORIGIN_TEE;

	res = tee_ta_verify_session_pointer(sess, &tee_open_sessions);
	if (res != TEE_SUCCESS)
		goto cleanup_return;

	res = tee_ta_cancel_command(&res_orig, sess, NULL);

cleanup_return:
	out->msg.err = res_orig;
	out->msg.res = res;
	return res;
}
