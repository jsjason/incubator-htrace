/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef APACHE_HTRACE_HTRACE_H
#define APACHE_HTRACE_HTRACE_H

#include <stdint.h> /* for uint64_t, etc. */
#include <unistd.h> /* for size_t, etc. */

/**
 * The public API for the HTrace C client.
 *
 * SPANS AND SCOPES
 * HTrace is a tracing framework for distributed systems.  The smallest unit of
 * tracing in HTrace is the trace span.  Trace spans represent intervals during
 * which a thread is performing some work.  Trace spans are identified by a
 * 64-bit ID called the trace span ID.  Trace spans can have one or more
 * parents.  The parent of a trace span is the operation or operations that
 * caused it to happen.
 *
 * Trace spans are managed by htrace_scope objects.  Creating an htrace_scope
 * (potentially) starts a trace span.  The trace span will be closed once the
 * htrace_scope is closed and freed.
 *
 * SPAN RECEIVERS
 * When a span is closed, it is sent to the current "span receiver."  Span
 * receivers decide what to do with the span data.  For example, the "local
 * file" span receiver saves the span data to a local file.  The "htraced" span
 * receiver sends the span data to the htraced daemon.
 *
 * Most interesting span receivers will start a background thread to handle
 * their workload.  This background thread will last until the associated
 * htracer is shut down.
 *
 * SAMPLING
 * HTrace is based around the concept of sampling.  That means that only some
 * trace scopes are managing spans-- the rest do nothing.  Sampling is managed
 * by htrace_sampler objects.  The two most important samplers are the
 * probability based sampler, and the "always" and "never" samplers.
 *
 * TRACERS
 * The HTrace C client eschews globals.  Instead, you are invited to create your
 * own htracer (HTrace context) object and use it throughout your program or
 * library.  The htracer object contains the logging settings and the currently
 * configured span receiver.  Tracers are thread-safe, so you can use the same
 * tracer for all of your threads if you like.
 *
 * As already mentioned, the Tracer may contain threads, so please do not call
 * htracer_create until you are ready to start threads in your program.  For
 * example, do not call it prior to daemonizing.
 *
 * COMPATIBILITY
 * When modifying this code, please try to avoid breaking binary compatibility.
 * Applications compiled against older versions of libhtrace.so should continue
 * to work when new versions of the library are dropped in.
 *
 * Adding new functions is always OK.  Modifying the type signature of existing
 * functions is not OK.  When adding structures, try to avoid including the
 * structure definition in this header, so that we can change it later on with
 * no harmful effects.  Perhaps we may need to break compatibility at some
 * point, but let's try to avoid that if we can.
 *
 * PORTABILITY
 * We aim for POSIX compatibility, although we have not done a lot of testing on
 * non-Linux systems.  Eventually, we will want to support Windows.
 */

