// AdaptivePerf: comprehensive profiling tool based on Linux perf
// Copyright (C) CERN. See LICENSE for details.

#include "mocks.hpp"
#include "consts.hpp"
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include <iostream>

using namespace testing;
using namespace std::chrono_literals;

TEST(StdSubclientTest, StandardCommTest1) {
  StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test_command";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      EXPECT_CALL(c, read()).Times(10)
        .WillOnce(Return("[\"<SYSCALL>\", \"251\", [\"a\", \"b\", \"C\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"testx\", "
                         "\"250\", \"250\", 12485, \"251\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"testx\", "
                         "\"251\", \"251\", 12499, \"253\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test_command\", "
                         "\"251\", \"253\", 12500, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test_command\", "
                         "\"251\", \"251\", 12550, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"253\", [\"*\", \"@\", \"a\", \"C\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"testx\", "
                         "\"250\", \"250\", 14000, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test_command\", "
                         "\"251\", \"251\", 14006, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test_command\", "
                         "\"251\", \"253\", 15271, \"0\"]"))
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_STDCOMM1));
}

TEST(StdSubclientTest, StandardCommTest2) {
  StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      EXPECT_CALL(c, read()).Times(19)
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"abc\", "
                         "\"25011\", \"25011\", 1, \"27006\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"27006\", [\"+\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"abc\", "
                         "\"25011\", \"25011\", 8, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"abc\", "
                         "\"27006\", \"27006\", 18, \"27007\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test\", "
                         "\"27007\", \"27007\", 25, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"27008\", [\"I\", \"this is a test\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"test\", "
                         "\"27007\", \"27007\", 299, \"27008\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"test\", "
                         "\"27007\", \"27007\", 299, \"27009\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"27009\", [\"this is a test123\", "
                         "\"this is a test\"]]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"27011\", [\"xyz\", \"abc\", \"+\", "
                         "\"+\", \"+\", \"@\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"test\", "
                         "\"27007\", \"27008\", 1205, \"27011\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test2\", "
                         "\"27011\", \"27011\", 1598, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"abc\", "
                         "\"27006\", \"27006\", 9571, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test\", "
                         "\"27007\", \"27007\", 11852, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test\", "
                         "\"27007\", \"27008\", 13333, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test2\", "
                         "\"27011\", \"27011\", 14005, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test\", "
                         "\"27007\", \"27009\", 38578, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"abc\", "
                         "\"25011\", \"25011\", 40000, \"0\"]"))
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_STDCOMM2));
}

TEST(StdSubclientTest, StandardCommTest3) {
   StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test_command";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      EXPECT_CALL(c, read()).Times(1)
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_STDCOMM3));
}

TEST(StdSubclientTest, StandardCommTest4) {
  StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test_command";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      EXPECT_CALL(c, read()).Times(10)
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1003\", 12951, 555, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1003\", 14000, 200, [\"X\", \"y\", \"@@@\", "
                         "\"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1003\", \"1004\", 18000, 5571, [\"a\", \"x\", "
                         "\"this is a test\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1006\", \"1006\", 19671, 21, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1006\", \"1006\", 25951, 20, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1006\", \"1007\", 27006, 129, [\"---\", \"+++\", "
                         "\"j\", \"k\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1003\", \"1003\", 30000, 995, [\"x\", \"y\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1004\", 30001, 4842, [\"b\", \"b\", \"d\", "
                         "\"this is a test123\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1003\", 44444, 190, [\"x\", \"y\", \"P\"]]"))
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_STDCOMM4));
}

TEST(StdSubclientTest, StandardCommTest5) {
   StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test_command";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      EXPECT_CALL(c, read()).Times(6)
        .WillOnce(Return("[\"<SAMPLE>\", \"cache-miss\", "
                         "\"7878\", \"7878\", 1, 7, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"cache-miss\", "
                         "\"7878\", \"7879\", 10, 29, [\"x\", \"y\", \"@@@\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"cache-miss\", "
                         "\"7878\", \"7878\", 571, 2, [\"x\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"cache-miss\", "
                         "\"7878\", \"7879\", 1000, 99, [\"x\", \"this is a test\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"cache-miss\", "
                         "\"7878\", \"7879\", 1002, 74, [\"a\", \"+++\", "
                         "\"this is a test\"]]"))
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_STDCOMM5));
}

TEST(StdSubclientTest, GetConnectionInstructionsTest) {
  StrictMock<test::MockNotifiable> notifiable;

  const std::string profiled_filename = "test_command";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, get_connection_instructions).Times(1)
        .WillOnce(Return("test_connection"));
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {

    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  ASSERT_EQ(subclient->get_connection_instructions(), "test_connection");
}

