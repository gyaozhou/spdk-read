/*-
 *   BSD LICENSE
 *
 *   Copyright (c) Intel Corporation.
 *   All rights reserved.
 *
 *   Redistribution and use in source and binary forms, with or without
 *   modification, are permitted provided that the following conditions
 *   are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *     * Neither the name of Intel Corporation nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 *   THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *   "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *   LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *   A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *   OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *   SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *   LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *   DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *   THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *   (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *   OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "spdk/stdinc.h"

#include "spdk/log.h"
#include "spdk/thread.h"

#include "spdk_internal/event.h"
#include "spdk/env.h"

struct spdk_subsystem_list g_subsystems = TAILQ_HEAD_INITIALIZER(g_subsystems);
struct spdk_subsystem_depend_list g_subsystems_deps = TAILQ_HEAD_INITIALIZER(g_subsystems_deps);
static struct spdk_subsystem *g_next_subsystem;
static bool g_subsystems_initialized = false;
static bool g_subsystems_init_interrupted = false;
static spdk_msg_fn g_app_start_fn = NULL;
static void *g_app_start_arg = NULL;
static spdk_msg_fn g_app_stop_fn = NULL;
static void *g_app_stop_arg = NULL;
static struct spdk_thread *g_fini_thread = NULL;

// zhou: these two functions will be invoked before main(). Be careful !
//       Up to depend on relationship, we need sort them before really start them.

// zhou: list of all subsystem inited before main()
void
spdk_add_subsystem(struct spdk_subsystem *subsystem)
{
	TAILQ_INSERT_TAIL(&g_subsystems, subsystem, tailq);
}

// zhou: list of dependency relationship.
void
spdk_add_subsystem_depend(struct spdk_subsystem_depend *depend)
{
	TAILQ_INSERT_TAIL(&g_subsystems_deps, depend, tailq);
}


struct spdk_subsystem *
spdk_subsystem_find(struct spdk_subsystem_list *list, const char *name)
{
	struct spdk_subsystem *iter;

	TAILQ_FOREACH(iter, list, tailq) {
		if (strcmp(name, iter->name) == 0) {
			return iter;
		}
	}

	return NULL;
}

// zhou: will sort "g_subsystems" according to dependency!!! Just like including.
static void
subsystem_sort(void)
{
	bool depends_on, depends_on_sorted;
	struct spdk_subsystem *subsystem, *subsystem_tmp;
	struct spdk_subsystem_depend *subsystem_dep;

    // zhou: tempory list, sorted list.
	struct spdk_subsystem_list subsystems_list = TAILQ_HEAD_INITIALIZER(subsystems_list);

	while (!TAILQ_EMPTY(&g_subsystems)) {

		TAILQ_FOREACH_SAFE(subsystem, &g_subsystems, tailq, subsystem_tmp) {
            // zhou: this subsystem "subsystem" depends on other subsystem.
			depends_on = false;

            // zhou: once one subsystem depends on other two subsytem, there will be
            //       two entries in list "g_subsystems_deps"
			TAILQ_FOREACH(subsystem_dep, &g_subsystems_deps, tailq) {

				if (strcmp(subsystem->name, subsystem_dep->name) == 0) {
					depends_on = true;
                    // zhou: already in sorted list.
					depends_on_sorted = !!spdk_subsystem_find(&subsystems_list, subsystem_dep->depends_on);
					if (depends_on_sorted) {
                        // zhou: need make sure all depended subsystem are in sorted list.
						continue;
					}

                    // zhou: the subystem be depended, not in sorted list.
					break;
				}
			}

            // zhou: in case of "depends_on == true && depends_on_sorted == false",
            //       means can't append to sorted list, wait for more depended subsytem
            //       append firstly.

			if (depends_on == false) {
                // zhou: depends on nothing, just remove from "g_subsystems" to
                //       "subsystems_list"
				TAILQ_REMOVE(&g_subsystems, subsystem, tailq);
				TAILQ_INSERT_TAIL(&subsystems_list, subsystem, tailq);
			} else {
				if (depends_on_sorted == true) {
                    // zhou: depended subsytems already in sorted list, which means
                    //       the order is correct.
					TAILQ_REMOVE(&g_subsystems, subsystem, tailq);
					TAILQ_INSERT_TAIL(&subsystems_list, subsystem, tailq);
				}
			}

            // zhou: next subsystem
		}

        // zhou: keep looping subsystem list in case not empty.
	}

    // zhou: move from sorted temporary list back to "g_subsystems"
	TAILQ_FOREACH_SAFE(subsystem, &subsystems_list, tailq, subsystem_tmp) {
		TAILQ_REMOVE(&subsystems_list, subsystem, tailq);
		TAILQ_INSERT_TAIL(&g_subsystems, subsystem, tailq);
	}
}

// zhou: find next subsystem in list to init.
void
spdk_subsystem_init_next(int rc)
{
	/* The initialization is interrupted by the spdk_subsystem_fini, so just return */
	if (g_subsystems_init_interrupted) {
		return;
	}

	if (rc) {
		SPDK_ERRLOG("Init subsystem %s failed\n", g_next_subsystem->name);
		spdk_app_stop(rc);
		return;
	}

	if (!g_next_subsystem) {
		g_next_subsystem = TAILQ_FIRST(&g_subsystems);
	} else {
		g_next_subsystem = TAILQ_NEXT(g_next_subsystem, tailq);
	}

    // zhou: no more subsystem need to be inited.
	if (!g_next_subsystem) {
		g_subsystems_initialized = true;
		g_app_start_fn(g_app_start_arg);
		return;
	}

	if (g_next_subsystem->init) {
		g_next_subsystem->init();
	} else {
		spdk_subsystem_init_next(0);
	}
}

