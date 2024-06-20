/* MIT License
 *
 * Copyright (c) The c-ares project and its contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * SPDX-License-Identifier: MIT
 */
#include "ares-test.h"
#include "dns-proto.h"

#include <sstream>
#include <vector>

namespace ares {
namespace test {

TEST_F(LibraryTest, ParseTxtReplyOK) {
  DNSPacket pkt;
  std::string expected1 = "txt1.example.com";
  std::string expected2a = "txt2a";
  std::string expected2b("ABC\0ABC", 7);
  pkt.set_qid(0x1234).set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_MX))
    .add_answer(new DNSTxtRR("example.com", 100, {expected1}))
    .add_answer(new DNSTxtRR("example.com", 100, {expected2a, expected2b}));
  std::vector<byte> data = pkt.data();

  struct ares_txt_reply* txt = nullptr;
  EXPECT_EQ(ARES_SUCCESS, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  ASSERT_NE(nullptr, txt);
  EXPECT_EQ(std::vector<byte>(expected1.data(), expected1.data() + expected1.size()),
            std::vector<byte>(txt->txt, txt->txt + txt->length));

  struct ares_txt_reply* txt2 = txt->next;
  ASSERT_NE(nullptr, txt2);
  std::vector<byte> rsp = std::vector<byte>(expected2a.data(), expected2a.data() + expected2a.size());
  rsp.insert(rsp.end(), expected2b.data(), expected2b.data() + expected2b.size());
  EXPECT_EQ(rsp,
            std::vector<byte>(txt2->txt, txt2->txt + txt2->length));

  ares_free_data(txt);
}

TEST_F(LibraryTest, ParseTxtExtReplyOK) {
  DNSPacket pkt;
  std::string expected1 = "txt1.example.com";
  std::string expected2a = "txt2a";
  std::string expected2b("ABC\0ABC", 7);
  pkt.set_qid(0x1234).set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_MX))
    .add_answer(new DNSTxtRR("example.com", 100, {expected1}))
    .add_answer(new DNSTxtRR("example.com", 100, {expected2a, expected2b}));
  std::vector<byte> data = pkt.data();

  struct ares_txt_ext* txt = nullptr;
  EXPECT_EQ(ARES_SUCCESS, ares_parse_txt_reply_ext(data.data(), (int)data.size(), &txt));
  ASSERT_NE(nullptr, txt);
  EXPECT_EQ(std::vector<byte>(expected1.data(), expected1.data() + expected1.size()),
            std::vector<byte>(txt->txt, txt->txt + txt->length));
  EXPECT_EQ(1, txt->record_start);

  struct ares_txt_ext* txt2 = txt->next;
  ASSERT_NE(nullptr, txt2);
  std::vector<byte> rsp = std::vector<byte>(expected2a.data(), expected2a.data() + expected2a.size());
  rsp.insert(rsp.end(), expected2b.data(), expected2b.data() + expected2b.size());
  EXPECT_EQ(rsp,
            std::vector<byte>(txt2->txt, txt2->txt + txt2->length));
  EXPECT_EQ(1, txt2->record_start);

  ares_free_data(txt);
}

TEST_F(LibraryTest, ParseTxtMalformedReply1) {
  std::vector<byte> data = {
    0x12, 0x34,  // qid
    0x84, // response + query + AA + not-TC + not-RD
    0x00, // not-RA + not-Z + not-AD + not-CD + rc=NoError
    0x00, 0x01,  // num questions
    0x00, 0x01,  // num answer RRs
    0x00, 0x00,  // num authority RRs
    0x00, 0x00,  // num additional RRs
    // Question
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x10,  // type TXT
    0x00, 0x01,  // class IN
    // Answer 1
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x10,  // RR type
    0x00, 0x01,  // class IN
    0x01, 0x02, 0x03, 0x04, // TTL
    0x00, 0x03,  // rdata length
    0x12, 'a', 'b',  // invalid length
  };

  struct ares_txt_reply* txt = nullptr;
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  ASSERT_EQ(nullptr, txt);
}

TEST_F(LibraryTest, ParseTxtMalformedReply2) {
  std::vector<byte> data = {
    0x12, 0x34,  // qid
    0x84, // response + query + AA + not-TC + not-RD
    0x00, // not-RA + not-Z + not-AD + not-CD + rc=NoError
    0x00, 0x01,  // num questions
    0x00, 0x01,  // num answer RRs
    0x00, 0x00,  // num authority RRs
    0x00, 0x00,  // num additional RRs
    // Question
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x10,  // type TXT
    0x00, 0x01,  // class IN
    // Answer 1
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x10,  // RR type
    // truncated
  };

  struct ares_txt_reply* txt = nullptr;
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  ASSERT_EQ(nullptr, txt);
}