TEST(StdSubclientTest, InvalidCommTest1) {
  StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test_command";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      EXPECT_CALL(c, read()).Times(18)
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1003\", 12951, 555, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"none\"]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", \"\", \"\"]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1003\", 14000, 200, [\"X\", \"y\", \"@@@\", "
                         "\"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1003\", \"1004\", 18000, 5571, [\"a\", \"x\", "
                         "\"this is a test\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1006\", \"1006\", 19671, 21, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1006\", \"1006\", 25951, 20, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1006\", \"1006\", 25959, 20, {}]"))
        .WillOnce(Return("RUBBISH!!!!3249832048mufcoirmjc9034cr,js90-rc"))
        .WillOnce(Return("{\"rubbish\": \"RUBBISH!!!!3249832048mufcoirmjc9034cr,js90-rc\", "
                         "\"<SAMPLE>\": 1}"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1006\", \"1007\", 27006, 129, [\"---\", \"+++\", \"j\", "
                         "\"k\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1006\", \"1007\", [], {}, [\"---\", \"+++\", \"j\", \"k\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"offcpu-time\", "
                         "\"1003\", \"1003\", 30000, 995, [\"x\", \"y\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1004\", 30001, 4842, [\"b\", \"b\", \"d\", "
                         "\"this is a test123\"]]"))
        .WillOnce(Return("<<STOP>>"))
        .WillOnce(Return("[\"<SAMPLE>\", \"cache-miss\", "
                         "\"1003\", \"1003\", 40005, 555, [\"x\", \"y\", \"Z\"]]"))
        .WillOnce(Return("[\"<SAMPLE>\", \"task-clock\", "
                         "\"1003\", \"1003\", 44444, 190, [\"x\", \"y\", \"P\"]]"))
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_STDCOMM4));
}

TEST(StdSubclientTest, InvalidCommTest2) {
  StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test_command";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      EXPECT_CALL(c, read()).Times(15)
        .WillOnce(Return("[\"<SYSCALL123>\", \"251\", [\"a\", \"b\", \"C\"]]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"251\", [\"a\", \"b\", \"C\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"testx\", "
                         "\"250\", \"250\", 12485, \"251\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"testx\", "
                         "\"251\", \"251\", 12499, \"253\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test_command\", "
                         "\"251\", \"253\", 12500, \"0\"]"))
        .WillOnce(Return("h@ck3d"))
        .WillOnce(Return("{\"<SYSCALL>\": 1, \"pid\": \"10000\"}"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test_command\", "
                         "\"251\", \"251\", 12550, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"253\", [\"*\", \"@\", \"a\", \"C\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"testx\", "
                         "\"250\", \"250\", 14000, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"testx\", "
                         "\"250\", \"250\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test_command\", "
                         "\"251\", \"251\", 14006, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test_command\", "
                         "\"251\", \"253\", 15271, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", [\"*\", \"@\", \"a\", \"C\"]]"))
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_STDCOMM1));
}

TEST(StdSubclientTest, InvalidCommTest3) {
  StrictMock<test::MockNotifiable> notifiable;

  EXPECT_CALL(notifiable, notify).Times(1);

  const std::string profiled_filename = "test";
  const unsigned int buf_size = 1024;

  std::unique_ptr<aperf::Acceptor::Factory> acceptor_factory =
    std::make_unique<test::MockAcceptor::Factory>([&](test::MockAcceptor &a) {
      EXPECT_CALL(a, construct(1)).Times(1);
      EXPECT_CALL(a, real_accept(buf_size)).Times(1);
      EXPECT_CALL(a, close).Times(1);
    }, [&](test::MockConnection &c) {
      InSequence sequence;

      // 27011/27011: no exit
      // 27007/27012: no exit
      // 27007/27009: no spawning stack trace
      // 27006/27006: no new process
      // 27007/27008: no new process
      // 27015/27015: execve without any other syscall events

      EXPECT_CALL(c, read()).Times(17)
        .WillOnce(Return("[\"<SYSCALL>\", \"27006\", [\"+\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"abc\", "
                         "\"25011\", \"25011\", 8, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"abc\", "
                         "\"27006\", \"27006\", 18, \"27007\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test\", "
                         "\"27007\", \"27007\", 25, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"27008\", [\"I\", \"this is a test\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"test\", "
                         "\"27007\", \"27007\", 299, \"27009\"]"))
        .WillOnce(Return("[\"<SYSCALL>\", \"27011\", [\"xyz\", \"abc\", \"+\", "
                         "\"+\", \"+\", \"@\"]]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"test\", "
                         "\"27007\", \"27008\", 1205, \"27011\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"new_proc\", \"test\", "
                         "\"27007\", \"27008\", 1209, \"27012\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test2\", "
                         "\"27011\", \"27011\", 1598, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"abc\", "
                         "\"27006\", \"27006\", 9571, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test\", "
                         "\"27007\", \"27007\", 11852, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test\", "
                         "\"27007\", \"27008\", 13333, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"test\", "
                         "\"27007\", \"27009\", 38578, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"exit\", \"abc\", "
                         "\"25011\", \"25011\", 40000, \"0\"]"))
        .WillOnce(Return("[\"<SYSCALL_TREE>\", \"execve\", \"test\", "
                         "\"27015\", \"27015\", 40056, \"0\"]"))
        .WillOnce(Return("<STOP>"));
      EXPECT_CALL(c, close).Times(1);
    }, true);

  aperf::StdSubclient::Factory factory(acceptor_factory);
  std::unique_ptr<aperf::Subclient> subclient = factory.make_subclient(notifiable,
                                                                       profiled_filename,
                                                                       buf_size);

  subclient->process();
  nlohmann::json &result = subclient->get_result();

  ASSERT_EQ(result, nlohmann::json::parse(SUBCLIENT_EXPECTED_INVALIDCOMM3));
}