#ifdef __cplusplus
extern  "C" {
#endif

#pragma GCC visibility push(default) // Begin publicly visible symbols

// Configuration keys.

/**
 * The path to use for the htrace client log.
 * If this is unset, we will log to stderr.
 */
#define HTRACE_LOG_PATH_KEY "log.path"

/**
 * The span receiver implementation to use.
 *
 * Possible values:
 *   noop            The "no op" span receiver, which discards all spans.
 *   local.file      A receiver which writes spans to local files.
 *   htraced         The htraced span receiver, which sends spans to htraced.
 */
#define HTRACE_SPAN_RECEIVER_KEY "span.receiver"

/**
 * The path which the local file span receiver should write spans to.
 */
#define HTRACE_LOCAL_FILE_RCV_PATH_KEY "local.file.path"

/**
 * The hostname and port which the htraced span receiver should send its spans
 * to.  This is in the format "hostname:port".
 */
#define HTRACED_ADDRESS_KEY "htraced.address"

/**
 * The maximum length of time to go before flushing spans to the htraced server.
 */
#define HTRACED_FLUSH_INTERVAL_MS_KEY "htraced.flush.interval.ms"

/**
 * The TCP write timeout to use when communicating with the htraced server.
 */
#define HTRACED_WRITE_TIMEO_MS_KEY "htraced.write.timeo.ms"

/**
 * The TCP read timeout to use when communicating with the htraced server.
 */
#define HTRACED_READ_TIMEO_MS_KEY "htraced.read.timeo.ms"

/**
 * The size of the circular buffer to use in the htraced receiver.
 */
#define HTRACED_BUFFER_SIZE_KEY "htraced.buffer.size"

/**
 * The fraction of the buffer that needs to be full to trigger the spans to be
 * sent from the htraced span receiver.
 */
#define HTRACED_BUFFER_SEND_TRIGGER_FRACTION \
    "htraced.buffer.send.trigger.fraction"

/**
 * The process ID string to use.
 *
 * %{ip} will be replaced by an IP address;
 * %{pid} will be replaced by the operating system process ID;
 * %{tname} will be replaced by the Tracer name.
 *
 * Defaults to %{tname}/%{ip}
 */
#define HTRACE_TRACER_ID "tracer.id"

/**
 * The sampler to use.
 *
 * Possible values:
 *   never          A sampler which never fires.
 *   always         A sampler which always fires.
 *   prob           A sampler which fires with some probability.
 */
#define HTRACE_SAMPLER_KEY "sampler"

/**
 * For the probability sampler, the fraction of the time that we should create a
 * new span.  This is a floating point number which is between 0.0 and 1.1,
 * inclusive.  It is _not_ a percentage.
 */
#define HTRACE_PROB_SAMPLER_FRACTION_KEY "prob.sampler.fraction"

/**
 * The length of an HTrace span ID in hexadecimal string form.
 */
#define HTRACE_SPAN_ID_STRING_LENGTH 32

    // Forward declarations
    struct htrace_conf;
    struct htracer;
    struct htrace_scope;

    /**
     * The HTrace span id.
     */
    struct htrace_span_id {
        uint64_t high;
        uint64_t low;
    };

    /**
     * Create an HTrace conf object from a string.
     *
     * The string should be in the form:
     * key1=val1;key2=val2;...
     * Entries without an equals sign will set the key to 'true'.
     *
     * The configuration object must be later freed with htrace_conf_free.
     *
     * @param values        The configuration string to parse.
     *                          You may free this string after this function
     *                          returns.
     *
     * @return              NULL on out-of-memory error; the configuration
     *                          object otherwise.
     */
    struct htrace_conf *htrace_conf_from_str(const char *values);

    /**
     * Free an htrace configuration.
     *
     * @param conf      The configuration object to free.
     */
    void htrace_conf_free(struct htrace_conf *cnf);

    /**
     * Create a Tracer.
     *
     * This function does a few things:
     *      - Initialize logging (if there are configuration tuples related to
     *          logging)
     *      - Initialize trace span receivers, if any are configured.
     *
     * This function may start background threads.
     *
     * @param tname         The name of the tracer to create.  Will be
     *                          deep-copied.  Must not be null.
     * @param conf          The configuration to use.  You may free this
     *                          configuration object after calling this
     *                          function.
     *
     * @return              NULL on OOM; the tracer otherwise.
     */
    struct htracer *htracer_create(const char *tname,
                                   const struct htrace_conf *cnf);

    /**
     * Get the Tracer name.
     *
     * @param tracer        The tracer.
     *
     * @return              The tracer name.  This string is managed by the
     *                          tracer itself and will remain valid until the
     *                          tracer is freed.
     */
    const char *htracer_tname(const struct htracer *tracer);

    /**
     * Free an HTracer.
     *
     * Frees the memory and other resources associated with a Tracer.
     * Closes the log file if there is one open.  Shuts down the span receiver
     * if there is one active.  Attempt to flush all buffered spans.
     *
     * Do not call this function until all the samplers which hold a reference
     * to this htracer have been freed.  Do not call this function if there is
     * currently an active htrace_scope object which holds a reference to this
     * tracer.
     *
     * @param tracer        The tracer to free.
     */
    void htracer_free(struct htracer *tracer);

    /**
     * Create an htrace configuration sample from a configuration.
     *
     * Samplers are thread-safe; you may use the same sampler simultaneously
     * from multiple threads.
     *
     * @param tracer        The HTracer to use.  The sampler will hold a
     *                          reference to this tracer.  Do not free the
     *                          tracer until after the sampler has been freed.
     * @param conf          The configuration to use.  You may free this
     *                          configuration object after calling this
     *                          function.
     *
     * @return              NULL if we are out of memory.
     *                      NULL if the configuration was invalid.
     *                      NULL if no sampler is configured.
     *                      The sampler otherwise.
     *                      Error conditions will be logged to the htracer log.
     */
    struct htrace_sampler *htrace_sampler_create(struct htracer *tracer,
                                                 struct htrace_conf *cnf);

    /**
     * Get the name of an HTrace sampler.
     *
     * @param smp           The sampler.
     *
     * @return              The sampler name.  This string is managed by the
     *                          sampler itself and will remain valid until the
     *                          sampler is freed.
     */
    const char *htrace_sampler_to_str(struct htrace_sampler *smp);

    /**
     * Free an htrace sampler.
     *
     * @param sampler       The sampler to free.
     */
    void htrace_sampler_free(struct htrace_sampler *smp);

    /**
     * Start a new trace span if necessary.
     *
     * You must call htrace_close_span on the scope object returned by this
     * function.
     *
     * @param tracer    The htracer to use.  Must remain valid for the
     *                      duration of the scope.
     * @param sampler   The sampler to use, or NULL for no sampler.
     *                      If no sampler is used, we will create a new span
     *                      only if there is a current active span.
     * @param desc      The description of the trace span.  Will be deep-copied.
     *
     * @return          The trace scope.  NULL if we ran out of memory, or if we
     *                      are not tracing.
     */
    struct htrace_scope* htrace_start_span(struct htracer *tracer,
                        struct htrace_sampler *sampler, const char *desc);

    /**
     * Start a new trace span with a given parent span.
     *
     * You must call htrace_close_span on the scope object returned by this
     * function.
     *
     * @param tracer    The htracer to use.  Must remain valid for the
     *                      duration of the scope.
     * @param parent    The span id of the parent span.
     *                      If the parent id either NULL or invalid, then
     *                      we do not create a new span.
     * @param desc      The description of the trace span.  Will be deep-copied.
     *
     * @return          The trace scope.  NULL if we ran out of memory, or if we
     *                      are not tracing.
     */
    struct htrace_scope* htrace_start_span_from_parent(struct htracer *tracer,
                        struct htrace_span_id *parent,
                        const char *desc);

    /**
     * Detach the trace span from the given trace scope.
     *
     * @param scope     The trace scope, or NULL.
     *
     * @return          NULL if there was no attached trace scope;
     *                  the trace scope otherwise.
     */
    struct htrace_span *htrace_scope_detach(struct htrace_scope *scope);

    /**
     * Create a new scope object with the given span.
     *
     * @param tracer    The htracer to use.
     * @param span      The trace span, or NULL.
     *
     * @return          NULL if there was no trace span;
     *                  the trace scope otherwise.
     */
    struct htrace_scope* htrace_restart_span(struct htracer *tracer,
                                             struct htrace_span *span);

    /**
     * Close a trace scope.
     *
     * This must be called from the same thread that the trace scope was created
     * in.
     *
     * @param scope     The trace scope to close.  You may pass NULL here
     *                      with no harmful effects-- it will be ignored.
     *                      If there is a span associated with the trace scope,
     *                      it will be sent to the relevant span receiver.
     *                      Then the scope and the span will be freed.
     */
    void htrace_scope_close(struct htrace_scope *scope);

    /**
     * Set a span ID to the invalid span ID by clearing it.
     *
     * @param id            The span ID to clear.
     */
    void htrace_span_id_clear(struct htrace_span_id *id);

    /**
     * Compare two span IDs.
     *
     * @param a             The first span ID.
     * @param b             The second span ID.
     *
     * @return              A number less than 0 if the first span ID is less;
     *                      A number greater than 0 if the first span ID is greater;
     *                      0 if the span IDs are equal.
     */
    int htrace_span_id_compare(const struct htrace_span_id *a,
                               const struct htrace_span_id *b);

    /**
     * Parse a string containing an HTrace span ID.
     *
     * @param id            The HTrace span ID to fill in.
     * @param str           The string to parse.
     * @param err           (out parameter) a buffer into which we will
     *                      write an error message, if parsing fails.
     *                      If this is unchanged, there was no error.
     * @param err_len       The length of the err buffer.
     *
     */
    void htrace_span_id_parse(struct htrace_span_id *id, const char *str,
                             char *err, size_t err_len);

    /**
     * Write an HTrace span ID to a string.
     *
     * @param id            The HTrace span ID.
     * @param str           Where to put the string.
     * @param len           The length of the string buffer.
     *
     * @return              1 on success; 0 if the length was not long enough, or
     *                          there was an internal snprintf error.
     */
    int htrace_span_id_to_str(const struct htrace_span_id *id,
                              char *str, size_t len);

    /**
     * Copy an htrace span ID.
     *
     * dst and src can be the same.
     *
     * @param dst           The destination span ID.
     * @param src           The source span ID.
     */
    void htrace_span_id_copy(struct htrace_span_id *dst,
                             const struct htrace_span_id *src);

    /**
     * Get the span id of an HTrace scope.
     *
     * @param scope     The trace scope, or NULL.
     * @param id        (out param) The htrace span ID object to modify.
     *                      It will be set to the invalid span ID if the scope
     *                      is null or has no span.
     */
    void htrace_scope_get_span_id(const struct htrace_scope *scope,
                                  struct htrace_span_id *id);

#pragma GCC visibility pop // End publicly visible symbols

#ifdef __cplusplus
}
#endif

#endif

// vim: ts=4:sw=4:et
