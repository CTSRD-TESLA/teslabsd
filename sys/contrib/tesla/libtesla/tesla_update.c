/*-
 * Copyright (c) 2012 Jonathan Anderson
 * All rights reserved.
 *
 * This software was developed by SRI International and the University of
 * Cambridge Computer Laboratory under DARPA/AFRL contract (FA8750-10-C-0237)
 * ("CTSRD"), as part of the DARPA CRASH research programme.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $Id$
 */

#include "tesla_internal.h"

#ifndef _KERNEL
#include <stdbool.h>
#include <inttypes.h>
#endif

#define	CHECK(fn, ...) do { \
	int err = fn(__VA_ARGS__); \
	if (err != TESLA_SUCCESS) { \
		PRINT("error in " #fn ": %s\n", tesla_strerror(err)); \
		return (err); \
	} \
} while(0)

#define	DEBUG_NAME	"libtesla.state.update"
#define PRINT(...) DEBUG(libtesla.state.update, __VA_ARGS__)

int32_t
tesla_update_state(uint32_t tesla_context, uint32_t class_id,
	const struct tesla_key *pattern,
	const char *name, const char *description,
	const struct tesla_transitions *trans)
{

	assert(ev_handlers != NULL);

	if (tesla_debugging(DEBUG_NAME)) {
		/* We should never see with multiple <<init>> transitions. */
		int init_count = 0;
		for (uint32_t i = 0; i < trans->length; i++)
			if (trans->transitions[i].flags & TESLA_TRANS_INIT)
				init_count++;

		assert(init_count < 2);
	}

	PRINT("\n====\n%s()\n", __func__);
	PRINT("  context:      %s\n",
	            (tesla_context == TESLA_SCOPE_GLOBAL
		     ? "global"
		     : "per-thread"));
	PRINT("  class:        %d ('%s')\n", class_id, name);

	PRINT("  transitions:  ");
	print_transitions(DEBUG_NAME, trans);
	PRINT("\n");
	PRINT("  key:          ");
	print_key(DEBUG_NAME, pattern);
	PRINT("\n----\n");

	struct tesla_store *store;
	CHECK(tesla_store_get, tesla_context, TESLA_MAX_CLASSES,
	    TESLA_MAX_INSTANCES, &store);
	PRINT("store: 0x%tx\n", (intptr_t) store);

	struct tesla_class *class;
	CHECK(tesla_class_get, store, class_id, &class, name, description);

	print_class(class);

	// Did we match any instances?
	bool matched_something = false;

	// When we're done, do we need to clean up the class?
	bool cleanup_required = false;

	// Make space for cloning existing instances.
	size_t cloned = 0;
	struct clone_info {
		tesla_instance *old;
		const tesla_transition *transition;
	} clones[class->tc_free];

	// Iterate over existing instances, figure out what to do with each.
	for (uint32_t i = 0; i < class->tc_limit; i++) {
		tesla_instance *inst = class->tc_instances + i;

		const tesla_transition *trigger = NULL;
		enum tesla_action_t action =
			tesla_action(inst, pattern, trans, &trigger);

		switch (action) {
		case FAIL:
			ev_handlers->teh_bad_transition(class, inst, trans);
			break;

		case IGNORE:
			break;

		case UPDATE:
			ev_handlers->teh_transition(class, inst, trigger);
			inst->ti_state = trigger->to;
			matched_something = true;

			if (trigger->flags & TESLA_TRANS_CLEANUP)
				ev_handlers->teh_accept(class, inst);

			break;

		case FORK: {
			struct clone_info *clone = clones + cloned++;
			clone->old = inst;
			clone->transition = trigger;
			matched_something = true;
			break;
		}
		}

		if (trigger && (trigger->flags & TESLA_TRANS_CLEANUP))
			cleanup_required = true;
	}

	// Move any clones into the class.
	for (size_t i = 0; i < cloned; i++) {
		struct clone_info *c = clones + i;
		struct tesla_instance *clone;
		CHECK(tesla_clone, class, c->old, &clone);

		tesla_key new_name = *pattern;
		new_name.tk_mask &= c->transition->to_mask;
		CHECK(tesla_key_union, &clone->ti_key, &new_name);

		clone->ti_state = c->transition->to;

		ev_handlers->teh_clone(class, c->old, clone, c->transition);

		if (c->transition->flags & TESLA_TRANS_CLEANUP)
			ev_handlers->teh_accept(class, clone);
	}


	// Does this transition cause class instance initialisation?
	for (uint32_t i = 0; i < trans->length; i++) {
		const tesla_transition *t = trans->transitions + i;
		if (t->flags & TESLA_TRANS_INIT) {
			struct tesla_instance *inst;
			CHECK(tesla_instance_new, class, pattern, t->to, &inst);
			assert(tesla_instance_active(inst));

			matched_something = true;
			ev_handlers->teh_init(class, inst);
		}
	}

	// Does it cause class cleanup?
	if (cleanup_required)
		tesla_class_reset(class);

	print_class(class);
	PRINT("\n====\n\n");

	if (!matched_something)
		ev_handlers->teh_fail_no_instance(class, pattern, trans);

	tesla_class_put(class);

	return (TESLA_SUCCESS);
}

enum tesla_action_t
tesla_action(const tesla_instance *inst, const tesla_key *event_data,
	const tesla_transitions *trans, const tesla_transition* *trigger)
{
	assert(trigger != NULL);

	if (!tesla_instance_active(inst))
		return IGNORE;

	/*
	 * We allowed to ignore this instance if its name doesn't match
	 * any of the given transitions.
	 */
	bool ignore = true;

	for (size_t i = 0; i < trans->length; i++) {
		const tesla_transition *t = trans->transitions + i;

		if (t->from == inst->ti_state) {
			assert(inst->ti_key.tk_mask == t->from_mask);
			assert(SUBSET(t->from_mask, t->to_mask));

			/*
			 * We need to match events against a pattern based on
			 * data from the event, but ignoring parts that are
			 * extraneous to this transition.
			 *
			 * For instance, if the event is 'foo(x,y) == z', we
			 * know what the values of x, y and z are, but the
			 * transition in question may only care about x and z:
			 * 'foo(x,*) == z'.
			 */
			tesla_key pattern = *event_data;
			pattern.tk_mask &= t->from_mask;

			/*
			 * Does the transition cause key data to be added
			 * to the instance's name?
			 */
			if (t->from_mask == t->to_mask) {
				/*
				 * No: just just update the instance
				 *     if its (masked) name matches.
				 */
				tesla_key masked_name = inst->ti_key;
				masked_name.tk_mask &= pattern.tk_mask;

				if (tesla_key_matches(&pattern, &masked_name)) {
					*trigger = t;
					return UPDATE;
				}

			} else {
				/*
				 * Yes: we need to fork the generic instance
				 *      into a more specific one.
				 */
				if (tesla_key_matches(&pattern, &inst->ti_key)) {
					*trigger = t;
					return FORK;
				}
			}

			/*
			 * If we are in the right state but don't match on
			 * the pattern, even with a mask, move on to the
			 * next transition.
			 */
			continue;
		}

		/*
		 * We are not in the correct state for this transition, so
		 * we can't take it.
		 *
		 * If we match the pattern, however, it means that *some*
		 * transition must match; we are no longer allowed to ignore
		 * this instance.
		 */
		if (tesla_key_matches(event_data, &inst->ti_key))
			ignore = false;
	}

	if (ignore)
		return IGNORE;

	else
		return FAIL;
}