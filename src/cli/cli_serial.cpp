/*
 *  Copyright (c) 2016, Nest Labs, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. Neither the name of the copyright holder nor the
 *     names of its contributors may be used to endorse or promote products
 *     derived from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 *  ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 *  LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *  CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *  SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *  INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *  CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *  ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * @file
 *   This file implements the CLI server on the serial service.
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cli/cli.hpp>
#include <cli/cli_serial.hpp>
#include <common/code_utils.hpp>
#include <common/encoding.hpp>
#include <common/tasklet.hpp>
#include <platform/serial.h>

namespace Thread {
namespace Cli {

static const char sEraseString[] = {'\b', ' ', '\b'};
static const char CRNL[] = {'\r', '\n'};
static Serial *sServer;

Serial::Serial(void)
{
    sServer = this;
}

ThreadError Serial::Start(void)
{
    mRxLength = 0;
    mTxHead = 0;
    mTxLength = 0;
    mSendLength = 0;
    otPlatSerialEnable();
    return kThreadError_None;
}

extern "C" void otPlatSerialReceived(const uint8_t *aBuf, uint16_t aBufLength)
{
    sServer->ReceiveTask(aBuf, aBufLength);
}

void Serial::ReceiveTask(const uint8_t *aBuf, uint16_t aBufLength)
{
    const uint8_t *end;

    end = aBuf + aBufLength;

    for (; aBuf < end; aBuf++)
    {
        switch (*aBuf)
        {
        case '\r':
        case '\n':
            Output(CRNL, sizeof(CRNL));

            if (mRxLength > 0)
            {
                mRxBuffer[mRxLength] = '\0';
                ProcessCommand();
            }

            break;

        case '\b':
        case 127:
            Output(sEraseString, sizeof(sEraseString));

            if (mRxLength > 0)
            {
                mRxBuffer[--mRxLength] = '\0';
            }

            break;

        default:
            Output(reinterpret_cast<const char *>(aBuf), 1);
            mRxBuffer[mRxLength++] = *aBuf;
            break;
        }
    }
}

ThreadError Serial::ProcessCommand(void)
{
    ThreadError error = kThreadError_None;

    if (mRxBuffer[mRxLength - 1] == '\n')
    {
        mRxBuffer[--mRxLength] = '\0';
    }

    if (mRxBuffer[mRxLength - 1] == '\r')
    {
        mRxBuffer[--mRxLength] = '\0';
    }

    Interpreter::ProcessLine(mRxBuffer, mRxLength, *this);

    mRxLength = 0;

    return error;
}

int Serial::Output(const char *aBuf, uint16_t aBufLength)
{
    uint16_t remaining = kTxBufferSize - mTxLength;
    uint16_t tail;

    if (aBufLength > remaining)
    {
        aBufLength = remaining;
    }

    for (int i = 0; i < aBufLength; i++)
    {
        tail = (mTxHead + mTxLength) % kTxBufferSize;
        mTxBuffer[tail] = *aBuf++;
        mTxLength++;
    }

    Send();

    return aBufLength;
}

int Serial::OutputFormat(const char *fmt, ...)
{
    char buf[kMaxLineLength];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    return Output(buf, strlen(buf));
}

void Serial::Send(void)
{
    VerifyOrExit(mSendLength == 0, ;);

    if (mTxLength > kTxBufferSize - mTxHead)
    {
        mSendLength = kTxBufferSize - mTxHead;
    }
    else
    {
        mSendLength = mTxLength;
    }

    if (mSendLength > 0)
    {
        otPlatSerialSend(reinterpret_cast<uint8_t *>(mTxBuffer + mTxHead), mSendLength);
    }

exit:
    return;
}

extern "C" void otPlatSerialSendDone(void)
{
    sServer->SendDoneTask();
}

void Serial::SendDoneTask(void)
{
    mTxHead = (mTxHead + mSendLength) % kTxBufferSize;
    mTxLength -= mSendLength;
    mSendLength = 0;

    Send();
}

}  // namespace Cli
}  // namespace Thread