TEST_F(LibraryTest, ParseTxtMalformedReply3) {
  std::vector<byte> data = {
    0x12, 0x34,  // qid
    0x84, // response + query + AA + not-TC + not-RD
    0x00, // not-RA + not-Z + not-AD + not-CD + rc=NoError
    0x00, 0x01,  // num questions
    0x00, 0x01,  // num answer RRs
    0x00, 0x00,  // num authority RRs
    0x00, 0x00,  // num additional RRs
    // Question
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x10,  // type TXT
    0x00, 0x01,  // class IN
    // Answer 1
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x10,  // RR type
    0x00, 0x01,  // class IN
    0x01, 0x02, 0x03, 0x04, // TTL
    0x00, 0x13,  // rdata length INVALID
    0x02, 'a', 'b',
  };

  struct ares_txt_reply* txt = nullptr;
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  ASSERT_EQ(nullptr, txt);
}

TEST_F(LibraryTest, ParseTxtMalformedReply4) {
  std::vector<byte> data = {
    0x12, 0x34,  // qid
    0x84, // response + query + AA + not-TC + not-RD
    0x00, // not-RA + not-Z + not-AD + not-CD + rc=NoError
    0x00, 0x01,  // num questions
    0x00, 0x01,  // num answer RRs
    0x00, 0x00,  // num authority RRs
    0x00, 0x00,  // num additional RRs
    // Question
    0x07, 'e', 'x', 'a', 'm', 'p', 'l', 'e',
    0x03, 'c', 'o', 'm',
    0x00,
    0x00, 0x10,  // type TXT
    0x00, // TRUNCATED
  };

  struct ares_txt_reply* txt = nullptr;
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  ASSERT_EQ(nullptr, txt);
}

TEST_F(LibraryTest, ParseTxtReplyErrors) {
  DNSPacket pkt;
  std::string expected1 = "txt1.example.com";
  std::string expected2a = "txt2a";
  std::string expected2b = "txt2b";
  pkt.set_qid(0x1234).set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_MX))
    .add_answer(new DNSTxtRR("example.com", 100, {expected1}))
    .add_answer(new DNSTxtRR("example.com", 100, {expected1}))
    .add_answer(new DNSTxtRR("example.com", 100, {expected2a, expected2b}));
  std::vector<byte> data = pkt.data();
  struct ares_txt_reply* txt = nullptr;
  struct ares_txt_ext* txt_ext = nullptr;

  // No question.
  pkt.questions_.clear();
  data = pkt.data();
  txt = nullptr;
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  EXPECT_EQ(nullptr, txt);
  pkt.add_question(new DNSQuestion("example.com", T_MX));

#ifdef DISABLED
  // Question != answer
  pkt.questions_.clear();
  pkt.add_question(new DNSQuestion("Axample.com", T_TXT));
  data = pkt.data();
  EXPECT_EQ(ARES_ENODATA, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  pkt.questions_.clear();
  pkt.add_question(new DNSQuestion("example.com", T_TXT));
#endif

  // Two questions.
  pkt.add_question(new DNSQuestion("example.com", T_MX));
  data = pkt.data();
  txt = nullptr;
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  EXPECT_EQ(nullptr, txt);
  pkt.questions_.clear();
  pkt.add_question(new DNSQuestion("example.com", T_MX));

  // No answer.
  pkt.answers_.clear();
  data = pkt.data();
  txt = nullptr;
  EXPECT_EQ(ARES_ENODATA, ares_parse_txt_reply(data.data(), (int)data.size(), &txt));
  EXPECT_EQ(nullptr, txt);
  pkt.add_answer(new DNSTxtRR("example.com", 100, {expected1}));

  // Truncated packets.
  for (size_t len = 1; len < data.size(); len++) {
    txt = nullptr;
    EXPECT_NE(ARES_SUCCESS, ares_parse_txt_reply(data.data(), (int)len, &txt));
    EXPECT_EQ(nullptr, txt);
  }

  // Negative Length
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply(data.data(), -1, &txt));
  EXPECT_EQ(ARES_EBADRESP, ares_parse_txt_reply_ext(data.data(), -1, &txt_ext));
}

TEST_F(LibraryTest, ParseTxtReplyAllocFail) {
  DNSPacket pkt;
  std::string expected1 = "txt1.example.com";
  std::string expected2a = "txt2a";
  std::string expected2b = "txt2b";
  pkt.set_qid(0x1234).set_response().set_aa()
    .add_question(new DNSQuestion("example.com", T_MX))
    .add_answer(new DNSCnameRR("example.com", 300, "c.example.com"))
    .add_answer(new DNSTxtRR("c.example.com", 100, {expected1}))
    .add_answer(new DNSTxtRR("c.example.com", 100, {expected1}))
    .add_answer(new DNSTxtRR("c.example.com", 100, {expected2a, expected2b}));
  std::vector<byte> data = pkt.data();
  struct ares_txt_reply* txt = nullptr;

  for (int ii = 1; ii <= 13; ii++) {
    ClearFails();
    SetAllocFail(ii);
    EXPECT_EQ(ARES_ENOMEM, ares_parse_txt_reply(data.data(), (int)data.size(), &txt)) << ii;
  }
}


}  // namespace test
}  // namespace ares
