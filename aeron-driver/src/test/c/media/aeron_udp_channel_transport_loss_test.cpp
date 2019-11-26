/*
 * Copyright 2014-2019 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <gtest/gtest.h>

extern "C"
{
#include <protocol/aeron_udp_protocol.h>
#include <media/aeron_udp_channel_transport_loss.h>
}

class UdpChannelTransportLossTest : public testing::Test
{
public:
    UdpChannelTransportLossTest()
    {
    }
};

static int delegate_return_packets_recvmmsg(
    aeron_udp_channel_transport_t *transport,
    struct mmsghdr *msgvec,
    size_t vlen,
    int64_t *bytes_rcved,
    aeron_udp_transport_recv_func_t recv_func,
    void *clientd)
{
    const int16_t *msg_type = static_cast<int16_t *>(clientd);
    for (size_t i = 0; i < vlen; i++)
    {
        iovec *iovec = msgvec[i].msg_hdr.msg_iov;
        aeron_frame_header_t *frame_header = static_cast<aeron_frame_header_t *>(iovec[0].iov_base);
        frame_header->type = *msg_type;
        msgvec[i].msg_len = static_cast<unsigned int>(iovec[0].iov_len);
    }

    return static_cast<int>(vlen);
}

TEST_F(UdpChannelTransportLossTest, shouldDiscardAllPacketsWithRateOfOne)
{
    uint16_t msg_type = AERON_HDR_TYPE_DATA;
    aeron_udp_channel_transport_bindings_t bindings;
    aeron_udp_channel_transport_loss_params_t params;
    struct mmsghdr msgvec[2];
    const size_t vlen = 2;
    uint8_t data_0[1024];
    uint8_t data_1[1024];
    struct iovec vec[2];

    vec[0].iov_base = data_0;
    vec[0].iov_len = 1024;
    vec[1].iov_base = data_1;
    vec[1].iov_len = 1024;

    msgvec[0].msg_hdr.msg_iov = &vec[0];
    msgvec[1].msg_hdr.msg_iov = &vec[1];

    params.rate = 1.0;
    params.recv_msg_type_mask = 1U << msg_type;
    params.send_msg_type_mask = 0xFFFFF;
    params.seed = 0;

    bindings.recvmmsg_func = delegate_return_packets_recvmmsg;

    aeron_udp_channel_transport_loss_init(&bindings, &params);

    int messages_received = aeron_udp_channel_transport_loss_recvmmsg(
        NULL, msgvec, vlen, NULL, NULL, reinterpret_cast<void *>(&msg_type));

    EXPECT_EQ(messages_received, 0);
}

TEST_F(UdpChannelTransportLossTest, shouldNotDiscardAllPacketsWithRateOfOneWithDifferentMessageType)
{
    uint16_t loss_msg_type = AERON_HDR_TYPE_DATA;
    uint16_t data_msg_type = AERON_HDR_TYPE_SETUP;
    aeron_udp_channel_transport_bindings_t bindings;
    aeron_udp_channel_transport_loss_params_t params;
    struct mmsghdr msgvec[2];
    const size_t vlen = 2;
    uint8_t data_0[1024];
    uint8_t data_1[1024];
    struct iovec vec[2];

    vec[0].iov_base = data_0;
    vec[0].iov_len = 1024;
    vec[1].iov_base = data_1;
    vec[1].iov_len = 1024;

    msgvec[0].msg_hdr.msg_iov = &vec[0];
    msgvec[1].msg_hdr.msg_iov = &vec[1];

    params.rate = 1.0;
    params.recv_msg_type_mask = 1U << loss_msg_type;
    params.send_msg_type_mask = 0xFFFFF;
    params.seed = 0;

    bindings.recvmmsg_func = delegate_return_packets_recvmmsg;

    aeron_udp_channel_transport_loss_init(&bindings, &params);

    int messages_received = aeron_udp_channel_transport_loss_recvmmsg(
        NULL, msgvec, vlen, NULL, NULL, reinterpret_cast<void *>(&data_msg_type));

    EXPECT_EQ(messages_received, 2);
}

TEST_F(UdpChannelTransportLossTest, shouldNotDiscardAllPacketsWithRateOfZero)
{
    uint16_t loss_msg_type = AERON_HDR_TYPE_DATA;
    aeron_udp_channel_transport_bindings_t bindings;
    aeron_udp_channel_transport_loss_params_t params;
    struct mmsghdr msgvec[2];
    const size_t vlen = 2;
    uint8_t data_0[1024];
    uint8_t data_1[1024];
    struct iovec vec[2];

    vec[0].iov_base = data_0;
    vec[0].iov_len = 1024;
    vec[1].iov_base = data_1;
    vec[1].iov_len = 1024;

    msgvec[0].msg_hdr.msg_iov = &vec[0];
    msgvec[1].msg_hdr.msg_iov = &vec[1];

    params.rate = 0.0;
    params.recv_msg_type_mask = 1U << loss_msg_type;
    params.send_msg_type_mask = 0xFFFFF;
    params.seed = 0;

    bindings.recvmmsg_func = delegate_return_packets_recvmmsg;

    aeron_udp_channel_transport_loss_init(&bindings, &params);

    int messages_received = aeron_udp_channel_transport_loss_recvmmsg(
        NULL, msgvec, vlen, NULL, NULL, reinterpret_cast<void *>(&loss_msg_type));

    EXPECT_EQ(messages_received, 2);
}

TEST_F(UdpChannelTransportLossTest, shouldDiscardRoughlyHalfTheMessages)
{
    uint16_t msg_type = AERON_HDR_TYPE_DATA;
    aeron_udp_channel_transport_bindings_t bindings;
    aeron_udp_channel_transport_loss_params_t params;

    const size_t vlen = 10;
    struct mmsghdr msgvec[vlen];
    uint8_t data[vlen * 1024];
    struct iovec vec[vlen];

    for (size_t i = 0; i < vlen; i++)
    {
        vec[i].iov_base = &data[i * 1024];
        vec[i].iov_len = 1024;

        msgvec[i].msg_hdr.msg_iov = &vec[i];
    }

    params.rate = 0.5;
    params.recv_msg_type_mask = 1U << msg_type;
    params.send_msg_type_mask = 0xFFFFF;
    params.seed = 23764;

    bindings.recvmmsg_func = delegate_return_packets_recvmmsg;

    aeron_udp_channel_transport_loss_init(&bindings, &params);

    int messages_received = aeron_udp_channel_transport_loss_recvmmsg(
        NULL, msgvec, vlen, NULL, NULL, reinterpret_cast<void *>(&msg_type));

    EXPECT_NE(messages_received, 10);
    EXPECT_NE(messages_received, 0);
    EXPECT_EQ(messages_received, 6);
}
