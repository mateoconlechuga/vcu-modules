/*
 * dec_user.c high level communication with the vcu and the firmware
 * channel creation, frame encoding
 *
 * Copyright (C) 2016, Sebastien Alaiwan (sebastien.alaiwan@allegrodvt.com)
 * Copyright (C) 2016, Kevin Grandemange (kevin.grandemange@allegrodvt.com)
 * Copyright (C) 2016, Antoine Gruzelle (antoine.gruzelle@allegrodvt.com)
 * Copyright (C) 2016, Allegro DVT (www.allegrodvt.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/printk.h>
#include <linux/string.h>

#include "dec_user.h"
#include "dec_mails_factory.h"
#include "al_mail.h"
#include "al_mail_private.h"
#include "al_codec_mails.h"

static void update_chan_param(struct al5_channel_status *status,
			      struct al5_mail *mail)
{
	u32 tmp_num_core;

	memcpy(&tmp_num_core, mail->body + 8, 4);
	status->num_core = (u8)tmp_num_core;
}

static int init_chan(struct al5_user *user, struct al5_mail *mail)
{
	int err;

	user->chan_uid = al5_mail_get_chan_uid(mail);
	err = !al5_chan_is_created(user);
	if (err) {
		pr_err("Chan uid is BAD");
		return -EINVAL;
	}

	return 0;
}

int al5d_user_create_channel(struct al5_user *user,
			     struct al5_channel_config *msg)
{
	struct al5_mail *feedback;
	int err;

	err = mutex_lock_killable(&user->locks[AL5_USER_CREATE]);
	if (err == -EINTR)
		return err;

	if (al5_chan_is_created(user)) {
		err = -EPERM;
		goto unlock;
	}

	err = al5_check_and_send(user,
				 al5d_create_channel_param_msg(user->uid,
							       &msg->param));
	if (err)
		goto unlock;

	feedback = al5_queue_pop_timeout(&user->queues[AL5_USER_MAIL_CREATE]);
	if (feedback)
		update_chan_param(&msg->status, feedback);
	else
		goto unlock;

	err = init_chan(user, feedback);
	al5_free_mail(feedback);
	if (err)
		goto unlock;

	mutex_unlock(&user->locks[AL5_USER_CREATE]);
	return 0;

unlock:
	pr_err("Channel wasn't created.\n");
	mutex_unlock(&user->locks[AL5_USER_CREATE]);
	return err;
}
EXPORT_SYMBOL_GPL(al5d_user_create_channel);

int al5d_user_decode_one_frame(struct al5_user *user,
			       struct al5_decode_msg *msg)
{
	int err;

	err = mutex_lock_killable(&user->locks[AL5_USER_XCODE]);
	if (err == -EINTR)
		return err;

	if (!al5_chan_is_created(user)) {
		pr_err("Cannot decode frame until channel is configured on MCU");
		err = -EPERM;
		goto unlock;
	}

	err = al5_check_and_send(user,
				 al5d_create_decode_one_frame_msg(user->chan_uid,
								  msg));

unlock:
	mutex_unlock(&user->locks[AL5_USER_XCODE]);
	return err;

}
EXPORT_SYMBOL_GPL(al5d_user_decode_one_frame);

int al5d_user_search_start_code(struct al5_user *user,
				struct al5_search_sc_msg *msg)
{
	return al5_check_and_send(user,
				  al5d_create_search_sc_mail(user->uid, msg));
}
EXPORT_SYMBOL_GPL(al5d_user_search_start_code);

int al5d_user_wait_for_status(struct al5_user *user, struct al5_params *msg)
{
	struct al5_mail *feedback;
	int err = 0;

	if (!mutex_trylock(&user->locks[AL5_USER_STATUS]))
		return -EINTR;

	if (!al5_chan_is_created(user)) {
		err = -EPERM;
		goto unlock;
	}

	feedback = al5_queue_pop(&user->queues[AL5_USER_MAIL_STATUS]);
	if (feedback)
		al5d_mail_get_status(msg, feedback);
	else
		err = -EINTR;
	al5_free_mail(feedback);

unlock:
	mutex_unlock(&user->locks[AL5_USER_STATUS]);
	return err;
}

int al5d_user_wait_for_start_code(struct al5_user *user,
				  struct al5_scstatus *msg)
{
	int err = 0;
	struct al5_mail *feedback;

	if (!mutex_trylock(&user->locks[AL5_USER_SC]))
		return -EINTR;

	feedback = al5_queue_pop(&user->queues[AL5_USER_MAIL_SC]);
	if (feedback)
		al5d_mail_get_sc_status(msg, feedback);
	else
		/* User has been unblocked */
		err = -EINTR;
	al5_free_mail(feedback);

	mutex_unlock(&user->locks[AL5_USER_SC]);
	return err;
}

