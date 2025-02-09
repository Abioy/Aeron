/*
 * Copyright 2014 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <array>

#include <gtest/gtest.h>
#include <mintomic/mintomic.h>

#include <thread>
#include "MockAtomicBuffer.h"
#include <concurrent/logbuffer/LogAppender.h>

using namespace aeron::common::concurrent::logbuffer;
using namespace aeron::common::concurrent::mock;
using namespace aeron::common::concurrent;
using namespace aeron::common;

#define LOG_BUFFER_CAPACITY (LogBufferDescriptor::MIN_LOG_SIZE)
#define STATE_BUFFER_CAPACITY (LogBufferDescriptor::STATE_BUFFER_LENGTH)
#define HDR_LENGTH (FrameDescriptor::BASE_HEADER_LENGTH + sizeof(std::int32_t))
#define MAX_FRAME_LENGTH (1024)
#define LOG_BUFFER_UNALIGNED_CAPACITY (LogBufferDescriptor::MIN_LOG_SIZE + FrameDescriptor::FRAME_ALIGNMENT - 1)
#define SRC_BUFFER_CAPACITY (1024)

typedef std::array<std::uint8_t, LOG_BUFFER_CAPACITY> log_buffer_t;
typedef std::array<std::uint8_t, STATE_BUFFER_CAPACITY> state_buffer_t;
typedef std::array<std::uint8_t, HDR_LENGTH> hdr_t;
typedef std::array<std::uint8_t, LOG_BUFFER_UNALIGNED_CAPACITY> log_buffer_unaligned_t;
typedef std::array<std::uint8_t, SRC_BUFFER_CAPACITY> src_buffer_t;

class LogAppenderTest : public testing::Test
{
public:
    LogAppenderTest() :
        m_log(&m_logBuffer[0], m_logBuffer.size()),
        m_state(&m_stateBuffer[0], m_stateBuffer.size()),
        m_logAppender(m_log, m_state, &m_hdr[0], m_hdr.size(), MAX_FRAME_LENGTH)
    {
        m_logBuffer.fill(0);
        m_stateBuffer.fill(0);
        m_hdr.fill(0);
    }

    virtual void SetUp()
    {
        m_logBuffer.fill(0);
        m_stateBuffer.fill(0);
        m_hdr.fill(0);
    }

protected:
    MINT_DECL_ALIGNED(log_buffer_t m_logBuffer, 16);
    MINT_DECL_ALIGNED(state_buffer_t m_stateBuffer, 16);
    MINT_DECL_ALIGNED(hdr_t m_hdr, 16);
    MockAtomicBuffer m_log;
    MockAtomicBuffer m_state;
    LogAppender m_logAppender;
};

TEST_F(LogAppenderTest, shouldReportCapacity)
{
    EXPECT_EQ(m_logAppender.capacity(), LOG_BUFFER_CAPACITY);
}

TEST_F(LogAppenderTest, shouldReportMaxFrameLength)
{
    EXPECT_EQ(m_logAppender.maxFrameLength(), MAX_FRAME_LENGTH);
}

TEST_F(LogAppenderTest, shouldThrowExceptionOnInsufficientLogBufferCapacity)
{
    MockAtomicBuffer mockLog(&m_logBuffer[0], LogBufferDescriptor::MIN_LOG_SIZE - 1);

    ASSERT_THROW(
    {
        LogAppender logAppender(mockLog, m_state, &m_hdr[0], m_hdr.size(), MAX_FRAME_LENGTH);
    }, util::IllegalStateException);
}

TEST_F(LogAppenderTest, shouldThrowExceptionWhenCapacityNotMultipleOfAlignment)
{
    MINT_DECL_ALIGNED(log_buffer_unaligned_t logBuffer, 16);
    MockAtomicBuffer mockLog(&logBuffer[0], logBuffer.size());

    ASSERT_THROW(
    {
        LogAppender logAppender(mockLog, m_state, &m_hdr[0], m_hdr.size(), MAX_FRAME_LENGTH);
    }, util::IllegalStateException);
}

TEST_F(LogAppenderTest, shouldThrowExceptionOnInsufficientStateBufferCapacity)
{
    MockAtomicBuffer mockState(&m_stateBuffer[0], LogBufferDescriptor::STATE_BUFFER_LENGTH - 1);

    ASSERT_THROW(
    {
        LogAppender logAppender(m_log, mockState, &m_hdr[0], m_hdr.size(), MAX_FRAME_LENGTH);
    }, util::IllegalStateException);
}

TEST_F(LogAppenderTest, shouldThrowExceptionOnDefaultHeaderLengthLessThanBaseHeaderLength)
{
    ASSERT_THROW(
    {
        LogAppender logAppender(m_log, m_state, &m_hdr[0], FrameDescriptor::BASE_HEADER_LENGTH - 1, MAX_FRAME_LENGTH);
    }, util::IllegalStateException);
}

TEST_F(LogAppenderTest, shouldThrowExceptionOnDefaultHeaderLengthNotOnWordSizeBoundary)
{
    ASSERT_THROW(
    {
        LogAppender logAppender(m_log, m_state, &m_hdr[0], m_hdr.size() - 1, MAX_FRAME_LENGTH);
    }, util::IllegalStateException);
}

TEST_F(LogAppenderTest, shouldThrowExceptionOnMaxFrameSizeNotOnWordSizeBoundary)
{
    ASSERT_THROW(
    {
        LogAppender logAppender(m_log, m_state, &m_hdr[0], m_hdr.size(), 1001);
    }, util::IllegalStateException);
}

TEST_F(LogAppenderTest, shouldThrowExceptionWhenMaxMessageLengthExceeded)
{
    const util::index_t maxMessageLength = m_logAppender.maxMessageLength();
    MINT_DECL_ALIGNED(src_buffer_t buffer, 16);
    AtomicBuffer srcBuffer(&buffer[0], buffer.size());

    ASSERT_THROW(
    {
        m_logAppender.append(srcBuffer, 0, maxMessageLength + 1);
    }, util::IllegalArgumentException);
}

TEST_F(LogAppenderTest, shouldReportCurrentTail)
{
    const std::int32_t tailValue = 64;

    EXPECT_CALL(m_state, getInt32Ordered(LogBufferDescriptor::TAIL_COUNTER_OFFSET))
        .Times(1)
        .WillOnce(testing::Return(tailValue));

    EXPECT_EQ(m_logAppender.tailVolatile(), tailValue);
}

TEST_F(LogAppenderTest, shouldReportCurrentTailAtCapacity)
{
    const std::int32_t tailValue = LOG_BUFFER_CAPACITY + 64;

    EXPECT_CALL(m_state, getInt32Ordered(LogBufferDescriptor::TAIL_COUNTER_OFFSET))
        .Times(1)
        .WillOnce(testing::Return(tailValue));

    EXPECT_CALL(m_state, getInt32(LogBufferDescriptor::TAIL_COUNTER_OFFSET))
        .Times(1)
        .WillOnce(testing::Return(tailValue));

    EXPECT_EQ(m_logAppender.tailVolatile(), LOG_BUFFER_CAPACITY);
    EXPECT_EQ(m_logAppender.tail(), LOG_BUFFER_CAPACITY);
}