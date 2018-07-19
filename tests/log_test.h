/*
* Copyright 2010-2018 Amazon.com, Inc. or its affiliates. All Rights Reserved.
*
* Licensed under the Apache License, Version 2.0 (the "License").
* You may not use this file except in compliance with the License.
* A copy of the License is located at
*
*  http://aws.amazon.com/apache2.0
*
* or in the "license" file accompanying this file. This file is distributed
* on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
* express or implied. See the License for the specific language governing
* permissions and limitations under the License.
*/

#include <aws/testing/aws_test_harness.h>
#include <aws/common/atomic.h>
#include <aws/common/log.h>

static void log_report_fn(const char *log_message) {
    fprintf(AWS_TESTING_REPORT_FD, "%s", log_message);
}

AWS_TEST_CASE(test_log_init_clean_up, test_log_init_clean_up_fn)
static int test_log_init_clean_up_fn(struct aws_allocator *allocator, void *ctx) {
    const int message_len = 1024;
    const int max_messages = 256;

    ASSERT_SUCCESS(aws_log_init(allocator, message_len, max_messages));
    aws_log_set_reporting_callback(log_report_fn);

    ASSERT_SUCCESS(aws_log(AWS_LOG_LEVEL_TRACE, "Oh, hello there #1.\n"));
    ASSERT_SUCCESS(aws_log_flush());
    ASSERT_SUCCESS(aws_log(AWS_LOG_LEVEL_TRACE, "Oh, hello there #2.\n"));
    ASSERT_SUCCESS(aws_log_flush());
    ASSERT_SUCCESS(aws_log(AWS_LOG_LEVEL_TRACE, "Oh, hello there #3.\n"));
    ASSERT_SUCCESS(aws_log(AWS_LOG_LEVEL_TRACE, "Oh, hello there #4.\n"));
    ASSERT_SUCCESS(aws_log(AWS_LOG_LEVEL_TRACE, "Oh, hello there #5.\n"));
    ASSERT_SUCCESS(aws_log_flush());

    ASSERT_SUCCESS(aws_log_clean_up());

    return 0;
}

AWS_TEST_CASE(test_log_overflow_message, test_log_overflow_message_fn)
static int test_log_overflow_message_fn(struct aws_allocator *allocator, void *ctx) {
    const int message_len = 75;
    const int max_messages = 1;

    ASSERT_SUCCESS(aws_log_init(allocator, message_len, max_messages));
    aws_log_set_reporting_callback(log_report_fn);

    ASSERT_SUCCESS(aws_log(AWS_LOG_LEVEL_TRACE, "This message should definitely overflow and get truncated because it is just simply way too long."));
    ASSERT_SUCCESS(aws_log(AWS_LOG_LEVEL_TRACE, "\nOverflow the memory pool, but not the message (no truncation).\n"));
    ASSERT_SUCCESS(aws_log_flush());

    ASSERT_SUCCESS(aws_log_clean_up());

    return 0;
}

void test_log_thread_fn(void *param) {
    int *running = (int *)param;
    uint64_t id = aws_thread_current_thread_id();
    int count = 0;
    while (*running) {
        if (count < 100) {
            aws_log(AWS_LOG_LEVEL_TRACE, "Hello from thread %d!\n", (unsigned)id);
            ++count;
        }
        aws_thread_current_sleep(1);
    }
}

#define AWS_TEST_LOG_THREAD_COUNT 10

AWS_TEST_CASE(test_log_threads_hammer, test_log_threads_hammer_fn)
static int test_log_threads_hammer_fn(struct aws_allocator *allocator, void *ctx) {
    const int message_len = 128;
    const int max_messages = 1024 * 16;

    ASSERT_SUCCESS(aws_log_init(allocator, message_len, max_messages));
    aws_log_set_reporting_callback(NULL);

    int running = 1;
    struct aws_thread threads[AWS_TEST_LOG_THREAD_COUNT];

    for (int i = 0; i < AWS_TEST_LOG_THREAD_COUNT; ++i) {
        aws_thread_init(threads + i, allocator);
        aws_thread_launch(threads + i, test_log_thread_fn, &running, NULL);
    }

    for (int i = 0; i < 1000; ++i) {
        aws_log_flush();
        aws_thread_current_sleep(1);
    }

    running = 0;

    for (int i = 0; i < AWS_TEST_LOG_THREAD_COUNT; ++i) {
        aws_thread_join(threads + i);
        aws_thread_clean_up(threads + i);
    }

    ASSERT_SUCCESS(aws_log_flush());

    ASSERT_SUCCESS(aws_log_clean_up());

    return 0;
}

static int log_test_thread_index;
static int log_test_thread_counts[AWS_TEST_LOG_THREAD_COUNT];
int log_test_order_correct;

static void log_report_test_order_fn(const char *log_message) {
    int thread_index, count;

    #ifdef _WIN32
        sscanf_s(log_message, "%d %d", &thread_index, &count);
    #else
        sscanf(log_message, "%d %d", &thread_index, &count);
    #endif
    
    if (log_test_thread_counts[thread_index] != count) {
        log_test_order_correct = 0;
    }
    log_test_thread_counts[thread_index]++;
}

void test_log_thread_order_fn(void *param) {
    int *running = (int *)param;
    int index = aws_atomic_add(&log_test_thread_index, 1);
    int count = 0;
    while (*running) {
        if (count < 1000) {
            aws_log(AWS_LOG_LEVEL_TRACE, "%d %d", index, count);
            ++count;
        }
        aws_thread_current_sleep(1);
    }
}

AWS_TEST_CASE(test_log_threads_order, test_log_threads_order_fn)
static int test_log_threads_order_fn(struct aws_allocator *allocator, void *ctx) {
    const int message_len = 128;
    const int max_messages = 1024 * 16;

    log_test_thread_index = 0;
    log_test_order_correct = 1;

    ASSERT_SUCCESS(aws_log_init(allocator, message_len, max_messages));
    aws_log_set_reporting_callback(log_report_test_order_fn);

    int running = 1;
    struct aws_thread threads[AWS_TEST_LOG_THREAD_COUNT];

    for (int i = 0; i < AWS_TEST_LOG_THREAD_COUNT; ++i) {
        aws_thread_init(threads + i, allocator);
        aws_thread_launch(threads + i, test_log_thread_order_fn, &running, NULL);
    }

    aws_thread_current_sleep(1000000);

    running = 0;

    for (int i = 0; i < AWS_TEST_LOG_THREAD_COUNT; ++i) {
        aws_thread_join(threads + i);
        aws_thread_clean_up(threads + i);
    }

    ASSERT_SUCCESS(aws_log_flush());

    ASSERT_SUCCESS(aws_log_clean_up());

    ASSERT_TRUE(log_test_order_correct == 1);

    return 0;
}

AWS_TEST_CASE(test_log_no_flush_for_no_leaks, test_log_no_flush_for_no_leaks_fn)
static int test_log_no_flush_for_no_leaks_fn(struct aws_allocator *allocator, void *ctx) {
    const int message_len = 128;
    const int max_messages = 1024 * 16;
    ASSERT_SUCCESS(aws_log_init(allocator, message_len, max_messages));
    aws_log_set_reporting_callback(NULL);

    for (int i = 0; i < 10; ++i)
        AWS_LOG(AWS_LOG_LEVEL_DEBUG, "This is a test log.\n");

    ASSERT_SUCCESS(aws_log_clean_up());

    return 0;
}