void
spdk_subsystem_init(spdk_msg_fn cb_fn, void *cb_arg)
{
	struct spdk_subsystem_depend *dep;

	g_app_start_fn = cb_fn;
	g_app_start_arg = cb_arg;

	/* Verify that all dependency name and depends_on subsystems are registered */
	TAILQ_FOREACH(dep, &g_subsystems_deps, tailq) {
		if (!spdk_subsystem_find(&g_subsystems, dep->name)) {
			SPDK_ERRLOG("subsystem %s is missing\n", dep->name);
			spdk_app_stop(-1);
			return;
		}
		if (!spdk_subsystem_find(&g_subsystems, dep->depends_on)) {
			SPDK_ERRLOG("subsystem %s dependency %s is missing\n",
				    dep->name, dep->depends_on);
			spdk_app_stop(-1);
			return;
		}
	}
    // zhou: make sure in dependency order.
	subsystem_sort();

    // zhou: start to init each subsystem
	spdk_subsystem_init_next(0);
}

static void
_spdk_subsystem_fini_next(void *arg1)
{
	assert(g_fini_thread == spdk_get_thread());

	if (!g_next_subsystem) {
		/* If the initialized flag is false, then we've failed to initialize
		 * the very first subsystem and no de-init is needed
		 */
		if (g_subsystems_initialized) {
			g_next_subsystem = TAILQ_LAST(&g_subsystems, spdk_subsystem_list);
		}
	} else {
		if (g_subsystems_initialized || g_subsystems_init_interrupted) {
			g_next_subsystem = TAILQ_PREV(g_next_subsystem, spdk_subsystem_list, tailq);
		} else {
			g_subsystems_init_interrupted = true;
		}
	}

	while (g_next_subsystem) {
		if (g_next_subsystem->fini) {
			g_next_subsystem->fini();
			return;
		}
		g_next_subsystem = TAILQ_PREV(g_next_subsystem, spdk_subsystem_list, tailq);
	}

	g_app_stop_fn(g_app_stop_arg);
	return;
}

void
spdk_subsystem_fini_next(void)
{
	if (g_fini_thread != spdk_get_thread()) {
		spdk_thread_send_msg(g_fini_thread, _spdk_subsystem_fini_next, NULL);
	} else {
		_spdk_subsystem_fini_next(NULL);
	}
}

// zhou: be notified to stop registered subsystem one by one.
void
spdk_subsystem_fini(spdk_msg_fn cb_fn, void *cb_arg)
{
	g_app_stop_fn = cb_fn;
	g_app_stop_arg = cb_arg;

	g_fini_thread = spdk_get_thread();

	spdk_subsystem_fini_next();
}

// zhou: ask each registered subsystem to read config file.
void
spdk_subsystem_config(FILE *fp)
{
	struct spdk_subsystem *subsystem;

	TAILQ_FOREACH(subsystem, &g_subsystems, tailq) {
		if (subsystem->config) {
			subsystem->config(fp);
		}
	}
}

void
spdk_subsystem_config_json(struct spdk_json_write_ctx *w, struct spdk_subsystem *subsystem)
{
	if (subsystem && subsystem->write_config_json) {
		subsystem->write_config_json(w);
	} else {
		spdk_json_write_null(w);
	}
}
